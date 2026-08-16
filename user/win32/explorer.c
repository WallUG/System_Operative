/* MyOS - user/win32/explorer.c
 * Explorador de archivos Win32 (Fase 24-P3.2): ventana top-level con
 * un SysListView32 (comctl32) de columnas Nombre/Tam sobre el MEFS.
 * - Enter o doble clic: subdirectorio -> navega; ".." -> sube; .exe/
 *   .elf -> lanza (fork+exec); el resto -> visor de texto (GDI en el
 *   cliente del top-level; 'b' vuelve).
 * - Teclado: flechas/Home/End/PgUp/PgDn (scroll automatico en el
 *   listview), 'b' sube, 'q' o X cierran. Clic en una fila la
 *   selecciona (LVM_SETSELECTIONMARK).
 * - Tests headless: 'i' inyecta un clic simple en la fila 0, 'd' un
 *   doble clic (SYS_MOUSE_INJECT); tras lanzar una app se inyectan
 *   clics periodicos en el boton X de su ventana hasta que cierre. */

#include <windows.h>
#include <commctrl.h>
#include <stdint.h>

#define SYS_EXIT    2
#define SYS_FORK    3
#define SYS_EXEC    4
#define SYS_WRITE   7
#define SYS_MALLOC  10
#define SYS_DREAD   12
#define SYS_DLISTDIR 33
#define SYS_DPARENT 34
#define SYS_DLOOKUP 35
#define SYS_MOUSE_INJECT 36
#define SYS_TICKS   41

#define MEFS_ROOT 0xFFFFFFFFu
#define MAXF      60
#define VIEW_MAX  2600
#define CHILD_X   2               /* posicion del listview en el cliente */
#define CHILD_Y   2
#define LV_HDR    18              /* LV_HEADER_H del listview */
#define ROW_H     16
#define FRAME     2               /* WM_FRAME del kernel */
#define TITLE     20              /* WM_TITLE_H del kernel */
#define DBL_TICKS 30              /* doble clic: < 300 ms a 100 Hz */
#define X_PERIOD  250             /* inyeccion periodica del boton X */
#define X_TRIES   14
#define X_X       676             /* boton X de la ventana de la app */
#define X_Y       43

typedef struct {
    char     name[16];
    uint32_t size;
    uint32_t flags;
} fent_t;

static fent_t files[MAXF];
static int nfiles;
static uint32_t cwd = MEFS_ROOT;

static HWND hMain, hList;
static char *view_text;
static char view_name[16];
static int view_trunc;
static int viewing;
static int last_row = -1;
static uint32_t last_ticks;
static int pending_x;
static uint32_t x_ticks;
static int x_tries;
static int btn_armed;               /* test P4: secuencia MAX/rest/MIN/X */
static int btn_stage;
static int x_skip;                  /* periodos a saltar antes del X */

/* --- syscalls --- */

static int sys_dlistdir(uint32_t parent, uint32_t idx, char *name,
                        uint32_t *size, uint32_t *flags)
{
    int r;
    uint32_t out[6];
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(SYS_DLISTDIR), "b"(parent), "c"(idx), "d"(out)
                     : "memory");
    if (r != 0)
        return r;
    {
        int k;
        for (k = 0; k < 16; k++)
            name[k] = (char)((uint8_t *)out)[k];
    }
    if (size) *size = out[4];
    if (flags) *flags = out[5];
    return 0;
}

static uint32_t sys_dparent(uint32_t idx)
{
    uint32_t r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(SYS_DPARENT), "b"(idx) : "memory");
    return r;
}

static int sys_dlookup(uint32_t parent, const char *name)
{
    int r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(SYS_DLOOKUP), "b"(parent), "c"(name)
                     : "memory");
    return r;
}

static int sys_dread(const char *name, char *buf, uint32_t off, uint32_t max)
{
    int r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(SYS_DREAD), "b"(name), "c"(buf), "d"(off),
                       "S"(max) : "memory");
    return r;
}

static void *sys_malloc(uint32_t n)
{
    void *p;
    __asm__ volatile("int $0x80" : "=a"(p) : "a"(SYS_MALLOC), "b"(n));
    return p;
}

static int sys_fork(void)
{
    int r;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(SYS_FORK));
    return r;
}

static int sys_exec(const char *name)
{
    int r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(SYS_EXEC), "b"(name) : "memory");
    return r;
}

