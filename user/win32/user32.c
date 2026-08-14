/* MyOS - user/win32/user32.c
 * user32.dll: modulo Win32 fijo (ring 3) en 0xB0100000.
 * Fase 12: GUI minimo sobre el LFB VBE (800x600x32, mapeado por el
 * kernel en 0xA8000000 y consultable por SYS_GFXINFO).
 * MessageBoxA: dibuja una ventana (marco, titulo, texto, boton OK) en
 * el framebuffer y espera Enter (SYS_READ) para devolver IDOK. */

#include <stdint.h>

#define SYS_WRITE   7
#define SYS_MALLOC  10
#define SYS_FREE    11
#define SYS_GFXINFO 15
#define SYS_MOUSEINFO 16
#define SYS_EVENT   17
#define SYS_WINCREATE 18
#define SYS_WINCLOSE 19
#define SYS_WINMOVE 20
#define SYS_WINUPDATE 21
#define SYS_WININFO 22
#define SYS_WINTITLE 23
#define SYS_EXEBASE 24
#define SYS_MENUBAR 25

#define EV_MOVE         1
#define EV_BUTTON_DOWN  2
#define EV_BUTTON_UP    3
#define EV_KEY          4
#define EV_WINCLOSE     5

#define CW_USEDEFAULT   (int)0x80000000u
#define WM_MENU_H       20   /* barra de menu (Fase D) */

/* WM_* minimo para el bucle de mensajes */
#define WM_CREATE       0x0001
#define WM_DESTROY      0x0002
#define WM_CLOSE        0x0010
#define WM_QUIT         0x0012
#define WM_KEYDOWN      0x0100
#define WM_KEYUP        0x0101
#define WM_CHAR         0x0102
#define WM_MOUSEMOVE    0x0200
#define WM_LBUTTONDOWN  0x0201
#define WM_LBUTTONUP    0x0202
#define WM_RBUTTONDOWN  0x0204
#define WM_RBUTTONUP    0x0205
#define WM_MBUTTONDOWN  0x0207
#define WM_MBUTTONUP    0x0208

#define MK_LBUTTON      0x0001
#define MK_RBUTTON      0x0002
#define MK_MBUTTON      0x0010

#define WM_COMMAND      0x0111
#define WM_SETTEXT      0x000C
#define WM_GETTEXT      0x000D
#define WM_PAINT        0x000F
#define WM_ERASEBKGND   0x0014
#define WM_FRAME        2           /* igual que kernel/winmgr.h */
#define WM_TITLE_H      20

#define RT_MENU         4
#define RT_ACCELERATOR  9

/* DC de gdi32 (GetDC/ReleaseDC de user32 crean el DC; gdi32 dibuja). */
#include "gdi_dc.h"

/* --- syscalls --- */

static int sys_write(const char *s, uint32_t len)
{
    int r;
    __asm__ volatile("int $0x80"
                     : "=a"(r)
                     : "a"(SYS_WRITE), "b"(s), "c"(len)
                     : "memory");
    return r;
}

static int sys_gfxinfo(uint32_t *info)
{
    int r;
    __asm__ volatile("int $0x80"
                     : "=a"(r)
                     : "a"(SYS_GFXINFO), "b"(info)
                     : "memory");
    return r;
}

static int sys_mouseinfo(uint32_t *mi)
{
    int r;
    __asm__ volatile("int $0x80"
                     : "=a"(r)
                     : "a"(SYS_MOUSEINFO), "b"(mi)
                     : "memory");
    return r;
}

static int sys_event(uint32_t *ev)
{
    int r;
    __asm__ volatile("int $0x80"
                     : "=a"(r)
                     : "a"(SYS_EVENT), "b"(ev)
                     : "memory");
    return r;
}

static int sys_wincreate(uint32_t *req)
{
    int r;
    __asm__ volatile("int $0x80"
                     : "=a"(r)
                     : "a"(SYS_WINCREATE), "b"(req)
                     : "memory");
    return r;
}

static int sys_winclose(uint32_t id)
{
    int r;
    __asm__ volatile("int $0x80"
                     : "=a"(r)
                     : "a"(SYS_WINCLOSE), "b"(id)
                     : "memory");
    return r;
}

static int sys_winupdate(uint32_t id)
{
    int r;
    __asm__ volatile("int $0x80"
                     : "=a"(r)
                     : "a"(SYS_WINUPDATE), "b"(id)
                     : "memory");
    return r;
}

/* Fase 20-D: blit por regiones. ecx = rect {x,y,w,h} (coords cliente). */
static int sys_winupdate_rect(uint32_t id, const int32_t *rect)
{
    int r;
    __asm__ volatile("int $0x80"
                     : "=a"(r)
                     : "a"(SYS_WINUPDATE), "b"(id), "c"(rect)
                     : "memory");
    return r;
}

static int sys_wintitle(uint32_t id, const char *title)
{
    int r;
    __asm__ volatile("int $0x80"
                     : "=a"(r)
                     : "a"(SYS_WINTITLE), "b"(id), "c"(title)
                     : "memory");
    return r;
}

static int sys_menubar(uint32_t id, uint32_t on, const char *flat)
{
    int r;
    __asm__ volatile("int $0x80"
                     : "=a"(r)
                     : "a"(SYS_MENUBAR), "b"(id), "c"(on), "d"(flat)
                     : "memory");
    return r;
}

static int sys_wininfo(uint32_t id, uint32_t *out)
{
    int r;
    __asm__ volatile("int $0x80"
                     : "=a"(r)
                     : "a"(SYS_WININFO), "b"(id), "c"(out)
                     : "memory");
    return r;
}

static uint32_t sys_malloc(uint32_t bytes)
{
    uint32_t r;
    __asm__ volatile("int $0x80"
                     : "=a"(r)
                     : "a"(SYS_MALLOC), "b"(bytes)
                     : "memory");
    return r;
}

static void sys_free(uint32_t p)
{
    uint32_t r;
    __asm__ volatile("int $0x80"
                     : "=a"(r)
                     : "a"(SYS_FREE), "b"(p)
                     : "memory");
}

/* Base ImageBase del .exe en curso (SYS_EXEBASE 24; 0 si no hay PE).
 * user32 vive en la misma PD que el ejecutable, asi que los recursos
 * del .exe se leen directamente en esa direccion. */
static uint32_t sys_exebase(void)
{
    uint32_t r;
    __asm__ volatile("int $0x80"
                     : "=a"(r)
                     : "a"(SYS_EXEBASE)
                     : "memory");
    return r;
}

/* --- texto a consola (fallback y debug) --- */

static uint32_t strlen32(const char *s)
{
    uint32_t n = 0;
    while (s[n])
        n++;
    return n;
}

static void console_print(const char *s)
{
    sys_write(s, strlen32(s));
}

/* --- fuente 8x16 (glifos ASCII 32..126) --- */

#include "font8x16.h"

/* --- LFB: 32 bpp, formato 0x00RRGGBB --- */

#define COLOR_BG     0x00202040u   /* azul oscuro */
#define COLOR_FRAME  0x00C0C0C0u
#define COLOR_TITLE  0x00000088u   /* barra de titulo azul */
#define COLOR_TEXT   0x00FFFFFFu
#define COLOR_BTN    0x00888888u
#define COLOR_BTN_TX 0x00000000u

static uint32_t *lfb;
static uint32_t scr_w, scr_h;

/* El LFB (VBE 32bpp) espera el byte bajo = R (BGRx8888 en memoria);
 * los colores logicos 0x00RRGGBB se swapean al escribir. */
static inline uint32_t px_disp(uint32_t c)
{
    return ((c & 0x0000FFFFu) << 8) |
           ((c >> 16) & 0x000000FFu) |
           (c & 0xFF000000u);
}

static void putpixel(int x, int y, uint32_t c)
{
    if (x < 0 || y < 0 || x >= (int)scr_w || y >= (int)scr_h)
        return;
    lfb[y * scr_w + x] = px_disp(c);
}

static void fillrect(int x, int y, int w, int h, uint32_t c)
{
    int i, j;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            putpixel(x + i, y + j, c);
}

static void drawchar(int x, int y, char c, uint32_t fg)
{
    const unsigned char *g;
    int i, j;

    if (c < 32 || c > 126)
        c = '?';
    g = font8x16_basic[c - 32];
    for (j = 0; j < 16; j++)
        for (i = 0; i < 8; i++)
            if (g[j] & (0x80u >> i))
                putpixel(x + i, y + j, fg);
}

static void drawtext(int x, int y, const char *s, uint32_t fg)
{
    while (*s) {
        drawchar(x, y, *s, fg);
        x += 8;
        s++;
    }
}

/* --- Widgets (Fase 15) ---
 * Mini-API de widgets para el escritorio (Fase 17). Un boton es un rect
 * con label y dos pares de colores (normal / presionado); el estado se
 * actualiza con los eventos de SYS_EVENT y se repinta solo en las
 * transiciones (hover -> press -> click). No se expande CreateWindowEx
 * ni GDI de Windows: es API propia, exportada como MyOS_*. */

typedef struct {
    int      x, y, w, h;        /* rect del boton */
    const char *label;
    uint32_t fg, bg;            /* colores normal */
    uint32_t fg_p, bg_p;        /* colores presionado (invertidos) */
    int      hovered, pressed;
} user32_button_t;

static int point_in_rect(int px, int py, int x, int y, int w, int h)
{
    return px >= x && px < x + w && py >= y && py < y + h;
}

/* Dibuja el boton segun su estado (hover: fondo mas claro; press: colores
 * invertidos). El label se centra con la fuente 8x16. */
static void user32_draw_button(user32_button_t *b)
{
    uint32_t bg, fg;
    int lw;

    if (b->pressed) {
        bg = b->bg_p;
        fg = b->fg_p;
    } else if (b->hovered) {
        bg = b->bg;
        fg = b->fg;
        bg = 0x00AAAAAAu;       /* hover: fondo mas claro */
        fg = 0x00000000u;
    } else {
        bg = b->bg;
        fg = b->fg;
    }
    fillrect(b->x, b->y, b->w, b->h, bg);
    lw = (int)strlen32(b->label) * 8;
    drawtext(b->x + (b->w - lw) / 2, b->y + (b->h - 16) / 2,
             b->label, fg);
}

/* Procesa un evento contra el boton: repinta en cada transicion de estado
 * y devuelve 1 cuando recibe un click completo (down+up dentro del rect). */
static int user32_button_feed(user32_button_t *b, uint32_t *ev)
{
    int in = point_in_rect((int)ev[1], (int)ev[2], b->x, b->y, b->w, b->h);

    if (ev[0] == EV_BUTTON_DOWN) {
        if (!in)
            return 0;
        b->pressed = 1;
        user32_draw_button(b);
        return 0;
    }
    if (ev[0] == EV_BUTTON_UP) {
        if (b->pressed) {
            b->pressed = 0;
            b->hovered = in;
            user32_draw_button(b);
            return in ? 1 : 0;  /* click completo solo si suelta dentro */
        }
        if (b->hovered != in) {
            b->hovered = in;
            user32_draw_button(b);
        }
        return 0;
    }
    if (ev[0] == EV_MOVE && !b->pressed && b->hovered != in) {
        b->hovered = in;
        user32_draw_button(b);
    }
    return 0;
}

/* --- recursos PE: LoadMenuA (Fase 18) ---
 * El kernel reubica los .exe reales y expone la base final por
 * SYS_EXEBASE; aqui se leen los recursos del .exe directamente desde
 * esa direccion (misma PD).
 * LoadMenuA camina el directorio de recursos (.rsrc) hasta
 * RT_MENU(4)/id y parsea el template MENUITEMTEMPLATE de 16 bits
 * (verificado byte a byte contra metapad.exe, menu 130):
 *   - 0x0080 = fin de popup
 *   - 0x0010 = popup: titulo UTF-16LE a continuacion
 *   - palabra < 0x0100 = (flags, id, texto): comando o separador
 *     (separador = flags 0 + id 0)
 *   - palabra >= 0x0100 = (id, texto): aparece tras un fin de popup
 *   - cada texto: UTF-16LE terminado en 0 + 1 palabra de padding
 *     (sin padding antes de 0x0080) */

#define RT_MENU         4
#define MNU_POP         1
#define MNU_CMD         2
#define MNU_SEP         3
#define MNU_END         4

typedef struct {
    uint16_t type;
    uint16_t depth;
    uint32_t id;
    uint32_t flags;
    char     text[48];
} menu_item_t;

