/* MyOS - user/win32/listview.c
 * Prueba del SysListView32 de comctl32 (Fase 23-C11): ventana Win32
 * con un ListView real (columnas Nombre/Tam, items del MEFS via
 * SYS_DLISTDIR), seleccion por teclado (el hijo maneja las flechas) y
 * Enter -> WM_COMMAND al padre que imprime el item seleccionado y
 * lanza los .exe/.elf con fork+exec. */

#include <windows.h>
#include <commctrl.h>
#include <stdint.h>

#define SYS_FORK   3
#define SYS_EXEC   4
#define SYS_DLISTDIR 33

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

static int sys_dlistdir(uint32_t idx, char *name)
{
    int r;
    uint32_t out[6];
    __asm__ volatile("int $0x80"
                     : "=a"(r)
                     : "a"(SYS_DLISTDIR), "b"(0xFFFFFFFFu), "c"(idx),
                       "d"(out)
                     : "memory");
    if (r != 0)
        return r;
    {
        int k;
        for (k = 0; k < 15; k++)
            name[k] = (char)((uint8_t *)out)[k];
        name[15] = 0;
    }
    return 0;
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
                     : "a"(SYS_EXEC), "b"(name)
                     : "memory");
    return r;
}

static void sys_exit(int code)
{
    __asm__ volatile("int $0x80" : : "a"(2), "b"(code) : "memory");
}

static HWND hList;
static char sel_name[40];

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)hwnd; (void)lp;
    if (msg == WM_COMMAND && LOWORD(wp) == 1) {
        /* Enter en el listview: item seleccionado */
        int sel = (int)SendMessageA(hList, LVM_GETNEXTITEM, 0, 0);
        LVITEMA it;
        it.mask = LVIF_TEXT;
        it.iItem = sel;
        it.iSubItem = 0;
        it.pszText = sel_name;
        it.cchTextMax = 39;
        if (SendMessageA(hList, LVM_GETITEMTEXTA, (WPARAM)sel, (LPARAM)&it)
            > 0) {
            int len = 0;
        logstr("lv: enter sel=");
        lognum(sel);
            logstr(" name=");
            while (sel_name[len]) len++;
            WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), sel_name, len,
                      &(DWORD){0}, 0);
            logstr("\n");
            /* lanzar apps y ver archivos */
            if (len > 4 &&
                (sel_name[len-4] == '.' && sel_name[len-3] == 'e' &&
                 ((sel_name[len-2] == 'x' && sel_name[len-1] == 'e') ||
                  (sel_name[len-2] == 'l' && sel_name[len-1] == 'f')))) {
                int kid = sys_fork();
                if (kid == 0) {
                    if (sys_exec(sel_name) != 0)
                        logstr("lv: exec fallo\n");
                    sys_exit(2);
                }
            }
        }
        return 0;
    }
    if (msg == WM_CLOSE)
        PostQuitMessage(0);
    return DefWindowProcA(hwnd, msg, wp, lp);
}

int main(void)
{
    WNDCLASS wc;
    HWND hw;
    MSG msg;
    LVCOLUMNA col;
    LVITEMA it;
    char fname[16];
    int idx, ncol = 0, nitem = 0;

    logstr("lv: start\n");
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
    wc.lpszClassName = "LvTest";
    RegisterClassA(&wc);

    hw = CreateWindowExA(0, "LvTest", "Lista de archivos", 0,
                         40, 40, 560, 400, 0, 0, 0, 0);
    if (!hw) { logstr("lv: create fallo\n"); return 1; }
    hList = CreateWindowExA(0, "SysListView32", "", 0,
                            4, 4, 540, 340, hw, 0, 0, 0);
    if (!hList) { logstr("lv: listview fallo\n"); return 1; }

    col.mask = LVCF_TEXT | LVCF_WIDTH;
    col.fmt = 0;
    col.cx = 320;
    col.pszText = "Nombre";
    SendMessageA(hList, LVM_INSERTCOLUMNA, 0, (LPARAM)&col);
    col.cx = 80;
    col.pszText = "Tam";
    SendMessageA(hList, LVM_INSERTCOLUMNA, 1, (LPARAM)&col);
    ncol = 2;

    it.mask = LVIF_TEXT;
    it.iSubItem = 0;
    for (idx = 0; idx < 64; idx++) {
        if (sys_dlistdir((uint32_t)idx, fname) != 0)
            break;
        it.iItem = nitem;
        it.pszText = fname;
        SendMessageA(hList, LVM_INSERTITEMA, (WPARAM)nitem,
                     (LPARAM)&it);
        nitem++;
    }
    logstr("lv: items=");
    lognum(nitem);
    logstr(" cols=");
    lognum(ncol);
    logstr("\n");

    ShowWindow(hw, 1);

    while (GetMessageA(&msg, 0, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    logstr("lv: fin\n");
    return 0;
}