static void sys_exit(int code)
{
    __asm__ volatile("int $0x80" : : "a"(SYS_EXIT), "b"(code) : "memory");
}

static uint32_t sys_ticks(void)
{
    uint32_t r;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(SYS_TICKS));
    return r;
}

static void sys_mouse_inject(int type, int x, int y)
{
    uint32_t ev[5];
    ev[0] = (uint32_t)type;
    ev[1] = (uint32_t)x;
    ev[2] = (uint32_t)y;
    ev[3] = 1;
    ev[4] = 0;
    __asm__ volatile("int $0x80" : : "a"(SYS_MOUSE_INJECT), "b"(ev)
                     : "memory");
}

/* --- serial --- */

static void logstr(const char *s)
{
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD n = 0;
    int len = 0;
    while (s[len]) len++;
    WriteFile(h, s, len, &n, 0);
}

static void lognum(int v)
{
    char b[12];
    int p = 0, u = v < 0 ? -v : v;
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (v < 0) { char c = '-'; WriteFile(h, &c, 1, &(DWORD){0}, 0); }
    do { b[p++] = (char)('0' + u % 10); u /= 10; } while (u);
    while (p > 0) WriteFile(h, &b[--p], 1, &(DWORD){0}, 0);
}

/* --- directorio --- */

static void load_dir(void)
{
    int i;
    nfiles = 0;
    for (i = 0; i < MAXF; i++) {
        if (sys_dlistdir(cwd, (uint32_t)i, files[nfiles].name,
                         &files[nfiles].size, &files[nfiles].flags) != 0)
            break;
        nfiles++;
    }
}