static uint16_t rd16u(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t rd32u(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void wr32u(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

/* Camina el arbol de recursos (tipo -> id -> lang) y devuelve puntero
 * a los datos del recurso y su tamano (0 si no existe). */
static const uint8_t *find_resource(uint32_t type, uint32_t id,
                                    uint32_t *size)
{
    const uint8_t *img = (const uint8_t *)sys_exebase();
    const uint8_t *res, *dir;
    uint32_t pe_off, dir_rva, level, cnt, i;

    if (img == 0 || img[0] != 'M' || img[1] != 'Z')
        return 0;
    pe_off = rd32u(img + 0x3C);
    if (rd16u(img + pe_off + 24) != 0x10B)      /* PE32 */
        return 0;
    dir_rva = rd32u(img + pe_off + 24 + 96 + 2 * 8);  /* dir 2: recursos */
    if (dir_rva == 0)
        return 0;
    res = img + dir_rva;
    dir = res;
    for (level = 0; level < 3; level++) {
        cnt = rd16u(dir + 12) + rd16u(dir + 14);
        for (i = 0; i < cnt; i++) {
            const uint8_t *e = dir + 16 + i * 8;
            uint32_t name = rd32u(e);
            uint32_t target = rd32u(e + 4) & 0x7FFFFFFF;
            uint32_t want = (level == 0) ? type : id;
            if (level == 2) {           /* nivel lang: vale cualquiera */
                const uint8_t *d = res + target;
                *size = rd32u(d + 4);
                return img + rd32u(d);
            }
            if ((name & 0x80000000) == 0 && name == want) {
                dir = res + target;
                break;
            }
        }
        if (i == cnt)
            return 0;
    }
    return 0;
}

/* Convierte UTF-16LE a ASCII (los textos de los menus son ASCII con
 * \t). Devuelve el numero de WCHARs consumidos (incl. el 0 final). */
static int utf16_to_ascii(char *dst, const uint8_t *src, int max)
{
    int n = 0;
    while (n < max - 1) {
        uint16_t c = rd16u(src + n * 2);
        if (c == 0)
            break;
        dst[n] = (c < 0x80) ? (char)c : '?';
        n++;
    }
    dst[n] = 0;
    return n + 1;
}

#define MENU_MAX_ITEMS  256
static menu_item_t menu_items[MENU_MAX_ITEMS];
static char menu_tmp[48];
static int menu_n;

static int menu_add(uint16_t type, uint16_t depth, uint32_t id,
                    uint32_t flags, const char *text)
{
    menu_item_t *it;
    int i;

    if (menu_n >= MENU_MAX_ITEMS)
        return 0;
    it = &menu_items[menu_n++];
    it->type = type;
    it->depth = depth;
    it->id = id;
    it->flags = flags;
    it->text[0] = 0;
    if (text != 0)
        for (i = 0; i < 47 && text[i]; i++)
            it->text[i] = text[i];
    return 1;
}

/* Parsea un nivel de items. Cada item es [mtOption][mtID][texto] (UTF-16).
 * MF_POPUP (0x10) es flag del mtOption seguido del titulo del submenu.
 * MF_END (0x80) es un flag combinado en el mtOption del ULTIMO item del
 * popup (no un word separado). Devuelve el offset tras el nivel. */
static uint32_t menu_parse_level(const uint8_t *tpl, uint32_t size,
                                 uint32_t p, int depth)
{
    while (p + 2 <= size) {
        uint32_t opt = rd16u(tpl + p);
        uint32_t ends = opt & 0x0080;

        if (opt & 0x0010) {                 /* popup: titulo */
            int len = utf16_to_ascii(menu_tmp, tpl + p + 2,
                                     (int)sizeof(menu_tmp));
            menu_add(MNU_POP, (uint16_t)depth, 0, 0, menu_tmp);
            p = menu_parse_level(tpl, size, p + 2 + (uint32_t)len * 2,
                                 depth + 1);
        } else {                            /* item o separador */
            uint32_t id = rd16u(tpl + p + 2);
            int len = utf16_to_ascii(menu_tmp, tpl + p + 4,
                                     (int)sizeof(menu_tmp));
            if (id == 0 && menu_tmp[0] == 0)
                menu_add(MNU_SEP, (uint16_t)depth, 0, 0, 0);
            else
                menu_add(MNU_CMD, (uint16_t)depth, id, opt, menu_tmp);
            p += 4 + (uint32_t)len * 2;
        }
        if (ends)
            return p;                       /* cierra este popup */
    }
    return p;
}

static void menu_parse(const uint8_t *tpl, uint32_t size)
{
    menu_n = 0;
    menu_parse_level(tpl, size, 4, 0);   /* salta wVersion + wOffset */
}

/* Convierte entero a decimal en buf (max 10 digitos). */
static char *itoa32(char *buf, uint32_t v)
{
    char tmp[12];
    int n = 0;
    do {
        tmp[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    for (int i = 0; i < n; i++)
        buf[i] = tmp[n - 1 - i];
    buf[n] = 0;
    return buf;
}

typedef struct {
    uint32_t magic;     /* 'MNUP' */
    uint32_t count;
    menu_item_t items[MENU_MAX_ITEMS];
} menu_t;

static menu_t loaded_menu;

/* Carga un menu de los recursos del ejecutable (RT_MENU). lpMenuName
 * puede ser un MAKEINTRESOURCE (id en el low word) o un nombre (no
 * soportado aqui). Devuelve un HMENU opaco (puntero a la copia). */
uint32_t __attribute__((stdcall)) LoadMenuA(uint32_t hinst, const char *name)
{
    const uint8_t *tpl;
    uint32_t size = 0, id, i, n;
    char line[80], num[12];
    int d;

    (void)hinst;
    if (name == 0)
        return 0;
    id = (((uint32_t)(uintptr_t)name >> 16) == 0)
             ? (uint32_t)(uintptr_t)name
             : RT_MENU;
    tpl = find_resource(RT_MENU, id, &size);
    if (tpl == 0 || size == 0) {
        console_print("[user32] LoadMenuA: recurso de menu no encontrado\n");
        return 0;
    }
    menu_parse(tpl, size);
    loaded_menu.magic = 0x4D4E5550u;    /* 'MNUP' */
    loaded_menu.count = (uint32_t)menu_n;
    for (i = 0; i < (uint32_t)menu_n; i++)
        loaded_menu.items[i] = menu_items[i];

    /* dump del arbol a consola (diagnostico de Fase 18) */
    n = loaded_menu.count;
    console_print("[user32] LoadMenuA: menu ");
    console_print(itoa32(num, id));
    console_print(", ");
    console_print(itoa32(num, n));
    console_print(" items\n");
    for (i = 0; i < n; i++) {
        menu_item_t *it = &loaded_menu.items[i];
        line[0] = 0;
        for (d = 0; d < (int)it->depth && d < 30; d++)
            line[d] = ' ';
        {
            int j = 0;
            while (it->text[j] && d + j < 45) {
                if (it->text[j] == '\t')
                    line[d + j] = ' ';
                else
                    line[d + j] = it->text[j];
                j++;
            }
            d += j;
        }
        line[d++] = ' ';
        if (it->type == MNU_POP) {
            line[d++] = '>';
            line[d++] = '>';
            line[d] = 0;
        } else if (it->type == MNU_END) {
            line[d++] = '[';
            line[d++] = 'E';
            line[d++] = 'N';
            line[d++] = 'D';
            line[d++] = ']';
            line[d] = 0;
        } else if (it->type == MNU_SEP) {
            line[d++] = '-';
            line[d] = 0;
        } else {
            line[d++] = '#';
            line[d] = 0;
        }
        console_print(line);
        if (it->type == MNU_CMD) {
            console_print(" id=");
            console_print(itoa32(num, it->id));
        }
        console_print("\n");
    }
    return (uint32_t)&loaded_menu;
}

/* --- aceleradores (RT_ACCELERATOR) --- */

#define ACCEL_MAX 64
static uint16_t accel_key[ACCEL_MAX], accel_id[ACCEL_MAX];
static uint8_t  accel_flags[ACCEL_MAX];
static int accel_count;

static uint32_t *wnd_proc_fwd;      /* ref a wnd_proc[] (definida abajo) */

static void msgq_push(uint32_t hwnd, uint32_t m, uint32_t wp, uint32_t lp);

uint32_t __attribute__((stdcall)) LoadAcceleratorsA(uint32_t hinst, uint32_t name)
{
    const uint8_t *acc;
    uint32_t size = 0, i, n;
    char num[16];

    (void)hinst;
    if (name == 0 || name > 0xFFFF)
        return 0;
    acc = find_resource(RT_ACCELERATOR, name, &size);
    if (acc == 0 || size < 6)
        return 0;
    n = size / 6;
    if (n > ACCEL_MAX)
        n = ACCEL_MAX;
    for (i = 0; i < n; i++) {
        accel_flags[i] = (uint8_t)rd16u(acc + i * 6);   /* fFlags */
        accel_key[i] = rd16u(acc + i * 6 + 2);          /* wAnsi */
        accel_id[i] = rd16u(acc + i * 6 + 4);           /* wId */
    }
    accel_count = (int)n;
    console_print("[user32] LoadAcceleratorsA id=");
    console_print(itoa32(num, name));
    console_print(" n=");
    console_print(itoa32(num, n));
    console_print("\n");
    return 1;
}

uint32_t __attribute__((stdcall)) TranslateAcceleratorA(uint32_t hwnd, uint32_t haccel, uint32_t m)
{
    const uint8_t *m8 = (const uint8_t *)(uint32_t)m;
    uint32_t msg = rd32u(m8 + 4);
    uint32_t key = rd32u(m8 + 8);
    uint32_t buttons = rd32u(m8 + 12);      /* lParam = modifiers EV_KEY */
    int i;

    (void)hwnd; (void)haccel;
    if (msg != WM_KEYDOWN || accel_count == 0)
        return 0;
    for (i = 0; i < accel_count; i++) {
        uint8_t fl = accel_flags[i];
        uint16_t k = accel_key[i];
        /* comparacion sin distinguir mayusculas */
        if (k >= 'a' && k <= 'z')
            k = (uint16_t)(k - 32);
        {
            uint32_t kk = key;
            if (kk >= 'a' && kk <= 'z')
                kk -= 32;
            if (kk != k)
                continue;
        }
        /* modificadores: FCONTROL=2, FSHIFT=4, FALT=16 (bits en ev[3]) */
        if ((fl & 2) && !(buttons & 1))
            continue;
        if ((fl & 4) && !(buttons & 4))
            continue;
        if ((fl & 16) && !(buttons & 2))
            continue;
        /* El WM_COMMAND va a la ventana principal (primer wndproc),
         * no al hwnd del WM_KEYDOWN (que puede ser el control edit). */
        msgq_push(wnd_proc_fwd[1] ? 1 : rd32u(m8), WM_COMMAND,
                  accel_id[i], 0);
        return 1;
    }
    return 0;
}

/* --- stubs USER32 (Fase 18) ---
 * metapad.exe importa 87 funciones de USER32; de momento solo
 * MessageBoxA/LoadMenuA estan implementadas. El resto devuelve 0
 * (o un valor inocuo) para que la resolucion de imports no falle y
 * el programa pueda llegar hasta LoadMenuA. Se implementaran de una
 * en una segun lo exija la escalera. */

uint32_t __attribute__((stdcall)) CharLowerA(uint32_t s) { (void)s; return 0; }
uint32_t __attribute__((stdcall)) CharLowerBuffA(uint32_t s, uint32_t n) { (void)s; (void)n; return 0; }
uint32_t __attribute__((stdcall)) CharUpperBuffA(uint32_t s, uint32_t n) { (void)s; (void)n; return 0; }
uint32_t __attribute__((stdcall)) CharToOemBuffA(uint32_t s, uint32_t d, uint32_t n) { (void)s; (void)d; (void)n; return 0; }
uint32_t __attribute__((stdcall)) OemToCharBuffA(uint32_t s, uint32_t d, uint32_t n) { (void)s; (void)d; (void)n; return 0; }
uint32_t __attribute__((stdcall)) IsCharAlphaA(uint32_t c) { (void)c; return 0; }
uint32_t __attribute__((stdcall)) IsCharAlphaNumericA(uint32_t c) { (void)c; return 0; }
uint32_t __attribute__((stdcall)) IsCharLowerA(uint32_t c) { (void)c; return 0; }
uint32_t __attribute__((stdcall)) IsCharUpperA(uint32_t c) { (void)c; return 0; }
/* --- minimo WM (ventanas + bucle de mensajes) sobre el WM del kernel ---
 * Fase 18, slice 2: RegisterClassA/CreateWindowExA -> SYS_WINCREATE,
 * eventos SYS_EVENT traducidos a WM_* y dispatch al wndproc del .exe. */

#define WNDCLASS_LPFN  4   /* lpfnWndProc */
#define WNDCLASS_HINST 16  /* hInstance */
#define WNDCLASS_NAME  36  /* lpszClassName */
#define MAX_CLASSES    16
#define MAX_WNDPROCS   64

static uint32_t class_names[MAX_CLASSES];
static uint32_t class_procs[MAX_CLASSES];
static uint32_t class_count;

static uint32_t wnd_proc[MAX_WNDPROCS];
static uint32_t *wnd_proc_fwd = wnd_proc;
uint32_t __attribute__((stdcall)) DefWindowProcA(uint32_t hwnd, uint32_t m, uint32_t a, uint32_t b);

/* --- estado por ventana (slice 2 del Hito B) ---
 * wnd_buf = buffer del cliente (formato LFB, px_disp) que el kernel
 * blitea con SYS_WINUPDATE; wnd_cw/wnd_ch = tamano del cliente. Los
 * hijos virtuales (clases built-in: RichEdit20A/EDIT/...) no crean
 * ventana en el kernel: viven en user32 y dibujan en el buffer del
 * padre. */

#define CHILD_BASE     32          /* hwnd de los hijos: 32..63 */
#define CHILD_MAX      (MAX_WNDPROCS - CHILD_BASE)
#define CHILD_TXTLEN   1024

static uint32_t wnd_buf[MAX_WNDPROCS];
static int      wnd_cw[MAX_WNDPROCS], wnd_ch[MAX_WNDPROCS];

static uint32_t child_parent[CHILD_MAX];
static int      child_x[CHILD_MAX], child_y[CHILD_MAX];
static int      child_w[CHILD_MAX], child_h[CHILD_MAX];
static char     child_text[CHILD_MAX][CHILD_TXTLEN];
static int      child_cur[CHILD_MAX];   /* caret (indice en child_text)  */
static int      child_edit[CHILD_MAX];  /* 1 = control editable (EDIT/RichEdit) */
static uint32_t focus_edit;             /* control de edicion enfocado  */
static uint32_t wm_focus_win = 1;       /* Fase 23-B6: foco por ventana */

/* DCs de gdi32: uno por hwnd (pool estatico). */
static myos_dc_t dc_pool[MAX_WNDPROCS];

/* stock GDI (wingdi.h): handle = indice + 1 */
#define STOCK_HANDLE(i) ((uint32_t)(i) + 1)
#define WHITE_BRUSH     0
#define BLACK_PEN       7

/* --- clases built-in (sin RegisterClass del .exe) --- */

static uint32_t builtin_wndproc(uint32_t hwnd, uint32_t m, uint32_t a,
                                uint32_t b);
static void child_paint(uint32_t i);    /* pinta un hijo en el padre */
static void child_repaint(uint32_t i);  /* repinta hijo + sys_winupdate */

typedef struct {
    const char *name;
    uint32_t    fn;         /* builtin_wndproc */
} builtin_class_t;

static const builtin_class_t builtin_classes[] = {
    { "RichEdit20A", (uint32_t)&builtin_wndproc },
    { "EDIT",        (uint32_t)&builtin_wndproc },
    { "STATIC",      (uint32_t)&builtin_wndproc },
    { "BUTTON",      (uint32_t)&builtin_wndproc },
    { "msctls_statusbar32", (uint32_t)&builtin_wndproc },
    { 0, 0 },
};

static int is_child(uint32_t hwnd)
{
    return hwnd >= CHILD_BASE && hwnd < CHILD_BASE + CHILD_MAX &&
           child_parent[hwnd - CHILD_BASE] != 0;
}

/* Rect del hijo en el cliente del padre (si es hijo). */
static void child_rect(uint32_t hwnd, int *x, int *y, int *w, int *h)
{
    uint32_t i = hwnd - CHILD_BASE;
    *x = child_x[i];
    *y = child_y[i];
    *w = child_w[i];
    *h = child_h[i];
}
#define MSGQ_MAX 64
static uint32_t msgq_hwnd[MSGQ_MAX], msgq_msg[MSGQ_MAX];
static uint32_t msgq_wp[MSGQ_MAX], msgq_lp[MSGQ_MAX];
static int msgq_head, msgq_tail;
static int quit_pending;

static uint32_t str_valid(const char *s)
{    uint32_t i;
    if (s == 0)
        return 0;
    for (i = 0; i < 512; i++) {
        char c = s[i];
        if (c == 0)
            return 1;
    }
    return 0;
}

static int ci_eq(const char *a, const char *b)
{
    for (;;) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z')
            ca += 32;
        if (cb >= 'A' && cb <= 'Z')
            cb += 32;
        if (ca != cb)
            return 0;
        if (ca == 0)
            return 1;
        a++;
        b++;
    }
}

static int class_find(uint32_t name)
{
    int i;
    for (i = 0; i < (int)class_count; i++)
        if (class_names[i] == name)
            return i;
    return -1;
}

static void msgq_push(uint32_t hwnd, uint32_t m, uint32_t wp, uint32_t lp)
{
    int n = (msgq_tail + 1) % MSGQ_MAX;
    if (n == msgq_head)
        return;                 /* cola llena: se descarta */
    msgq_hwnd[msgq_tail] = hwnd;
    msgq_msg[msgq_tail] = m;
    msgq_wp[msgq_tail] = wp;
    msgq_lp[msgq_tail] = lp;
    msgq_tail = n;
}

static int msgq_pop(uint32_t *out)
{
    if (msgq_head == msgq_tail)
        return 0;
    out[0] = msgq_hwnd[msgq_head];
    out[1] = msgq_msg[msgq_head];
    out[2] = msgq_wp[msgq_head];
    out[3] = msgq_lp[msgq_head];
    msgq_head = (msgq_head + 1) % MSGQ_MAX;
    return 1;
}

/* Traduce un evento crudo del kernel (SYS_EVENT) a mensajes WM_* y los
 * encola. Un EV_KEY produce WM_KEYDOWN + WM_CHAR (si es imprimible). */
static uint32_t menu_win_hwnd;      /* fwd: definido en la seccion menu */
static int menu_bar_top_at(uint32_t hwnd, int x);
static int menu_bar_top_letter(uint32_t hwnd, char c);
static uint32_t menu_modal(uint32_t hwnd, int top, int x);

static void event_to_wm(const uint32_t *ev); /* fwd */
static uint32_t wm_hit_hwnd(int mx, int my)
{
    uint32_t info[8];
    int i;
    for (i = MAX_WNDPROCS - 1; i >= 1; i--) {
        if (!wnd_proc[i] || is_child((uint32_t)i))
            continue;
        if (sys_wininfo((uint32_t)i, info) != 0)
            continue;
        if (mx >= (int)info[0] && mx < (int)info[0] + (int)info[2] &&
            my >= (int)info[1] && my < (int)info[1] + (int)info[3])
            return (uint32_t)i;
    }
    return 0;
}
static void event_to_wm(const uint32_t *ev)
{
    uint32_t hwnd = 1, buttons = ev[3], key = ev[4];
    int i;
    for (i = 1; i < MAX_WNDPROCS; i++)
        if (wnd_proc[i]) {
            hwnd = i;
            break;
        }

    /* Fase 23-B6: routing por ventana. El kernel entrega los eventos al
     * proceso, no a la ventana; aqui se hace hit-testing (ventana bajo
     * el raton, topmost primero) para el raton y se usa el foco
     * (ultima ventana clicada) para el teclado. */
    {
        uint32_t hit = wm_hit_hwnd((int)ev[1], (int)ev[2]);
        if (ev[0] == EV_BUTTON_DOWN && hit)
            wm_focus_win = hit;
        if (hit)
            hwnd = hit;
        else if (ev[0] == EV_KEY || ev[0] == EV_MOVE || ev[0] == EV_WINCLOSE) {
            if (wm_focus_win && wm_focus_win < MAX_WNDPROCS &&
                wnd_proc[wm_focus_win])
                hwnd = wm_focus_win;
        }
    }

    switch (ev[0]) {
    case EV_MOVE:
        msgq_push(hwnd, WM_MOUSEMOVE, buttons, (ev[2] << 16) | ev[1]);
        break;
    case EV_BUTTON_DOWN: {
        /* Fase D: clic en la barra de menu de la ventana principal ->
         * abre el desplegable (modal); no se encola WM_LBUTTONDOWN. */
        uint32_t info[8];
        if (menu_win_hwnd && wnd_proc[menu_win_hwnd] &&
            sys_wininfo(menu_win_hwnd, info) == 0 &&
            (int)ev[2] >= (int)info[1] + WM_TITLE_H + WM_FRAME &&
            (int)ev[2] < (int)info[1] + WM_TITLE_H + WM_MENU_H +
                         WM_FRAME) {
            int t = menu_bar_top_at(menu_win_hwnd, (int)ev[1]);
            if (t >= 0) {
                menu_modal(menu_win_hwnd, t, (int)ev[1]);
                break;
            }
        }
        if (buttons & 1)
            msgq_push(hwnd, WM_LBUTTONDOWN, 1, (ev[2] << 16) | ev[1]);
        else if (buttons & 2)
            msgq_push(hwnd, WM_RBUTTONDOWN, 1, (ev[2] << 16) | ev[1]);
        else
            msgq_push(hwnd, WM_MBUTTONDOWN, 1, (ev[2] << 16) | ev[1]);
        break;
    }
    case EV_BUTTON_UP:
        if (buttons & 1)
            msgq_push(hwnd, WM_LBUTTONUP, 0, (ev[2] << 16) | ev[1]);
        else if (buttons & 2)
            msgq_push(hwnd, WM_RBUTTONUP, 0, (ev[2] << 16) | ev[1]);
        else
            msgq_push(hwnd, WM_MBUTTONUP, 0, (ev[2] << 16) | ev[1]);
        break;
    case EV_KEY: {
        /* Fase D: Alt+mnemonico de un top-level abre su desplegable. */
        if ((ev[3] & 2) && key >= 32 && key <= 126 &&
            menu_win_hwnd && wnd_proc[menu_win_hwnd]) {
            int t = menu_bar_top_letter(menu_win_hwnd, (char)key);
            if (t >= 0) {
                menu_modal(menu_win_hwnd, t, 0);
                break;
            }
        }
        /* Las teclas van al control de edicion enfocado (si hay), no al
         * primer wndproc: asi el WM_CHAR llega al RichEdit/EDIT. */
        uint32_t target = hwnd;
        if (focus_edit && focus_edit < MAX_WNDPROCS &&
            wnd_proc[focus_edit])
            target = focus_edit;
        msgq_push(target, WM_KEYDOWN, key, ev[3]);
        /* WM_CHAR solo sin Ctrl: Ctrl+letra es comando (acelerador) */
        if (key >= 32 && key <= 126 && !(ev[3] & 1))
            msgq_push(target, WM_CHAR, key, ev[3]);
        break;
    }
    case EV_WINCLOSE:
        msgq_push(hwnd, WM_CLOSE, 0, 0);
        break;
    default:
        break;
    }
}




uint32_t __attribute__((stdcall)) RegisterClassA(uint32_t wc)
{
    uint32_t name, proc;
    char num[16];
    console_print("[user32] RegisterClassA wc=");
    console_print(itoa32(num, wc));
    console_print("\n");
    if (wc == 0 || class_count >= MAX_CLASSES)
        return 0;
    name = rd32u((const uint8_t *)(uint32_t)wc + WNDCLASS_NAME);
    proc = rd32u((const uint8_t *)(uint32_t)wc + WNDCLASS_LPFN);
    console_print("[user32]   name=");
    console_print(itoa32(num, name));
    console_print(" proc=");
    console_print(itoa32(num, proc));
    console_print("\n");
    if (proc == 0 || !str_valid((const char *)name))
        return 0;
    class_names[class_count] = name;
    class_procs[class_count] = proc;
    class_count++;
    console_print("[user32] RegisterClassA ok\n");
    return 1;
}

uint32_t __attribute__((stdcall)) CreateWindowExA(uint32_t e, uint32_t cls, uint32_t name,
                         uint32_t style, int x, int y, int w, int h,
                         uint32_t parent, uint32_t menu, uint32_t inst,
                         uint32_t param)
{
    uint32_t req[8], id;
    char num[16];
    int ci, i;
    uint32_t proc;
    (void)e; (void)menu; (void)inst; (void)param;

    /* Clase built-in (RichEdit20A/EDIT/STATIC/BUTTON/msctls_statusbar32):
     * no requiere RegisterClass del .exe. */
    ci = class_find(cls);
    if (ci >= 0) {
        proc = class_procs[ci];
    } else {
        proc = 0;
        for (i = 0; builtin_classes[i].name; i++)
            if (ci_eq((const char *)cls, builtin_classes[i].name)) {
                proc = builtin_classes[i].fn;
                break;
            }
        if (proc == 0)
            return 0;
    }
    if (x == CW_USEDEFAULT || y == CW_USEDEFAULT) {
        x = 80;
        y = 40;
    }
    if (w == CW_USEDEFAULT || w <= 0)
        w = 600;
    if (h == CW_USEDEFAULT || h <= 0)
        h = 400;
    if (!str_valid((const char *)name))
        name = (uint32_t)"MyOS";

    if (parent != 0) {
        /* Hijo virtual: sin ventana en el kernel; dibuja en el buffer
         * del padre (el kernel solo blitea top-levels). */
        for (i = 0; i < CHILD_MAX; i++)            if (child_parent[i] == 0)
                break;
        if (i >= CHILD_MAX)
            return 0;
        id = CHILD_BASE + (uint32_t)i;
        child_parent[i] = parent;
        child_x[i] = x;
        child_y[i] = y;
        child_w[i] = w;
        child_h[i] = h;
        child_text[i][0] = 0;
        child_cur[i] = 0;
        child_edit[i] = ci_eq((const char *)cls, "EDIT") ||
                        ci_eq((const char *)cls, "RichEdit20A") ? 1 : 0;
        if (child_edit[i])
            focus_edit = id;        /* primer editor: foco inicial */
        wnd_proc[id] = proc;
        wnd_buf[id] = 0;
        wnd_cw[id] = w;
        wnd_ch[id] = h;
        return id;
    }

    /* Top-level: buffer del cliente + ventana del kernel. */
    w = w > 800 ? 800 : w;
    h = h > 600 ? 600 : h;
    {
        int cw = w - 2 * WM_FRAME;          /* marco 2+2 */
        int ch = h - WM_TITLE_H - WM_FRAME; /* titulo 20 + marco 2 */
        uint32_t buf = 0;
        if (cw > 0 && ch > 0)
            buf = sys_malloc((uint32_t)cw * (uint32_t)ch * 4);
        req[0] = name;
        req[1] = (uint32_t)x;
        req[2] = (uint32_t)y;
        req[3] = (uint32_t)w;
        req[4] = (uint32_t)h;
        req[5] = buf;
        req[6] = (uint32_t)cw * (uint32_t)ch * 4;
        req[7] = 0;
        (void)style;
        id = (uint32_t)sys_wincreate(req);
        if (id == 0 || id >= MAX_WNDPROCS) {
            if (buf)
                sys_free(buf);
            return 0;
        }
        wnd_proc[id] = proc;
        wnd_buf[id] = buf;
        wnd_cw[id] = cw;
        wnd_ch[id] = ch;
        console_print("[user32] CreateWindowExA id=");
        console_print(itoa32(num, id));
        console_print(" buf=");
        console_print(itoa32(num, buf));
        console_print(" cw=");
        console_print(itoa32(num, (uint32_t)cw));
        console_print(" ch=");
        console_print(itoa32(num, (uint32_t)ch));
        console_print("\n");
    }

    /* WM_CREATE sincrono, como Windows: el .exe monta sus hijos
     * (RichEdit, statusbar...) aqui. */
    ((uint32_t(*)(uint32_t, uint32_t, uint32_t, uint32_t))proc)
        (id, WM_CREATE, 0, 0);
    return id;
}

/* Pinta la ventana: borra el fondo del cliente (blanco) y llama al
 * wndproc con WM_PAINT; para top-levels ademas recompone el kernel
 * (SYS_WINUPDATE). Hijos: dibujan en el buffer del padre. */
static void wm_paint_window(uint32_t hwnd)
{
    uint32_t p, buf;
    uint32_t cw, ch;
    int i;

    if (hwnd >= MAX_WNDPROCS || wnd_proc[hwnd] == 0)
        return;
    p = wnd_proc[hwnd];
    buf = wnd_buf[hwnd];
    cw = (uint32_t)wnd_cw[hwnd];
    ch = (uint32_t)wnd_ch[hwnd];

    if (is_child(hwnd)) {
        /* el hijo pinta en el buffer del padre; su DC lleva el origen */
        uint32_t p2 = child_parent[hwnd - CHILD_BASE];
        buf = wnd_buf[p2];
        cw = (uint32_t)wnd_cw[p2];
        ch = (uint32_t)wnd_ch[p2];
    }
    if (buf && cw > 0 && ch > 0) {
        /* borrado del fondo del cliente (blanco por defecto) */
        uint32_t *px = (uint32_t *)buf;
        uint32_t n = cw * ch;
        uint32_t i;
        for (i = 0; i < n; i++)
            px[i] = px_disp(0x00FFFFFFu);
    }
    ((uint32_t(*)(uint32_t, uint32_t, uint32_t, uint32_t))p)
        (hwnd, WM_PAINT, 0, 0);
    /* repintar los hijos virtuales del padre tras su WM_PAINT: el texto
     * de los EDIT/RichEdit no debe borrarse cuando el padre repinta */
    for (i = 0; i < CHILD_MAX; i++)
        if (child_parent[i] == hwnd && wnd_proc[CHILD_BASE + i])
            child_paint((uint32_t)i);
    if (!is_child(hwnd)) {
        /* Fase 20-D: si el DC acumulo un rect sucio, blit solo esa
         * region; si no, cliente completo. */
        myos_dc_t *d = &dc_pool[hwnd];
        if (d->magic == GDI_DC_MAGIC && d->dirty_w > 0 && d->dirty_h > 0) {
            int32_t rect[4];
            rect[0] = d->dirty_x;
            rect[1] = d->dirty_y;
            rect[2] = d->dirty_w;
            rect[3] = d->dirty_h;
            d->dirty_w = 0;
            d->dirty_h = 0;
            sys_winupdate_rect(hwnd, rect);
        } else {
            sys_winupdate(hwnd);
        }
    }
}

uint32_t __attribute__((stdcall)) ShowWindow(uint32_t hwnd, int cmd)
{
    (void)cmd;
    if (hwnd < MAX_WNDPROCS && wnd_proc[hwnd])
        wm_paint_window(hwnd);
    return 1;
}

uint32_t __attribute__((stdcall)) UpdateWindow(uint32_t hwnd)
{
    if (hwnd < MAX_WNDPROCS && wnd_proc[hwnd])
        wm_paint_window(hwnd);
    return 1;
}

uint32_t __attribute__((stdcall)) DestroyWindow(uint32_t hwnd)
{
    if (hwnd < MAX_WNDPROCS) {
        if (is_child(hwnd)) {
            uint32_t i = hwnd - CHILD_BASE;
            child_parent[i] = 0;
            wnd_proc[hwnd] = 0;
            return 1;
        }
        if (wnd_buf[hwnd])
            sys_free(wnd_buf[hwnd]);
        wnd_buf[hwnd] = 0;
        wnd_proc[hwnd] = 0;
        sys_winclose(hwnd);
    }
    return 1;
}

uint32_t __attribute__((stdcall)) IsWindow(uint32_t hwnd)
{
    return (hwnd < MAX_WNDPROCS && wnd_proc[hwnd]) ? 1 : 0;
}

uint32_t __attribute__((stdcall)) GetMessageA(uint32_t msg, uint32_t hwnd, uint32_t a, uint32_t b)
{
    uint32_t ev[5];
    uint32_t out[4];
    (void)hwnd; (void)a; (void)b;
    for (;;) {
        if (quit_pending)
            return 0;
        if (msgq_pop(out)) {
            wr32u((uint8_t *)(uint32_t)msg, out[0]);
            wr32u((uint8_t *)(uint32_t)msg + 4, out[1]);
            wr32u((uint8_t *)(uint32_t)msg + 8, out[2]);
            wr32u((uint8_t *)(uint32_t)msg + 12, out[3]);
            wr32u((uint8_t *)(uint32_t)msg + 16, 0);
            wr32u((uint8_t *)(uint32_t)msg + 20, 0);
            wr32u((uint8_t *)(uint32_t)msg + 24, 0);
            return 1;
        }
        if (sys_event(ev) == 0 && ev[0] != 0)
            event_to_wm(ev);
    }
}

uint32_t __attribute__((stdcall)) PeekMessageA(uint32_t msg, uint32_t hwnd, uint32_t a, uint32_t b,
                      uint32_t rm)
{
    uint32_t ev[5];
    uint32_t out[4];
    (void)hwnd; (void)a; (void)b; (void)rm;
    if (msgq_pop(out)) {
        wr32u((uint8_t *)(uint32_t)msg, out[0]);
        wr32u((uint8_t *)(uint32_t)msg + 4, out[1]);
        wr32u((uint8_t *)(uint32_t)msg + 8, out[2]);
        wr32u((uint8_t *)(uint32_t)msg + 12, out[3]);
        return 1;
    }
    if (sys_event(ev) == 0 && ev[0] != 0) {
        event_to_wm(ev);
        if (msgq_pop(out)) {
            wr32u((uint8_t *)(uint32_t)msg, out[0]);
            wr32u((uint8_t *)(uint32_t)msg + 4, out[1]);
            wr32u((uint8_t *)(uint32_t)msg + 8, out[2]);
            wr32u((uint8_t *)(uint32_t)msg + 12, out[3]);
            return 1;
        }
    }
    return 0;
}

uint32_t __attribute__((stdcall)) TranslateMessage(uint32_t msg) { (void)msg; return 0; }

uint32_t __attribute__((stdcall)) DispatchMessageA(uint32_t msg)
{
    const uint8_t *m8 = (const uint8_t *)(uint32_t)msg;
    uint32_t hwnd = rd32u(m8);
    uint32_t m = rd32u(m8 + 4);
    uint32_t wp = rd32u(m8 + 8);
    uint32_t lp = rd32u(m8 + 12);
    uint32_t p;
    if (hwnd >= MAX_WNDPROCS)
        return 0;
    p = wnd_proc[hwnd];
    if (p == 0)
        return DefWindowProcA(hwnd, m, wp, lp);
    return ((uint32_t(*)(uint32_t, uint32_t, uint32_t, uint32_t))p)
        (hwnd, m, wp, lp);
}

uint32_t __attribute__((stdcall)) PostMessageA(uint32_t hwnd, uint32_t m, uint32_t a, uint32_t b)
{
    msgq_push(hwnd, m, a, b);
    return 1;
}

uint32_t __attribute__((stdcall)) PostQuitMessage(int code)
{
    (void)code;
    quit_pending = 1;
    msgq_push(0, WM_QUIT, code, 0);
    return 0;
}

uint32_t __attribute__((stdcall)) SendMessageA(uint32_t hwnd, uint32_t m, uint32_t a, uint32_t b)
{
    uint32_t p;
    if (hwnd >= MAX_WNDPROCS)
        return 0;
    p = wnd_proc[hwnd];
    if (p == 0)
        return DefWindowProcA(hwnd, m, a, b);
    return ((uint32_t(*)(uint32_t, uint32_t, uint32_t, uint32_t))p)
        (hwnd, m, a, b);
}

uint32_t __attribute__((stdcall)) SendDlgItemMessageA(uint32_t d, uint32_t i, uint32_t m,
                             uint32_t a, uint32_t b)
{ (void)d; (void)i; (void)m; (void)a; (void)b; return 0; }

uint32_t __attribute__((stdcall)) DefWindowProcA(uint32_t hwnd, uint32_t m, uint32_t a, uint32_t b)
{
    (void)a; (void)b;
    if (m == WM_CLOSE) {
        if (hwnd < MAX_WNDPROCS) {
            wnd_proc[hwnd] = 0;
            sys_winclose(hwnd);
        }
        PostQuitMessage(0);
        return 0;
    }
    if (m == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    if (m == WM_QUIT) {
        PostQuitMessage(0);
        return 0;
    }
    if (m == WM_PAINT || m == 0x0014 /* WM_ERASEBKGND */) {
        /* el fondo blanco ya lo deja wm_paint_window(); si la app llama
         * a DefWindowProc por su cuenta no hay que pintar nada mas */
        return m == 0x0014 ? 1 : 0;
    }
    return 0;
}

uint32_t __attribute__((stdcall)) CallWindowProcA(uint32_t p, uint32_t hwnd, uint32_t m,
                         uint32_t a, uint32_t b)
{
    (void)p; (void)hwnd; (void)m; (void)a; (void)b;
    return DefWindowProcA(hwnd, m, a, b);
}

/* --- DC de dibujo (slice 2 del Hito B) ---
 * GetDC crea un DC apuntando al buffer del cliente de la ventana (o
 * del padre + origen si es un hijo virtual). gdi32.dll dibuja dentro
 * con sus funciones (TextOutA/FillRect/...). El handle es la VA del
 * DC del pool; se libera con ReleaseDC. */

static uint32_t dc_lookup(uint32_t hwnd, myos_dc_t **dc)
{
    uint32_t buf, parent;
    int cw, ch, ox, oy;

    if (hwnd >= MAX_WNDPROCS)
        return 0;
    if (is_child(hwnd)) {
        uint32_t i = hwnd - CHILD_BASE;
        parent = child_parent[i];
        if (parent >= MAX_WNDPROCS || wnd_buf[parent] == 0)
            return 0;
        buf = wnd_buf[parent];
        cw = wnd_cw[parent];
        ch = wnd_ch[parent];
        ox = child_x[i];
        oy = child_y[i];
    } else {
        if (wnd_buf[hwnd] == 0)
            return 0;
        buf = wnd_buf[hwnd];
        cw = wnd_cw[hwnd];
        ch = wnd_ch[hwnd];
        ox = 0;
        oy = 0;
    }
    if (dc_pool[hwnd].magic == GDI_DC_MAGIC)
        *dc = &dc_pool[hwnd];
    else {
        myos_dc_t *d = &dc_pool[hwnd];
        d->magic = GDI_DC_MAGIC;
        d->buf = buf;
        d->cw = cw;
        d->ch = ch;
        d->ox = ox;
        d->oy = oy;
        d->fg = 0x00000000u;        /* texto negro */
        d->bg = 0x00FFFFFFu;        /* fondo blanco */
        d->bk_mode = GDI_BK_OPAQUE;
        d->font = 0;
        d->brush = STOCK_HANDLE(WHITE_BRUSH);   /* como Windows: brush blanco */
        d->pen = STOCK_HANDLE(BLACK_PEN);       /* y pen negro por defecto */
        d->pen_x = 0;
        d->pen_y = 0;
        d->hwnd = hwnd;
        d->dirty_x = 0;
        d->dirty_y = 0;
        d->dirty_w = 0;
        d->dirty_h = 0;
        *dc = d;
    }
    return 1;
}

uint32_t __attribute__((stdcall)) GetDC(uint32_t hwnd)
{
    myos_dc_t *dc;
    char num[16];
    if (!dc_lookup(hwnd, &dc))
        return 0;
    console_print("[user32] GetDC hwnd=");
    console_print(itoa32(num, hwnd));
    console_print(" -> ");
    console_print(itoa32(num, (uint32_t)dc));
    console_print("\n");
    return (uint32_t)dc;
}

uint32_t __attribute__((stdcall)) ReleaseDC(uint32_t hwnd, uint32_t dc) {
    char num[16];
    console_print("[u32] RD hwnd=");
    console_print(itoa32(num, hwnd));
    console_print(" dc=");
    console_print(itoa32(num, dc));
    console_print(" lo=");
    console_print(itoa32(num, (uint32_t)&dc_pool[0]));
    console_print(" hi=");
    console_print(itoa32(num, (uint32_t)&dc_pool[MAX_WNDPROCS]));
    console_print("\n");
    (void)hwnd;
    if (dc < (uint32_t)&dc_pool[0] || dc >= (uint32_t)&dc_pool[MAX_WNDPROCS]) return 0;
    ((myos_dc_t *)dc)->magic = 0;
    return 1;
}

/* FillRect: en Windows vive en USER32 (no en GDI32). Rellena el rect del
 * DC. pinceles stock: indice+1; creados (en gdi32): 0x1000+ (no
 * resolubles aqui -> blanco). */
static uint32_t u32_brush_color(uint32_t h)
{
    switch ((int)h - 1) {
    case 0:  return 0x00FFFFFFu;   /* WHITE_BRUSH */
    case 1:  return 0x00C0C0C0u;   /* LTGRAY_BRUSH */
    case 2:  return 0x00808080u;   /* GRAY_BRUSH */
    case 3:  return 0x00404040u;   /* DKGRAY_BRUSH */
    case 5:  return 0x00FFFFFFu;   /* NULL_BRUSH: pinta igual */
    default: return 0x00FFFFFFu;
    }
}

uint32_t __attribute__((stdcall)) FillRect(uint32_t hdc, const int32_t *rc, uint32_t brush)
{
    myos_dc_t *dc;
    uint32_t c;
    int l, t, r2, b2, x, y;
    if (hdc < (uint32_t)&dc_pool[0] ||
        hdc >= (uint32_t)&dc_pool[MAX_WNDPROCS])
        return 0;
    dc = (myos_dc_t *)hdc;
    if (dc->magic != GDI_DC_MAGIC || rc == 0 || dc->buf == 0)
        return 0;
    l = rc[0]; t = rc[1]; r2 = rc[2]; b2 = rc[3];
    if (l > r2) { int q = l; l = r2; r2 = q; }
    if (t > b2) { int q = t; t = b2; b2 = q; }
    c = (brush < 0x1000) ? u32_brush_color(brush)
                         : (brush ? 0x00FFFFFFu : 0x00FFFFFFu);
    for (y = t; y < b2; y++) {
        int ay = dc->oy + y;
        if (ay < 0 || ay >= dc->ch)
            continue;
        {
            uint32_t *row = (uint32_t *)dc->buf +
                            (uint32_t)ay * (uint32_t)dc->cw;
            for (x = l; x < r2; x++) {
                int ax = dc->ox + x;
                if (ax < 0 || ax >= dc->cw)
                    continue;
                row[ax] = px_disp(c);
            }
        }
    }
    return 1;
}

/* BeginPaint: rellena el PAINTSTRUCT (hdc, rcPaint=cliente) y devuelve
 * el DC. EndPaint libera el DC. */
#define WM_NULL      0x0000
typedef struct {
    int32_t left, top, right, bottom;
} myos_rect_t;

typedef struct {
    uint32_t hdc;
    int      fErase;
    myos_rect_t rcPaint;
    int      fRestore, fIncUpdate;
    uint32_t rgbReserved[32];
} myos_paintstruct_t;

uint32_t __attribute__((stdcall)) BeginPaint(uint32_t hwnd, myos_paintstruct_t *ps)
{
    myos_dc_t *dc;
    char num[16];
    if (ps == 0 || !dc_lookup(hwnd, &dc))
        return 0;
    console_print("[u32] BP hwnd=");
    console_print(itoa32(num, hwnd));
    console_print(" magic=");
    console_print(itoa32(num, dc->magic));
    console_print(" pen=");
    console_print(itoa32(num, dc->pen));
    console_print(" brush=");
    console_print(itoa32(num, dc->brush));
    console_print("\n");
    ps->hdc = (uint32_t)dc;
    ps->fErase = 1;
    ps->rcPaint.left = 0;
    ps->rcPaint.top = 0;
    ps->rcPaint.right = wnd_cw[hwnd];
    ps->rcPaint.bottom = wnd_ch[hwnd];
    ps->fRestore = 0;
    ps->fIncUpdate = 0;
    return ps->hdc;
}

uint32_t __attribute__((stdcall)) EndPaint(uint32_t hwnd, const myos_paintstruct_t *ps)
{
    char num[16];
    console_print("[u32] EP hwnd=");
    console_print(itoa32(num, hwnd));
    console_print(" ps=");
    console_print(itoa32(num, (uint32_t)ps));
    console_print(" ps->hdc=");
    console_print(itoa32(num, ps ? ps->hdc : 0));
    console_print("\n");
    if (ps)
        ReleaseDC(hwnd, ps->hdc);
    return 1;
}

/* --- wndproc built-in (clases RichEdit20A/EDIT/STATIC/...) ---
 * WM_SETTEXT/GetWindowText mantienen el texto; el paint dibuja el texto
 * en el buffer del padre (cliente blanco, letra negra) multilinea con
 * caret visible y salto de linea en '\n'; WM_CHAR/WM_KEYDOWN editan. */

/* memmove propio: user32 es freestanding (no linkea msvcrt/libc). */
static void memmove_myos(void *d, const void *s, uint32_t n)
{
    uint8_t *dd = (uint8_t *)d;
    const uint8_t *ss = (const uint8_t *)s;
    if (dd < ss)
        while (n--)
            *dd++ = *ss++;
    else if (dd > ss) {
        dd += n;
        ss += n;
        while (n--)
            *--dd = *--ss;
    }
}

/* Pinta el texto (y el caret) de un hijo en el buffer del padre. */
static void child_paint(uint32_t i)
{
    uint32_t parent = child_parent[i];
    uint32_t *px = (uint32_t *)wnd_buf[parent];
    int px0 = child_x[i], py0 = child_y[i];
    int pw = child_w[i], ph = child_h[i];
    int cw = wnd_cw[parent];
    const char *s = child_text[i];
    int cx = px0, cy = py0;
    int cur = child_cur[i], idx = 0;
    int cur_x = px0, cur_y = py0;
    int x, y;

    if (px == 0)
        return;
    /* fondo del editor: blanco */
    for (y = py0; y < py0 + ph && y < cw && y < wnd_ch[parent]; y++) {
        uint32_t *row = px + (uint32_t)y * (uint32_t)cw;
        for (x = px0; x < px0 + pw && x < cw; x++)
            row[x] = px_disp(0x00FFFFFFu);
    }
    /* texto 8x16 en negro, clip al rect del hijo, '\n' salta de linea */
    while (*s && cy < py0 + ph) {
        char c = *s;
        const unsigned char *g;
        if (c == '\n') {
            idx++;
            cx = px0;
            cy += 16;
            if (idx == cur) {
                cur_x = cx;
                cur_y = cy;
            }
            s++;
            continue;
        }
        if (c < 32 || c > 126)
            c = '?';
        g = font8x16_basic[c - 32];
        for (y = 0; y < 16 && cy + y < py0 + ph; y++)
            for (x = 0; x < 8; x++)
                if (g[y] & (0x80u >> x)) {
                    int xx = cx + x, yy = cy + y;
                    if (xx < px0 + pw && xx < cw && yy >= py0)
                        px[(uint32_t)yy * (uint32_t)cw + (uint32_t)xx] =
                            px_disp(0x00000000u);
                }
        idx++;
        if (idx == cur) {
            cur_x = cx + 8;
            cur_y = cy;
        }
        cx += 8;
        if (cx >= px0 + pw) {
            cx = px0;
            cy += 16;
        }
        s++;
    }
    /* caret (barra 1x16) en la posicion de insercion */
    if (cur >= 0) {
        if (cur_x >= px0 + pw)
            cur_x = px0 + pw - 1;
        if (cur_x < px0)
            cur_x = px0;
        if (cur_y < py0)
            cur_y = py0;
        for (y = 0; y < 16 && cur_y + y < py0 + ph && cur_y + y >= py0; y++)
            px[(uint32_t)(cur_y + y) * (uint32_t)cw + (uint32_t)cur_x] =
                px_disp(0x00000000u);
    }
}

/* Repinta un hijo y hace que el kernel recomponga el top-level padre. */
static void child_repaint(uint32_t i)
{
    uint32_t parent = child_parent[i];
    uint32_t hwnd;

    if (i >= CHILD_MAX)
        return;
    hwnd = CHILD_BASE + i;
    if (!wnd_proc[hwnd])
        return;
    child_paint(i);
    if (parent < MAX_WNDPROCS && wnd_proc[parent]) {
        /* Fase 20-D: blit solo el rect del hijo (region pequena) */
        int32_t rect[4];
        rect[0] = child_x[i];
        rect[1] = child_y[i];
        rect[2] = child_w[i];
        rect[3] = child_h[i];
        sys_winupdate_rect(parent, rect);
    }
}

/* Inserta/borra un caracter en el caret del control (hijo builtin). */
static void child_insert(uint32_t hwnd, char ch)
{
    uint32_t i = hwnd - CHILD_BASE;
    char *t = child_text[i];
    int len = 0, cur = child_cur[i];
    while (t[len])
        len++;
    if (cur < 0)
        cur = 0;
    if (cur > len)
        cur = len;
    if (ch == '\b') {
        if (cur > 0) {
            child_cur[i] = cur - 1;
            memmove_myos(t + cur - 1, t + cur, (uint32_t)(len - cur + 1));
            child_repaint(i);
        }
        return;
    }
    if (ch == '\n' || (ch >= 32 && ch <= 126)) {
        if (len < CHILD_TXTLEN - 1) {
            memmove_myos(t + cur + 1, t + cur, (uint32_t)(len - cur + 1));
            t[cur] = ch;
            child_cur[i] = cur + 1;
            child_repaint(i);
        }
    }
}

uint32_t builtin_wndproc(uint32_t hwnd, uint32_t m, uint32_t a, uint32_t b)
{
    (void)b;
    if (m == WM_SETTEXT) {
        uint32_t i = hwnd - CHILD_BASE;
        int k = 0;
        const char *s = (const char *)a;
        if (s == 0)
            s = "";
        while (s[k] && k < CHILD_TXTLEN - 1) {
            child_text[i][k] = s[k];
            k++;
        }
        child_text[i][k] = 0;
        child_cur[i] = k;
        child_repaint(i);
        return 1;
    }
    if (m == WM_GETTEXT) {
        uint32_t i = hwnd - CHILD_BASE;
        int k = 0;
        char *dst = (char *)a;
        if (dst == 0)
            return 0;
        while (child_text[i][k] && k < (int)b - 1) {
            dst[k] = child_text[i][k];
            k++;
        }
        dst[k] = 0;
        return (uint32_t)k;
    }
    if (m == WM_CHAR) {
        child_insert(hwnd, (char)a);
        return 0;
    }
    if (m == WM_KEYDOWN) {
        /* Enter/BackSpace llegan por KEYDOWN (no pasan el filtro CHAR) */
        if (a == '\n' || a == '\b') {
            child_insert(hwnd, (char)a);
            return 0;
        }
        /* teclas VK especiales (0x100+) y DEL; mover/borrar con caret */
        uint32_t i = hwnd - CHILD_BASE;
        char *t = child_text[i];
        int len = 0, cur = child_cur[i], k;
        while (t[len])
            len++;
        if (cur < 0)
            cur = 0;
        if (cur > len)
            cur = len;
        switch ((int)a) {
        case 0x100: /* LEFT */
            child_cur[i] = cur > 0 ? cur - 1 : 0;
            child_repaint(i);
            break;
        case 0x101: /* RIGHT */
            child_cur[i] = cur < len ? cur + 1 : len;
            child_repaint(i);
            break;
        case 0x104: /* HOME */
            child_cur[i] = 0;
            child_repaint(i);
            break;
        case 0x105: /* END */
            child_cur[i] = len;
            child_repaint(i);
            break;
        case 0x108: /* DEL */
            if (cur < len) {
                memmove_myos(t + cur, t + cur + 1, (uint32_t)(len - cur));
                child_repaint(i);
            }
            break;
        case 0x102: { /* UP: misma columna en la linea anterior */
            int linestart = 0, prevstart = 0, col, j, newpos;
            for (k = cur - 1; k >= 0; k--)
                if (t[k] == '\n') { linestart = k + 1; break; }
            if (linestart == 0)
                break;                          /* ya en la linea 1 */
            col = cur - linestart;
            for (j = linestart - 2; j >= 0; j--)
                if (t[j] == '\n') { prevstart = j + 1; break; }
            newpos = prevstart + col;
            if (newpos < prevstart)
                newpos = prevstart;
            if (newpos > linestart - 1)
                newpos = linestart - 1;
            child_cur[i] = newpos;
            child_repaint(i);
            break;
        }
        case 0x103: { /* DOWN: misma columna en la linea siguiente */
            int linestart = 0, endline = len, col, j, newpos;
            for (k = cur - 1; k >= 0; k--)
                if (t[k] == '\n') { linestart = k + 1; break; }
            for (k = cur; k < len; k++)
                if (t[k] == '\n') { endline = k; break; }
            if (endline >= len)
                break;                          /* ya en la ultima linea */
            col = cur - linestart;
            newpos = endline + 1 + col;
            if (newpos > len)
                newpos = len;
            if (newpos < endline + 1)
                newpos = endline + 1;
            (void)j;
            child_cur[i] = newpos;
            child_repaint(i);
            break;
        }
        default:
            break;
        }
        return 0;
    }
    if (m == WM_PAINT) {
        child_paint(hwnd - CHILD_BASE);
        return 0;
    }
    if (m == WM_CLOSE) {
        child_parent[hwnd - CHILD_BASE] = 0;
        return 0;
    }
    /* Mensajes RichEdit que metapad envia al hijo; el formato se acepta
     * pero no se aplica (evita el MessageBox 'Couldn't set para format.').
     * 0x444 = EM_SETPARAFORMAT, 0x447 = EM_SETTEXTEX, 0x43d = EM_EXSETSEL,
     * 0x437 = EM_GETSEL, 0x434 = EM_GETTEXTLENGTHEX */
    if (m == 0x444 || m == 0x447 || m == 0x43d || m == 0x437 ||
        m == 0x434 || m == 0x480 || m == 0x481 || m == 0x482)
        return 1;
    return DefWindowProcA(hwnd, m, a, 0);
}

uint32_t __attribute__((stdcall)) SetFocus(uint32_t hwnd) {
    uint32_t prev = focus_edit;
    if (is_child(hwnd) && child_edit[hwnd - CHILD_BASE]) {
        focus_edit = hwnd;
        child_repaint(hwnd - CHILD_BASE);
    }
    return prev;
}
uint32_t GetFocus(void) { return focus_edit; }
uint32_t __attribute__((stdcall)) SetWindowTextA(uint32_t hwnd, uint32_t t)
{
    if (hwnd >= MAX_WNDPROCS || !wnd_proc[hwnd])
        return 0;
    if (is_child(hwnd)) {
        uint32_t i = hwnd - CHILD_BASE;
        int k = 0;
        const char *s = (const char *)t;
        if (s == 0)
            s = "";
        while (s[k] && k < CHILD_TXTLEN - 1) {
            child_text[i][k] = s[k];
            k++;
        }
        child_text[i][k] = 0;
        return 1;
    }
    if (str_valid((const char *)t))
        sys_wintitle(hwnd, (const char *)t);
    return 1;
}

uint32_t __attribute__((stdcall)) GetWindowTextA(uint32_t hwnd, uint32_t b, int n)
{
    if (is_child(hwnd)) {
        uint32_t i = hwnd - CHILD_BASE;
        int k = 0;
        char *dst = (char *)b;
        if (dst == 0 || n <= 0)
            return 0;
        while (child_text[i][k] && k < n - 1) {
            dst[k] = child_text[i][k];
            k++;
        }
        dst[k] = 0;
        return (uint32_t)k;
    }
    (void)b; (void)n;
    return 0;
}

uint32_t __attribute__((stdcall)) GetWindowTextLengthA(uint32_t hwnd)
{
    if (is_child(hwnd)) {
        uint32_t i = hwnd - CHILD_BASE;
        uint32_t k = 0;
        while (child_text[i][k])
            k++;
        return k;
    }
    return 0;
}
uint32_t __attribute__((stdcall)) GetWindowLongA(uint32_t hwnd, int idx) { (void)hwnd; (void)idx; return 0; }
uint32_t __attribute__((stdcall)) SetWindowLongA(uint32_t hwnd, int idx, uint32_t v) { (void)hwnd; (void)idx; (void)v; return 0; }
uint32_t __attribute__((stdcall)) SetClassLongA(uint32_t hwnd, int idx, uint32_t v) { (void)hwnd; (void)idx; (void)v; return 0; }
uint32_t __attribute__((stdcall)) SetWindowPos(uint32_t hwnd, uint32_t after, int x, int y, int w,
                      int h, uint32_t f)
{ (void)hwnd; (void)after; (void)x; (void)y; (void)w; (void)h; (void)f; return 0; }
uint32_t __attribute__((stdcall)) GetWindowRect(uint32_t hwnd, uint32_t r) { (void)hwnd; (void)r; return 0; }
uint32_t __attribute__((stdcall)) GetClientRect(uint32_t hwnd, uint32_t r)
{
    int *rc = (int *)r;
    if (rc == 0)
        return 0;
    if (is_child(hwnd)) {
        rc[0] = 0;
        rc[1] = 0;
        rc[2] = child_w[hwnd - CHILD_BASE];
        rc[3] = child_h[hwnd - CHILD_BASE];
    } else {
        rc[0] = 0;
        rc[1] = 0;
        rc[2] = wnd_cw[hwnd];
        rc[3] = wnd_ch[hwnd];
    }
    return 1;
}
uint32_t __attribute__((stdcall)) GetWindowPlacement(uint32_t hwnd, uint32_t p) { (void)hwnd; (void)p; return 0; }
uint32_t __attribute__((stdcall)) GetCursorPos(uint32_t p) { (void)p; return 0; }
uint32_t __attribute__((stdcall)) SetCursor(uint32_t c) { (void)c; return 0; }
uint32_t __attribute__((stdcall)) GetParent(uint32_t hwnd) { (void)hwnd; return 0; }
uint32_t __attribute__((stdcall)) EnableWindow(uint32_t hwnd, int en) { (void)hwnd; (void)en; return 0; }
uint32_t __attribute__((stdcall)) GetKeyboardState(uint32_t s) { (void)s; return 0; }
uint32_t __attribute__((stdcall)) SetKeyboardState(uint32_t s) { (void)s; return 0; }
uint32_t __attribute__((stdcall)) IsDialogMessageA(uint32_t d, uint32_t m) { (void)d; (void)m; return 0; }
uint32_t __attribute__((stdcall)) RegisterWindowMessageA(uint32_t s) { (void)s; return 0x0400; }
uint32_t __attribute__((stdcall)) MessageBeep(uint32_t t) { (void)t; return 0; }
uint32_t __attribute__((stdcall)) LoadIconA(uint32_t i, uint32_t n) { (void)i; (void)n; console_print("[user32] LoadIconA\n"); return 0; }
uint32_t __attribute__((stdcall)) LoadCursorA(uint32_t i, uint32_t n) { (void)i; (void)n; console_print("[user32] LoadCursorA\n"); return 0; }
uint32_t __attribute__((stdcall)) LoadStringA(uint32_t i, uint32_t id, uint32_t b, int n)
{
    /* RT_STRING (tipo 6): el bloque es (id>>4)+1; dentro, 16 strings
     * [len u16][utf16]. Lanza mensaje al log si no existe. */
    const uint8_t *tbl;
    uint32_t size, inblk, k;
    char num[16];
    int pos, out;

    (void)i;
    if (b == 0 || n <= 0)
        return 0;
    tbl = find_resource(6, (id >> 4) + 1, &size);
    if (tbl == 0) {
        console_print("[user32] LoadStringA id=");
        console_print(itoa32(num, id));
        console_print(" (sin recurso)\n");
        return 0;
    }
    inblk = id & 0xF;
    pos = 0;
    out = 0;
    for (k = 0; k < 16 && k <= inblk; k++) {
        uint16_t len;
        int w;
        if ((uint32_t)pos + 2 > size)
            return 0;
        len = rd16u(tbl + pos);
        pos += 2;
        if ((uint32_t)pos + (uint32_t)len * 2 > size)
            return 0;
        if (k == inblk) {
            for (w = 0; w < len && out < n - 1; w++) {
                uint16_t c = rd16u(tbl + pos + w * 2);
                ((char *)b)[out++] = (c < 0x80) ? (char)c : '?';
            }
            break;
        }
        pos += len * 2;
    }
    ((char *)b)[out] = 0;
    if (out > 0) {
        console_print("[user32] LoadStringA id=");
        console_print(itoa32(num, id));
        console_print(" -> '");
        console_print((const char *)b);
        console_print("'\n");
    }
    return (uint32_t)out;
}
static void wsprint_uint(char *buf, uint32_t v, int base, int upper)
{
    char tmp[16];
    int n = 0;
    static const char *dig_l = "0123456789abcdef";
    static const char *dig_u = "0123456789ABCDEF";
    const char *dig = upper ? dig_u : dig_l;
    if (v == 0)
        tmp[n++] = '0';
    while (v) {
        tmp[n++] = dig[v % (uint32_t)base];
        v /= (uint32_t)base;
    }
    while (n)
        *buf++ = tmp[--n];
    *buf = 0;
}

uint32_t __attribute__((stdcall)) wsprintfA(uint32_t b, uint32_t f, ...)
{
    char *out = (char *)b;
    const char *fmt = (const char *)f;
    uint32_t args[8];
    int ai = 0, k = 0;
    __builtin_va_list ap;
    int i;

    if (fmt == 0)
        fmt = "";
    __builtin_va_start(ap, f);
    for (i = 0; i < 8; i++)
        args[i] = __builtin_va_arg(ap, uint32_t);
    __builtin_va_end(ap);
    while (*fmt && k < 240) {
        if (*fmt != '%') {
            out[k++] = *fmt++;
            continue;
        }
        fmt++;
        if (*fmt == 's' || *fmt == 'S') {
            const char *s = (const char *)args[ai++];
            if (s == 0)
                s = "";
            while (*s && k < 239) {
                out[k++] = *s;
                s++;
            }
        } else if (*fmt == 'd' || *fmt == 'i' || *fmt == 'u') {
            char tmp[16];
            int neg = 0;
            int32_t v = (int32_t)args[ai++];
            if (v < 0 && *fmt != 'u') {
                neg = 1;
                v = -v;
            }
            wsprint_uint(tmp, (uint32_t)v, 10, 0);
            if (neg)
                out[k++] = '-';
            {   char *t = tmp;
                while (*t && k < 239)
                    out[k++] = *t++;
            }
        } else if (*fmt == 'x' || *fmt == 'X') {
            char tmp[16];
            wsprint_uint(tmp, args[ai++], 16, *fmt == 'X');
            {   char *t = tmp;
                while (*t && k < 239)
                    out[k++] = *t++;
            }
        } else if (*fmt == 'c') {
            out[k++] = (char)(uint8_t)args[ai++];
        } else {
            out[k++] = '%';
        }
        if (*fmt)
            fmt++;
    }
    out[k] = 0;
    return (uint32_t)k;
}
uint32_t __attribute__((stdcall)) GetSysColor(int idx) { (void)idx; return 0x00FFFFFFu; }
uint32_t __attribute__((stdcall)) GetSysColorBrush(int idx) { (void)idx; return 0; }
uint32_t __attribute__((stdcall)) SystemParametersInfoA(uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{ (void)a; (void)b; (void)c; (void)d; return 0; }
uint32_t __attribute__((stdcall)) GetDialogBaseUnits(void) { return 0; }
uint32_t __attribute__((stdcall)) GetDlgItem(uint32_t d, int id) { (void)d; (void)id; return 0; }
uint32_t __attribute__((stdcall)) GetDlgItemTextA(uint32_t d, int id, uint32_t b, int n)
{ (void)d; (void)id; (void)b; (void)n; return 0; }
uint32_t __attribute__((stdcall)) SetDlgItemTextA(uint32_t d, int id, uint32_t b)
{ (void)d; (void)id; (void)b; return 0; }
/* --- Dialogos modales reales (Fase 23-B5) ---
 * DialogBoxParamA/EndDialog sobre el patron de menu_modal (drenan
 * sys_event). Parsea DLGTEMPLATE (32-bit, no extendido) del recurso
 * RT_DIALOG, dibuja la ventana y los controles (BUTTON/STATIC/EDIT),
 * envia WM_INITDIALOG al DlgProc y enruta WM_COMMAND/WM_CLOSE.
 * EndDialog corta el bucle y devuelve el resultado. */

#define RT_DIALOG 5
#define WM_INITDIALOG 0x0110
#define DLG_SCALE 2             /* dialog units -> px (aproximado) */

typedef struct {
    int      id;
    int      x, y, w, h;        /* en px */
    int      cls;               /* 0=BUTTON 1=STATIC 2=EDIT 3=otro */
    int      is_btn;
    int      is_default;        /* BS_DEFPUSHBUTTON */
    char     text[48];
    user32_button_t b;
} dlgctl_t;

static dlgctl_t dlg_ctls[16];
static int dlg_nctl;
static uint32_t dlg_proc;
static uint32_t dlg_param;
static int dlg_result;
static int dlg_done;
static uint32_t dlg_cur;

typedef int (__attribute__((stdcall)) *dlgproc_t)(uint32_t, uint32_t,
                                                  uint32_t, uint32_t);

/* Los DlgProc de mingw son __stdcall (ret $0x10): el callee limpia sus
 * 16 bytes de args. Se llama con 4 pushes y sin cleanup del caller
 * (un call cdecl limpiaria 16 mas -> doble limpieza -> pila corrupta). */
static int dlg_call(uint32_t fn, uint32_t hwnd, uint32_t msg, uint32_t wp,
                    uint32_t lp)
{
    int ret;
    __asm__ volatile(
        "pushl %4\n\t"
        "pushl %3\n\t"
        "pushl %2\n\t"
        "pushl %1\n\t"
        "call *%5\n\t"
        : "=a"(ret)
        : "r"(hwnd), "r"(msg), "r"(wp), "r"(lp), "r"(fn)
        : "ecx", "edx", "memory");
    return ret;
}

/* Clase del control: ordinal 0xFFFF+0x80xx o string. */
static int dlg_ctl_class(const uint8_t **pp, const uint8_t *end)
{
    uint16_t v;
    if (*pp + 2 > end)
        return 3;
    v = rd16u(*pp); *pp += 2;
    if (v == 0xFFFF) {
        if (*pp + 2 > end)
            return 3;
        v = rd16u(*pp); *pp += 2;
        switch (v & 0xFF) {
        case 0x80: return 0;    /* BUTTON */
        case 0x82: return 1;    /* STATIC */
        case 0x81: return 2;    /* EDIT */
        default:   return 3;
        }
    }
    /* string: consumir el nombre */
    while (rd16u(*pp)) *pp += 2;
    *pp += 2;
    return 3;
}

/* Texto del control: string o ordinal. */
static void dlg_ctl_text(const uint8_t **pp, const uint8_t *end, char *buf,
                         int max)
{
    uint16_t v;
    if (*pp + 2 > end) {
        buf[0] = 0;
        return;
    }
    v = rd16u(*pp);
    if (v == 0xFFFF) {           /* ordinal (id de recurso): sin texto */
        *pp += 4;
        buf[0] = 0;
        return;
    }
    utf16_to_ascii(buf, *pp, max);
    while (rd16u(*pp)) *pp += 2;
    *pp += 2;
}

static uint32_t dialog_modal(const uint8_t *tpl, uint32_t size, uint32_t f,
                             uint32_t a)
{
    uint32_t info[4];
    uint32_t ev[5];
    const uint8_t *p = tpl, *end = tpl + size;
    uint32_t style, cdit;
    int dlu_cx, dlu_cy, wx, wy, ww, wh, i;
    char title[64];

    if (sys_gfxinfo(info) != 0)
        return 0xFFFFFFFFu;
    lfb = (uint32_t *)info[0];
    scr_w = info[1];
    scr_h = info[2];
    if (p + 18 > end)
        return 0xFFFFFFFFu;

    style = rd32u(p); p += 4;
    p += 4;                      /* dwExtendedStyle */
    cdit  = rd16u(p); p += 2;
    p += 2;                      /* x */
    p += 2;                      /* y */
    dlu_cx = rd16u(p); p += 2;
    dlu_cy = rd16u(p); p += 2;
    p += 2;                      /* menu (ninguno) */
    p += 2;                      /* clase (ninguna) */
    utf16_to_ascii(title, p, 63);        /* titulo */
    while (rd16u(p)) p += 2;
    p += 2;
    if (style & 0x00000040) {    /* DS_SETFONT: puntos + face */
        p += 2;
        while (rd16u(p)) p += 2;
        p += 2;
    }

    dlg_nctl = 0;
    dlg_proc = f;
    dlg_param = a;
    dlg_result = 0;
    dlg_done = 0;
    dlg_cur = 1;

        ww = dlu_cx * DLG_SCALE;
    wh = dlu_cy * DLG_SCALE;
    wx = ((int)scr_w - ww) / 2;
    wy = ((int)scr_h - wh) / 2;

    fillrect(wx - 3, wy - 3, ww + 6, wh + 6, COLOR_FRAME);
    fillrect(wx, wy, ww, wh, COLOR_BG);
    fillrect(wx, wy, ww, 18, COLOR_TITLE);
    drawtext(wx + 6, wy + 2, title, COLOR_TEXT);

    for (i = 0; i < (int)cdit && dlg_nctl < 16; i++) {
        dlgctl_t *c = &dlg_ctls[dlg_nctl];
        uint32_t cstyle;
        p = (const uint8_t *)(((uint32_t)p + 3) & ~3u);   /* alinear */
        if (p + 20 > end)
            break;
        cstyle = rd32u(p); p += 4;
        p += 4;                  /* extended style */
        c->x = rd16u(p) * DLG_SCALE; p += 2;
        c->y = rd16u(p) * DLG_SCALE; p += 2;
        c->w = rd16u(p) * DLG_SCALE; p += 2;
        c->h = rd16u(p) * DLG_SCALE; p += 2;
        c->id = rd16u(p); p += 2;
        c->cls = dlg_ctl_class(&p, end);
        dlg_ctl_text(&p, end, c->text, 47);
        /* creation data: WORD cb + cb bytes (alineado a WORD) */
        if (p + 2 <= end) {
            uint32_t cb = rd16u(p);
            p += 2;
            if (cb)
                p += (cb + 1) & ~1u;
        }
        c->is_btn = (c->cls == 0);
        c->is_default = (cstyle & 0x1) != 0;   /* BS_DEFPUSHBUTTON */
        c->x += wx;
        c->y += wy + 18;

                if (c->is_btn) {
            c->b.x = c->x; c->b.y = c->y; c->b.w = c->w; c->b.h = c->h;
            c->b.label = c->text;
            c->b.fg = COLOR_BTN_TX; c->b.bg = COLOR_BTN;
            c->b.fg_p = COLOR_BTN; c->b.bg_p = COLOR_TEXT;
            c->b.hovered = 0; c->b.pressed = 0;
            user32_draw_button(&c->b);
        } else {
            drawtext(c->x + 4, c->y + 2, c->text, COLOR_TEXT);
        }
        dlg_nctl++;
    }

    /* WM_INITDIALOG */
    if (dlg_proc)
        dlg_call(dlg_proc, dlg_cur, WM_INITDIALOG, a, 0);

    /* bucle modal */
    for (;;) {
        int handled = 0;
        if (sys_event(ev) != 0)
            continue;
        for (i = 0; i < dlg_nctl; i++) {
            dlgctl_t *c = &dlg_ctls[i];
            if (c->is_btn && user32_button_feed(&c->b, ev)) {
                if (dlg_proc)
                    dlg_call(dlg_proc, dlg_cur, WM_COMMAND, c->id, 0);
                handled = 1;
                break;
            }
        }
        if (handled)
            continue;
        if (ev[0] == EV_KEY) {
            if (ev[4] == '\n') {         /* Enter: boton por defecto */
                for (i = 0; i < dlg_nctl; i++)
                    if (dlg_ctls[i].is_default) {
                        if (dlg_proc)
                            dlg_call(dlg_proc, dlg_cur, WM_COMMAND,
                                           dlg_ctls[i].id, 0);
                        break;
                    }
            } else if (ev[4] == 27) {    /* Esc: WM_CLOSE */
                if (dlg_proc)
                    dlg_call(dlg_proc, dlg_cur, WM_CLOSE, 0, 0);
            }
        }
        if (dlg_done)
            break;
    }
    return (uint32_t)dlg_result;
}

uint32_t __attribute__((stdcall)) DialogBoxParamA(uint32_t i, uint32_t t, uint32_t p, uint32_t f,
                         uint32_t a)
{
    const uint8_t *tpl;
    uint32_t size = 0, id;
    (void)i; (void)p;
    if (t < 0x10000)             /* MAKEINTRESOURCE */
        id = t;
    else                         /* nombre de recurso (no soportado) */
        return 0xFFFFFFFFu;
    tpl = find_resource(RT_DIALOG, id, &size);
    if (tpl == 0)
        return 0xFFFFFFFFu;
    return dialog_modal(tpl, size, f, a);
}

uint32_t __attribute__((stdcall)) CreateDialogParamA(uint32_t i, uint32_t t, uint32_t p, uint32_t f,
                            uint32_t a)
{ (void)i; (void)t; (void)p; (void)f; (void)a; return 0; }

uint32_t __attribute__((stdcall)) EndDialog(uint32_t d, int r)
{
    (void)d;
    dlg_result = r;
    dlg_done = 1;
    return 0;
}
uint32_t __attribute__((stdcall)) CreateMenu(void) { return 0; }
uint32_t __attribute__((stdcall)) GetMenu(uint32_t hwnd) { (void)hwnd; return 0; }

/* --- barra de menu de ventana (Fase D) ---
 * SetMenu activa la barra en el kernel (SYS_MENUBAR: franja gris +
 * labels top-level dibujados por el winmgr, que sobreviven a las
 * recomposiciones) y guarda el hwnd para el menú modal. */

static uint32_t menu_win_hwnd;      /* hwnd con la barra de menu        */

/* Labels top-level (depth 0) en flat NUL-separados para el kernel. */
static void menu_build_flat(char *flat, uint32_t max)
{
    uint32_t k = 0;
    uint32_t i;

    flat[0] = 0;
    for (i = 0; i < loaded_menu.count; i++) {
        menu_item_t *it = &loaded_menu.items[i];
        if (it->depth == 0 && it->type == MNU_POP) {
            uint32_t j = 0;
            while (it->text[j] && j < 47 && k < max - 2) {
                flat[k++] = it->text[j++];
            }
            flat[k++] = 0;
        }
    }
    flat[k] = 0;
}

uint32_t __attribute__((stdcall)) SetMenu(uint32_t hwnd, uint32_t menu)
{
    char flat[200];

    (void)menu;
    if (hwnd == 0 || loaded_menu.count == 0)
        return 0;
    menu_build_flat(flat, sizeof(flat));
    sys_menubar(hwnd, 1, flat);
    menu_win_hwnd = hwnd;
    return 1;
}

uint32_t __attribute__((stdcall)) DrawMenuBar(uint32_t hwnd)
{
    if (hwnd == 0 || loaded_menu.count == 0)
        return 0;
    menu_win_hwnd = hwnd;
    sys_winupdate(hwnd);
    return 1;
}

/* --- menu desplegable modal (Fase D) --- */

#define MENU_ITEM_H     18
#define MENU_W          220

#define C_POPUP_BG  0x00F0F0F0u
#define C_POPUP_TX  0x00000000u
#define C_POPUP_HI  0x000000A0u
#define C_POPUP_HI_TX 0x00FFFFFFu

/* Texto del item sin '&' y sin el '\tshortcut' (la parte de shortcut
 * se ignora en el dibujo). */
static void menu_label(const menu_item_t *it, char *out, uint32_t max)
{
    uint32_t k = 0, i;

    for (i = 0; it->text[i] && k < max - 1; i++) {
        char c = it->text[i];
        if (c == '&')
            continue;
        if (c == '\t')
            break;
        out[k++] = c;
    }
    out[k] = 0;
}

/* Numero de items (depth 1) del top-level `top` (los MNU_SEP cuentan
 * como fila pero no son seleccionables). */
static int menu_popup_rows(int top)
{
    int n = 0, i;

    for (i = top + 1; i < (int)loaded_menu.count; i++) {
        menu_item_t *it = &loaded_menu.items[i];
        if (it->depth <= 0)
            break;
        if (it->depth == 1)
            n++;
    }
    return n;
}

/* item (indice global) de la fila `row` del popup de `top`. */
static int menu_popup_item(int top, int row)
{
    int i, r = 0;

    for (i = top + 1; i < (int)loaded_menu.count; i++) {
        menu_item_t *it = &loaded_menu.items[i];
        if (it->depth <= 0)
            break;
        if (it->depth == 1) {
            if (r == row)
                return i;
            r++;
        }
    }
    return -1;
}

/* Indice del top-level cuyo label cae en la x de pantalla `x`
 * (replica el layout del kernel: x0 = winx+FRAME+4, paso 8*len+16). */
static int menu_bar_top_at(uint32_t hwnd, int x)
{
    uint32_t info[8];
    int cur, i;

    if (sys_wininfo(hwnd, info) != 0)
        return -1;
    cur = (int)info[0] + 2 + 4;
    for (i = 0; i < (int)loaded_menu.count; i++) {
        menu_item_t *it = &loaded_menu.items[i];
        uint32_t len;
        if (it->depth != 0 || it->type != MNU_POP)
            continue;
        menu_label(it, (char *)&it->text, 48);
        len = 0;
        while (it->text[len])
            len++;
        if (x >= cur && x < cur + (int)len * 8 + 16)
            return i;
        cur += (int)len * 8 + 16;
    }
    return -1;
}

/* X de pantalla donde empieza el label del top-level `top`. */
static int menu_bar_top_x(uint32_t hwnd, int top)
{
    uint32_t info[8];
    int cur, i;

    if (sys_wininfo(hwnd, info) != 0)
        return 0;
    cur = (int)info[0] + 2 + 4;
    for (i = 0; i <= top && i < (int)loaded_menu.count; i++) {
        menu_item_t *it = &loaded_menu.items[i];
        uint32_t len;
        if (it->depth != 0 || it->type != MNU_POP)
            continue;
        if (i == top)
            return cur;
        menu_label(it, (char *)&it->text, 48);
        len = 0;
        while (it->text[len])
            len++;
        cur += (int)len * 8 + 16;
    }
    return 0;
}

/* Top-level cuyo mnemónico (char tras '&') es `c`. */
static int menu_bar_top_letter(uint32_t hwnd, char c)
{
    uint32_t info[8];
    int i;
    int cur;

    if (sys_wininfo(hwnd, info) != 0)
        return -1;
    if (c >= 'a' && c <= 'z')
        c = (char)(c - 32);
    cur = (int)info[0] + 2 + 4;
    for (i = 0; i < (int)loaded_menu.count; i++) {
        menu_item_t *it = &loaded_menu.items[i];
        uint32_t len = 0, j;
        char mn = 0;
        if (it->depth != 0 || it->type != MNU_POP)
            continue;
        for (j = 0; it->text[j]; j++) {
            if (it->text[j] == '&' && it->text[j + 1]) {
                mn = it->text[j + 1];
                break;
            }
        }
        if (mn >= 'a' && mn <= 'z')
            mn = (char)(mn - 32);
        if (mn == c)
            return i;
        for (j = 0; it->text[j]; j++)
            if (it->text[j] != '&')
                len++;
        cur += (int)len * 8 + 16;
    }
    return -1;
}

/* Dibuja el desplegable de `top` con highlight `hi` (-1 = ninguno).
 * Devuelve el alto en *h. La x del popup es la del top-level. */
static void menu_popup_draw(uint32_t hwnd, int top, int x, int y, int hi)
{
    uint32_t info[8];
    char lb[48];
    int rows, r, px, py, i;

    if (sys_wininfo(hwnd, info) != 0)
        return;
    rows = menu_popup_rows(top);
    px = x;
    py = (int)info[1] + WM_TITLE_H + WM_MENU_H;
    fillrect(px - 1, py - 1, MENU_W + 2, rows * MENU_ITEM_H + 3,
             COLOR_FRAME);
    fillrect(px, py, MENU_W, rows * MENU_ITEM_H + 1, C_POPUP_BG);
    for (r = 0; r < rows; r++) {
        i = menu_popup_item(top, r);
        if (i < 0)
            break;
        {
            menu_item_t *it = &loaded_menu.items[i];
            int iy = py + 1 + r * MENU_ITEM_H;
            if (r == hi) {
                fillrect(px + 1, iy, MENU_W - 2, MENU_ITEM_H - 2,
                         C_POPUP_HI);
            }
            if (it->type == MNU_SEP) {
                fillrect(px + 8, iy + MENU_ITEM_H / 2 - 1,
                         MENU_W - 16, 1, 0x00808080u);
                continue;
            }
            menu_label(it, lb, sizeof(lb));
            drawtext(px + 8, iy + 1,
                     (r == hi) ? lb : lb, (r == hi) ? C_POPUP_HI_TX
                                                    : C_POPUP_TX);
            if (it->type == MNU_POP) {
                drawtext(px + MENU_W - 16, iy + 1, ">",
                         (r == hi) ? C_POPUP_HI_TX : C_POPUP_TX);
            }
        }
    }
}

/* Bucle modal del desplegable del top-level `top` (x = x del label).
 * Devuelve el id del item elegido o 0 al cancelar. */
static uint32_t menu_modal(uint32_t hwnd, int top, int x)
{
    uint32_t info[8];
    uint32_t ev[5];
    int hi = -1, rows, pop_x, pop_y;
    uint32_t id = 0;

    if (sys_gfxinfo(info) != 0)
        return 0;
    lfb = (uint32_t *)info[0];
    scr_w = info[1];
    scr_h = info[2];
    if (sys_wininfo(hwnd, info) != 0)
        return 0;
    pop_x = menu_bar_top_x(hwnd, top);
    if (pop_x == 0)
        pop_x = x;
    pop_y = (int)info[1] + WM_TITLE_H + WM_MENU_H;
    rows = menu_popup_rows(top);
    menu_popup_draw(hwnd, top, pop_x, pop_y, hi);

    for (;;) {
        int sel = 0;
        if (sys_event(ev) != 0)
            continue;
        switch (ev[0]) {
        case EV_MOVE: {
            int row = ((int)ev[2] - pop_y) / MENU_ITEM_H;
            int t;
            if (row >= 0 && row < rows && row != hi) {
                hi = row;
                menu_popup_draw(hwnd, top, pop_x, pop_y, hi);
            } else if (row < 0 || row >= rows) {
                t = menu_bar_top_at(hwnd, (int)ev[1]);
                if (t >= 0 && t != top) {
                    top = t;
                    hi = -1;
                    rows = menu_popup_rows(top);
                    menu_popup_draw(hwnd, top, pop_x, pop_y, hi);
                }
            }
            break;
        }
        case EV_BUTTON_DOWN: {
            int row = ((int)ev[2] - pop_y) / MENU_ITEM_H;
            int t;
            if (row >= 0 && row < rows) {
                if (row != hi) {
                    hi = row;
                    menu_popup_draw(hwnd, top, pop_x, pop_y, hi);
                }
            } else if ((t = menu_bar_top_at(hwnd, (int)ev[1])) >= 0 &&
                       t != top) {
                top = t;
                hi = -1;
                rows = menu_popup_rows(top);
                menu_popup_draw(hwnd, top, pop_x, pop_y, hi);
            } else {
                goto out_cancel;
            }
            break;
        }
        case EV_BUTTON_UP: {
            int row = ((int)ev[2] - pop_y) / MENU_ITEM_H;
            if ((int)ev[2] < pop_y) {
                /* UP del clic que abrio el menu (sobre la barra):
                 * ignorar, no seleccionar ni cancelar. */
                break;
            }
            if (row >= 0 && row < rows) {
                hi = row;
                sel = 1;
            } else {
                goto out_cancel;
            }
            break;
        }
        case EV_KEY: {
            uint32_t key = ev[4];
            if (key == 0x102 || key == 0x103) {   /* Up/Down */
                int step = (key == 0x102) ? -1 : 1;
                int r = hi;
                do {
                    r += step;
                    if (r < 0 || r >= rows) {
                        if (r < 0)
                            r = rows - 1;
                        else
                            r = 0;
                    }
                    if (menu_popup_item(top, r) >= 0 &&
                        loaded_menu.items[menu_popup_item(top, r)].type
                            != MNU_SEP)
                        break;
                } while (1);
                hi = r;
                menu_popup_draw(hwnd, top, pop_x, pop_y, hi);
            } else if (key == '\n') {
                sel = 1;
            } else if (key == 27) {
                goto out_cancel;
            } else if (key == 0x100 || key == 0x101) {  /* Izq/Der */
                int dir = (key == 0x100) ? -1 : 1;
                int t = top, guard = 0;
                do {
                    int i;
                    t += dir;
                    if (t < 0 || t >= (int)loaded_menu.count) {
                        t = (t < 0) ? (int)loaded_menu.count - 1 : 0;
                    }
                    for (i = t; i >= 0 && i < (int)loaded_menu.count; i++) {
                        menu_item_t *it = &loaded_menu.items[i];
                        if (it->depth == 0 && it->type == MNU_POP) {
                            t = i;
                            break;
                        }
                    }
                    guard++;
                } while (t == top && guard < 4);
                if (t != top) {
                    top = t;
                    hi = -1;
                    rows = menu_popup_rows(top);
                    menu_popup_draw(hwnd, top, pop_x, pop_y, hi);
                }
            } else if (key >= 32 && key <= 126) {
                int r;
                for (r = 0; r < rows; r++) {
                    int ii = menu_popup_item(top, r);
                    menu_item_t *it;
                    uint32_t j;
                    if (ii < 0)
                        break;
                    it = &loaded_menu.items[ii];
                    for (j = 0; it->text[j]; j++) {
                        if (it->text[j] == '&' && it->text[j + 1] &&
                            (it->text[j + 1] == (char)key ||
                             (it->text[j + 1] >= 'A' &&
                              it->text[j + 1] <= 'Z' &&
                              it->text[j + 1] + 32 == (char)key))) {
                            hi = r;
                            sel = 1;
                            break;
                        }
                    }
                    if (sel)
                        break;
                }
            }
            break;
        }
        default:
            break;
        }
        if (sel) {
            int ii = menu_popup_item(top, hi < 0 ? 0 : hi);
            if (ii >= 0 && loaded_menu.items[ii].type == MNU_CMD)
                id = loaded_menu.items[ii].id;
            else
                id = 0;
            goto out;
        }
    }
out_cancel:
    id = 0;
out:
    sys_winupdate(hwnd);
    if (id)
        msgq_push(hwnd, WM_COMMAND, id, 0);
    return id;
}
uint32_t __attribute__((stdcall)) DestroyMenu(uint32_t menu) { (void)menu; return 0; }
uint32_t __attribute__((stdcall)) GetSubMenu(uint32_t menu, int pos) { (void)menu; (void)pos; return 0; }
uint32_t __attribute__((stdcall)) GetMenuItemCount(uint32_t menu) { (void)menu; return 0; }
uint32_t __attribute__((stdcall)) GetMenuItemInfoA(uint32_t menu, uint32_t item, uint32_t bypos,
                          uint32_t info)
{ (void)menu; (void)item; (void)bypos; (void)info; return 0; }
uint32_t __attribute__((stdcall)) SetMenuItemInfoA(uint32_t menu, uint32_t item, uint32_t bypos,
                          uint32_t info)
{ (void)menu; (void)item; (void)bypos; (void)info; return 0; }
uint32_t __attribute__((stdcall)) InsertMenuItemA(uint32_t menu, uint32_t item, uint32_t bypos,
                         uint32_t info)
{ (void)menu; (void)item; (void)bypos; (void)info; return 0; }
uint32_t __attribute__((stdcall)) DeleteMenu(uint32_t menu, uint32_t item, uint32_t bypos)
{ (void)menu; (void)item; (void)bypos; return 0; }
uint32_t __attribute__((stdcall)) EnableMenuItem(uint32_t menu, uint32_t item, uint32_t f)
{ (void)menu; (void)item; (void)f; return 0; }
uint32_t __attribute__((stdcall)) CheckMenuRadioItem(uint32_t menu, uint32_t a, uint32_t b,
                            uint32_t c, uint32_t f)
{ (void)menu; (void)a; (void)b; (void)c; (void)f; return 0; }
uint32_t __attribute__((stdcall)) TrackPopupMenuEx(uint32_t menu, uint32_t f, int x, int y,
                          uint32_t h, uint32_t r)
{ (void)menu; (void)f; (void)x; (void)y; (void)h; (void)r; return 0; }
uint32_t __attribute__((stdcall)) InvalidateRect(uint32_t hwnd, uint32_t r, uint32_t e)
{ (void)hwnd; (void)r; (void)e; return 0; }
uint32_t __attribute__((stdcall)) RedrawWindow(uint32_t hwnd, uint32_t r, uint32_t u, uint32_t f)
{ (void)hwnd; (void)r; (void)u; (void)f; return 0; }
uint32_t __attribute__((stdcall)) EnableScrollBar(uint32_t hwnd, uint32_t a, uint32_t b)
{ (void)hwnd; (void)a; (void)b; return 0; }
uint32_t __attribute__((stdcall)) GetScrollInfo(uint32_t hwnd, uint32_t a, uint32_t b)
{ (void)hwnd; (void)a; (void)b; return 0; }
uint32_t __attribute__((stdcall)) GetClipboardData(uint32_t f) { (void)f; return 0; }
uint32_t __attribute__((stdcall)) SetClipboardData(uint32_t f, uint32_t h) { (void)f; (void)h; return 0; }
uint32_t __attribute__((stdcall)) OpenClipboard(uint32_t hwnd) { (void)hwnd; return 0; }
uint32_t __attribute__((stdcall)) CloseClipboard(void) { return 0; }
uint32_t __attribute__((stdcall)) EmptyClipboard(void) { return 0; }
uint32_t __attribute__((stdcall)) IsClipboardFormatAvailable(uint32_t f) { (void)f; return 0; }

/* --- MessageBoxA --- */

/* Botones: 0 = OK. Devuelve IDOK (1) al hacer clic en OK o pulsar Enter.
 * MB_YESNO (0x4): botones "Si"/"No" -> IDYES (6) / IDNO (7); teclas
 * y/Y=Si, n/N=No, Enter=Si, Esc=No. Metapad comprueba eax==6 (IDYES)
 * para continuar una carga de archivo. */
int __attribute__((stdcall)) MessageBoxA(void *h, const char *text, const char *caption, uint32_t type)
{
    uint32_t info[4];
    uint32_t ev[5];
    int wx, wy, ww, wh;
    int tx, ty;
    int yesno = (type & 0x4) != 0;
    user32_button_t btn1, btn2;

    (void)h;
    if (sys_gfxinfo(info) != 0) {
        /* Sin modo grafico: fallback a consola. */
        console_print("[user32] sin framebuffer\n");
        return yesno ? 6 : 1;
    }
    lfb = (uint32_t *)info[0];
    scr_w = info[1];
    scr_h = info[2];
    if (text == 0)
        text = "";
    if (caption == 0)
        caption = "MessageBox";

    /* Marco de la ventana, centrada (400x190). */
    ww = 400;
    wh = 190;
    wx = ((int)scr_w - ww) / 2;
    wy = ((int)scr_h - wh) / 2;

    fillrect(wx - 3, wy - 3, ww + 6, wh + 6, COLOR_FRAME);   /* sombra */
    fillrect(wx, wy, ww, wh, COLOR_BG);
    fillrect(wx, wy, ww, 18, COLOR_TITLE);                   /* titulo */
    drawtext(wx + 6, wy + 2, caption, COLOR_TEXT);

    tx = wx + 16;
    ty = wy + 34;
    drawtext(tx, ty, text, COLOR_TEXT);
    ty += 20;
    if (yesno)
        drawtext(tx, ty, "Pulsa y/N o haz clic en un boton", COLOR_TEXT);
    else
        drawtext(tx, ty, "Haz clic en OK o pulsa Enter para cerrar", COLOR_TEXT);

    btn1.x = wx + ww / 2 - (yesno ? 75 : 30);
    btn1.y = wy + wh - 42;
    btn1.w = 60;
    btn1.h = 22;
    btn1.label = yesno ? "Si" : "OK";
    btn1.fg = COLOR_BTN_TX;
    btn1.bg = COLOR_BTN;
    btn1.fg_p = COLOR_BTN;
    btn1.bg_p = COLOR_TEXT;
    btn1.hovered = 0;
    btn1.pressed = 0;
    user32_draw_button(&btn1);
    if (yesno) {
        btn2 = btn1;
        btn2.x += 75;
        btn2.label = "No";
        user32_draw_button(&btn2);
    }

    /* Bucle de eventos (no bloqueante): clic sobre OK o Enter (EV_KEY)
     * cierran el dialogo. El scheduler desaloja el bucle ocupado por
     * tick, asi que no congela el resto del sistema. */
    for (;;) {
        if (sys_event(ev) != 0)
            continue;
        if (user32_button_feed(&btn1, ev))
            return yesno ? 6 : 1;
        if (yesno && user32_button_feed(&btn2, ev))
            return 7;
        if (ev[0] == EV_KEY && ev[4] == '\n')
            return yesno ? 6 : 1;
        if (ev[0] == EV_KEY && yesno) {
            if (ev[4] == 'y' || ev[4] == 'Y')
                return 6;
            if (ev[4] == 'n' || ev[4] == 'N')
                return 7;
            if (ev[4] == 27)
                return 7;
        }
    }
}

typedef struct {
    char     name[32];
    uint32_t fn;
} win32_export_t;

win32_export_t __exports[] __attribute__((section(".exports"))) = {
    { "MessageBoxA", (uint32_t)&MessageBoxA },
    { "LoadMenuA", (uint32_t)&LoadMenuA },
    { "CharLowerA", (uint32_t)&CharLowerA },
    { "CharLowerBuffA", (uint32_t)&CharLowerBuffA },
    { "CharUpperBuffA", (uint32_t)&CharUpperBuffA },
    { "CharToOemBuffA", (uint32_t)&CharToOemBuffA },
    { "OemToCharBuffA", (uint32_t)&OemToCharBuffA },
    { "IsCharAlphaA", (uint32_t)&IsCharAlphaA },
    { "IsCharAlphaNumericA", (uint32_t)&IsCharAlphaNumericA },
    { "IsCharLowerA", (uint32_t)&IsCharLowerA },
    { "IsCharUpperA", (uint32_t)&IsCharUpperA },
    { "RegisterClassA", (uint32_t)&RegisterClassA },
    { "CreateWindowExA", (uint32_t)&CreateWindowExA },
    { "ShowWindow", (uint32_t)&ShowWindow },
    { "UpdateWindow", (uint32_t)&UpdateWindow },
    { "DestroyWindow", (uint32_t)&DestroyWindow },
    { "IsWindow", (uint32_t)&IsWindow },
    { "GetMessageA", (uint32_t)&GetMessageA },
    { "PeekMessageA", (uint32_t)&PeekMessageA },
    { "TranslateMessage", (uint32_t)&TranslateMessage },
    { "DispatchMessageA", (uint32_t)&DispatchMessageA },
    { "PostMessageA", (uint32_t)&PostMessageA },
    { "PostQuitMessage", (uint32_t)&PostQuitMessage },
    { "SendMessageA", (uint32_t)&SendMessageA },
    { "SendDlgItemMessageA", (uint32_t)&SendDlgItemMessageA },
    { "DefWindowProcA", (uint32_t)&DefWindowProcA },
    { "CallWindowProcA", (uint32_t)&CallWindowProcA },
    { "GetDC", (uint32_t)&GetDC },
    { "ReleaseDC", (uint32_t)&ReleaseDC },
    { "FillRect", (uint32_t)&FillRect },
    { "BeginPaint", (uint32_t)&BeginPaint },
    { "EndPaint", (uint32_t)&EndPaint },
    { "SetFocus", (uint32_t)&SetFocus },
    { "SetWindowTextA", (uint32_t)&SetWindowTextA },
    { "GetWindowTextA", (uint32_t)&GetWindowTextA },
    { "GetWindowTextLengthA", (uint32_t)&GetWindowTextLengthA },
    { "GetWindowLongA", (uint32_t)&GetWindowLongA },
    { "SetWindowLongA", (uint32_t)&SetWindowLongA },
    { "SetClassLongA", (uint32_t)&SetClassLongA },
    { "SetWindowPos", (uint32_t)&SetWindowPos },
    { "GetWindowRect", (uint32_t)&GetWindowRect },
    { "GetClientRect", (uint32_t)&GetClientRect },
    { "GetWindowPlacement", (uint32_t)&GetWindowPlacement },
    { "GetCursorPos", (uint32_t)&GetCursorPos },
    { "SetCursor", (uint32_t)&SetCursor },
    { "GetParent", (uint32_t)&GetParent },
    { "EnableWindow", (uint32_t)&EnableWindow },
    { "GetKeyboardState", (uint32_t)&GetKeyboardState },
    { "SetKeyboardState", (uint32_t)&SetKeyboardState },
    { "IsDialogMessageA", (uint32_t)&IsDialogMessageA },
    { "RegisterWindowMessageA", (uint32_t)&RegisterWindowMessageA },
    { "MessageBeep", (uint32_t)&MessageBeep },
    { "LoadIconA", (uint32_t)&LoadIconA },
    { "LoadCursorA", (uint32_t)&LoadCursorA },
    { "LoadStringA", (uint32_t)&LoadStringA },
    { "wsprintfA", (uint32_t)&wsprintfA },
    { "GetSysColor", (uint32_t)&GetSysColor },
    { "GetSysColorBrush", (uint32_t)&GetSysColorBrush },
    { "SystemParametersInfoA", (uint32_t)&SystemParametersInfoA },
    { "GetDialogBaseUnits", (uint32_t)&GetDialogBaseUnits },
    { "GetDlgItem", (uint32_t)&GetDlgItem },
    { "GetDlgItemTextA", (uint32_t)&GetDlgItemTextA },
    { "SetDlgItemTextA", (uint32_t)&SetDlgItemTextA },
    { "DialogBoxParamA", (uint32_t)&DialogBoxParamA },
    { "CreateDialogParamA", (uint32_t)&CreateDialogParamA },
    { "EndDialog", (uint32_t)&EndDialog },
    { "TranslateAcceleratorA", (uint32_t)&TranslateAcceleratorA },
    { "LoadAcceleratorsA", (uint32_t)&LoadAcceleratorsA },
    { "CreateMenu", (uint32_t)&CreateMenu },
    { "GetMenu", (uint32_t)&GetMenu },
    { "SetMenu", (uint32_t)&SetMenu },
    { "DrawMenuBar", (uint32_t)&DrawMenuBar },
    { "DestroyMenu", (uint32_t)&DestroyMenu },
    { "GetSubMenu", (uint32_t)&GetSubMenu },
    { "GetMenuItemCount", (uint32_t)&GetMenuItemCount },
    { "GetMenuItemInfoA", (uint32_t)&GetMenuItemInfoA },
    { "SetMenuItemInfoA", (uint32_t)&SetMenuItemInfoA },
    { "InsertMenuItemA", (uint32_t)&InsertMenuItemA },
    { "DeleteMenu", (uint32_t)&DeleteMenu },
    { "EnableMenuItem", (uint32_t)&EnableMenuItem },
    { "CheckMenuRadioItem", (uint32_t)&CheckMenuRadioItem },
    { "TrackPopupMenuEx", (uint32_t)&TrackPopupMenuEx },
    { "InvalidateRect", (uint32_t)&InvalidateRect },
    { "RedrawWindow", (uint32_t)&RedrawWindow },
    { "EnableScrollBar", (uint32_t)&EnableScrollBar },
    { "GetScrollInfo", (uint32_t)&GetScrollInfo },
    { "GetClipboardData", (uint32_t)&GetClipboardData },
    { "SetClipboardData", (uint32_t)&SetClipboardData },
    { "OpenClipboard", (uint32_t)&OpenClipboard },
    { "CloseClipboard", (uint32_t)&CloseClipboard },
    { "EmptyClipboard", (uint32_t)&EmptyClipboard },
    { "IsClipboardFormatAvailable", (uint32_t)&IsClipboardFormatAvailable },
    { "", 0 },
};