static void itoa_dec(char *out, uint32_t v)
{
    char tmp[16];
    int k = 0, p = 0;
    do { tmp[k++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (k > 0)
        out[p++] = tmp[--k];
    out[p] = 0;
}

/* (Re)llena el listview con ".." (si no estamos en la raiz) + entradas
 * del directorio actual. */
static void fill_list(void)
{
    LVITEMA it;
    char sz[16];
    int i, n = 0;

    SendMessageA(hList, LVM_DELETEALLITEMS, 0, 0);
    it.mask = LVIF_TEXT;
    it.iSubItem = 0;
    if (cwd != MEFS_ROOT) {
        it.iItem = 0;
        it.pszText = "..";
        SendMessageA(hList, LVM_INSERTITEMA, 0, (LPARAM)&it);
        n = 1;
    }
    for (i = 0; i < nfiles; i++) {
        it.iItem = n;
        it.pszText = files[i].name;
        SendMessageA(hList, LVM_INSERTITEMA, n, (LPARAM)&it);
        if (files[i].flags & 1) {   /* directorio: "/" en la col Tam */
            it.iSubItem = 1;
            it.pszText = "-";
            SendMessageA(hList, LVM_INSERTITEMA, n, (LPARAM)&it);
            it.iSubItem = 0;
        } else {
            itoa_dec(sz, files[i].size);
            it.iSubItem = 1;
            it.pszText = sz;
            SendMessageA(hList, LVM_INSERTITEMA, n, (LPARAM)&it);
            it.iSubItem = 0;
        }
        n++;
    }
    SendMessageA(hList, LVM_SETSELECTIONMARK, 0, 0);
}

static void cd_to(uint32_t p)
{
    cwd = p;
    load_dir();
    fill_list();
    UpdateWindow(hMain);
}

/* (Re)crea el listview del cliente (tambien al volver del visor). */
static HWND create_list(HWND hw)
{
    HWND hl;
    RECT rc;
    LVCOLUMNA col;

    GetClientRect(hw, &rc);
    hl = CreateWindowExA(0, "SysListView32", "", 0,
                         CHILD_X, CHILD_Y,
                         rc.right - 4, rc.bottom - 4, hw, 0, 0, 0);
    if (!hl)
        return 0;
    col.mask = LVCF_TEXT | LVCF_WIDTH;
    col.fmt = 0;
    col.cx = 300;
    col.pszText = "Nombre";
    SendMessageA(hl, LVM_INSERTCOLUMNA, 0, (LPARAM)&col);
    col.cx = 90;
    col.pszText = "Tam";
    SendMessageA(hl, LVM_INSERTCOLUMNA, 1, (LPARAM)&col);
    return hl;
}

static void launch(const char *name)
{
    int kid;
    logstr("exp: lanzando ");
    logstr(name);
    logstr("\n");
    kid = sys_fork();
    if (kid == 0) {
        if (sys_exec(name) != 0)
            logstr("exp: exec fallo\n");
        sys_exit(2);
    }
    /* Test headless: inyectar clics periodicos en el X de la ventana
     * de la app hasta que cierre (el teclado ya no llega a nosotros). */
    pending_x = 1;
    x_ticks = sys_ticks();
    x_tries = 0;
}

/* Enter/doble clic sobre la fila 'row' del listview. */
static void activate_row(int row)
{
    char name[40];
    LVITEMA it;
    int len, fi;

    if (row < 0)
        return;
    it.mask = LVIF_TEXT;
    it.iItem = row;
    it.iSubItem = 0;
    it.pszText = name;
    it.cchTextMax = 39;
    if (SendMessageA(hList, LVM_GETITEMTEXTA, (WPARAM)row, (LPARAM)&it)
        <= 0)
        return;
    len = 0;
    while (name[len]) len++;
    if (row == 0 && cwd != MEFS_ROOT) {     /* ".." virtual */
        cd_to(sys_dparent(cwd));
        return;
    }
    fi = row - (cwd != MEFS_ROOT ? 1 : 0);
    if (fi < 0 || fi >= nfiles)
        return;
    if (files[fi].flags & 1) {              /* subdirectorio */
        int idx = sys_dlookup(cwd, name);
        if (idx >= 0) {
            logstr("exp: cd ");
            logstr(name);
            logstr("\n");
            cd_to((uint32_t)idx);
        }
        return;
    }
    if (len > 4 && name[len - 4] == '.' && name[len - 3] == 'e' &&
        ((name[len - 2] == 'x' && name[len - 1] == 'e') ||
         (name[len - 2] == 'l' && name[len - 1] == 'f'))) {
        launch(name);
        return;
    }
    /* visor de texto */
    {
        uint32_t need;
        int i, got;
        for (i = 0; i < 15 && files[fi].name[i]; i++)
            view_name[i] = files[fi].name[i];
        view_name[i] = 0;
        if (view_text == 0)
            view_text = (char *)sys_malloc(VIEW_MAX + 1);
        if (view_text == 0) {
            logstr("exp: visor sin memoria\n");
            return;
        }
        viewing = 1;
        if (files[fi].size == 0) {
            view_text[0] = 0;
            view_trunc = 0;
        } else {
            need = files[fi].size;
            view_trunc = files[fi].size > VIEW_MAX;
            if (need > VIEW_MAX)
                need = VIEW_MAX;
            got = sys_dread(view_name, view_text, 0, need);
            if (got < 0) {
                view_text[0] = 0;
                view_trunc = 0;
            } else {
                view_text[got] = 0;
            }
        }
        logstr("exp: ver ");
        logstr(view_name);
        logstr("\n");
        DestroyWindow(hList);
        UpdateWindow(hMain);
    }
}

/* Inyecta 'clicks' clics simples (DOWN+UP) en la fila 'row' del
 * listview, en coords de pantalla (test headless). */
static void inject_click_row(int row, int clicks)
{
    RECT rc;
    int cx, cy;
    GetWindowRect(hMain, &rc);
    cx = rc.left + FRAME + CHILD_X + 100;
    cy = rc.top + TITLE + FRAME + CHILD_Y + LV_HDR + row * ROW_H + 8;
    while (clicks-- > 0) {
        sys_mouse_inject(1, cx, cy);
        sys_mouse_inject(2, cx, cy);
        sys_mouse_inject(3, cx, cy);
    }
}

/* --- WndProc --- */

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_COMMAND) {
        if (LOWORD(wp) == 1) {              /* Enter en el listview */
            int sel = (int)SendMessageA(hList, LVM_GETNEXTITEM, 0, 0);
            activate_row(sel);
            return 0;
        }
        if (LOWORD(wp) == 2) {              /* 'b': subir */
            if (cwd != MEFS_ROOT) {
                cd_to(sys_dparent(cwd));
                logstr("exp: subir\n");
            }
            return 0;
        }
        if (LOWORD(wp) == 3) {              /* 'q': cerrar */
            PostMessageA(hwnd, WM_CLOSE, 0, 0);
            return 0;
        }
        if (LOWORD(wp) == 4) {              /* test: clic simple fila 0 */
            inject_click_row(0, 1);
            logstr("exp: inj clic row0\n");
            return 0;
        }
        if (LOWORD(wp) == 5) {              /* test: doble clic fila 0 */
            inject_click_row(0, 2);
            logstr("exp: inj doble row0\n");
            return 0;
        }
        if (LOWORD(wp) == 6) {              /* test 'm': ultimo .exe */
            int n = (int)SendMessageA(hList, LVM_GETITEMCOUNT, 0, 0);
            int r;
            for (r = n - 1; r >= 0; r--) {
                char nm[40];
                LVITEMA it;
                int l2 = 0;
                it.mask = LVIF_TEXT;
                it.iItem = r;
                it.iSubItem = 0;
                it.pszText = nm;
                it.cchTextMax = 39;
                if (SendMessageA(hList, LVM_GETITEMTEXTA, (WPARAM)r,
                                 (LPARAM)&it) <= 0)
                    continue;
                while (nm[l2]) l2++;
                if (l2 > 4 && nm[l2 - 4] == '.' && nm[l2 - 3] == 'e' &&
                    nm[l2 - 2] == 'x' && nm[l2 - 1] == 'e') {
                    logstr("exp: inj last-exe row=");
                    lognum(r);
                    logstr("\n");
                    activate_row(r);
                    return 0;
                }
            }
            logstr("exp: sin .exe\n");
            return 0;
        }
        if (LOWORD(wp) == 7) {              /* test 't': armar secuencia P4 */
            btn_armed = 1;
            btn_stage = 0;
            logstr("exp: secuencia botones armada\n");
            return 0;
        }
        return 0;
    }
    if (msg == WM_KEYDOWN) {
        /* Solo llega en modo visor (el listview consume el teclado en
         * modo lista). */
        if (wp == 'b' && viewing) {         /* volver a la lista */
            viewing = 0;
            hList = create_list(hMain);
            if (hList)
                fill_list();
            UpdateWindow(hMain);
            logstr("exp: vuelta lista\n");
            return 0;
        }
        if (wp == 'q') {
            PostMessageA(hwnd, WM_CLOSE, 0, 0);
            return 0;
        }
        return 0;
    }
    if (msg == WM_LBUTTONDOWN) {
        /* Coords de PANTALLA (event_to_wm pasa ev sin convertir).
         * Fila del listview = (y - child_scr_y - LV_HDR) / ROW_H. */
        int x = (int)(lp & 0xFFFF), y = (int)((uint32_t)lp >> 16);
        RECT rc;
        int cxs, cys, row;
        if (viewing)
            return 0;
        GetWindowRect(hMain, &rc);
        cxs = rc.left + FRAME + CHILD_X;
        cys = rc.top + TITLE + FRAME + CHILD_Y;
        if (x < cxs || y < cys)
            return 0;
        row = (y - cys - LV_HDR) / ROW_H;
        if (row < 0)
            return 0;
        SendMessageA(hList, LVM_SETSELECTIONMARK, (WPARAM)row, 0);
        /* doble clic: misma fila en < 300 ms */
        if (row == last_row && sys_ticks() - last_ticks < DBL_TICKS) {
            last_row = -1;
            activate_row(row);
            return 0;
        }
        last_row = row;
        last_ticks = sys_ticks();
        return 0;
    }
    if (msg == WM_PAINT) {
        if (viewing) {
            HDC dc = GetDC(hwnd);
            RECT rc, sr;
            HBRUSH br;
            int y = 22, i = 0, p = 0;
            char row[80], status[64];
            GetClientRect(hwnd, &rc);
            br = CreateSolidBrush(0x00101020u);
            FillRect(dc, &rc, br);
            SetTextColor(dc, 0x00E0E0FFu);
            TextOutA(dc, 4, 2, "Visor (b: volver, q: cerrar)", 28);
            SetTextColor(dc, 0x00C0C0C0u);
            while (view_text[i] && y + ROW_H < rc.bottom) {
                char c = view_text[i];
                if (c == '\n') {
                    TextOutA(dc, 4, y, row, p);
                    p = 0;
                    y += ROW_H;
                    i++;
                    continue;
                }
                if (p < (int)sizeof(row) - 1)
                    row[p++] = c;
                i++;
            }
            row[p] = 0;
            if (p > 0)
                TextOutA(dc, 4, y, row, p);
            /* barra de estado inferior */
            {
                int s = 0;
                const char *pre = "abierto: ";
                while (*pre && s < 62) status[s++] = *pre++;
                i = 0;
                while (view_name[i] && i < 16 && s < 62)
                    status[s++] = view_name[i++];
                if (view_trunc && s < 62) {
                    const char *tr = " (truncado)";
                    int t = 0;
                    while (tr[t] && s < 62)
                        status[s++] = tr[t++];
                }
                status[s] = 0;
                sr.left = 0; sr.top = rc.bottom - 18;
                sr.right = rc.right; sr.bottom = rc.bottom;
                br = CreateSolidBrush(0x00505090u);
                FillRect(dc, &sr, br);
                SetTextColor(dc, 0x00FFFFFFu);
                TextOutA(dc, 4, rc.bottom - 16, status, s);
            }
            ReleaseDC(hwnd, dc);
        }
        return 0;
    }
    if (msg == WM_CLOSE)
        PostQuitMessage(0);
    return DefWindowProcA(hwnd, msg, wp, lp);
}

/* --- main --- */

int main(void)
{
    WNDCLASS wc;
    HWND hw;

    logstr("exp: start\n");
    InitCommonControls();

    wc.style = 0;
    wc.lpfnWndProc = WndProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = GetModuleHandleA(0);
    wc.hIcon = 0;
    wc.hCursor = 0;
    wc.hbrBackground = 0;
    wc.lpszMenuName = 0;
    wc.lpszClassName = "Explorer";
    RegisterClassA(&wc);

    load_dir();

    hw = CreateWindowExA(0, "Explorer", "Explorador de archivos", 0,
                         100, 70, 580, 380, 0, 0, 0, 0);
    if (!hw) { logstr("exp: create fallo\n"); return 1; }
    hMain = hw;
    hList = create_list(hw);
    if (!hList) { logstr("exp: listview fallo\n"); return 1; }

    fill_list();
    logstr("exp: items=");
    lognum((int)SendMessageA(hList, LVM_GETITEMCOUNT, 0, 0));
    logstr(" cols=2\n");

    ShowWindow(hw, 1);

    /* Bucle de mensajes con poll del boton X (las apps lanzadas se
     * cierran inyectando clics periodicos: el teclado ya no llega al
     * explorador mientras la app tiene el foco). Test P4 ('t' + 'm'):
     * la secuencia inyecta MAX -> restaurar -> MIN -> X en los botones
     * de titulo de la app lanzada (metapad en 80,40). */
    for (;;) {
        MSG msg;
        if (pending_x && (sys_ticks() - x_ticks >= X_PERIOD)) {
            x_ticks = sys_ticks();
            if (x_skip > 0) {
                x_skip--;
            } else if (btn_armed && btn_stage < 2) {
                int bx = 654, by = 50;      /* MAX (646..662) */
                if (btn_stage == 1) {
                    bx = 774;               /* restaurar: el titulo del
                                             * max esta arriba (0,0,800,600):
                                             * boton 766..782, y 2..18 */
                    by = 10;
                }
                sys_mouse_inject(1, bx, by);
                sys_mouse_inject(2, bx, by);
                sys_mouse_inject(3, bx, by);
                if (btn_stage == 0)
                    logstr("exp: inj MAX\n");
                else
                    logstr("exp: inj rest\n");
                btn_stage++;
                if (btn_stage == 2) {
                    /* Test P4: pausa larga antes del X para que el test
                     * abra/cierre el dialogo de Guardar (el X cerraria
                     * la app antes). x_skip = 10 periodos = 25 s. */
                    x_tries = 0;
                    x_skip = 10;
                }
            } else {
                if (++x_tries >= X_TRIES) {
                    pending_x = 0;
                    btn_armed = 0;
                }
                sys_mouse_inject(1, X_X, X_Y);      /* EV_MOVE */
                sys_mouse_inject(2, X_X, X_Y);      /* DOWN */
                sys_mouse_inject(3, X_X, X_Y);      /* UP */
                logstr("exp: inj X\n");
            }
        }
        if (PeekMessageA(&msg, 0, 0, 0, 1)) {
            if (msg.message == WM_QUIT)
                break;
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
    }
    logstr("exp: fin\n");
    return 0;
}