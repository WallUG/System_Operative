/* MyOS - user/win32/user32.c
 * user32.dll: modulo Win32 fijo (ring 3) en 0xB0100000.
 * Fase 12: GUI minimo sobre el LFB VBE (800x600x32, mapeado por el
 * kernel en 0xA8000000 y consultable por SYS_GFXINFO).
 * MessageBoxA: dibuja una ventana (marco, titulo, texto, boton OK) en
 * el framebuffer y espera Enter (SYS_READ) para devolver IDOK. */

#include <stdint.h>

#define SYS_WRITE   7
#define SYS_GFXINFO 15
#define SYS_MOUSEINFO 16
#define SYS_EVENT   17

#define EV_MOVE         1
#define EV_BUTTON_DOWN  2
#define EV_BUTTON_UP    3
#define EV_KEY          4

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

static void putpixel(int x, int y, uint32_t c)
{
    if (x < 0 || y < 0 || x >= (int)scr_w || y >= (int)scr_h)
        return;
    lfb[y * scr_w + x] = c;
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

/* --- MessageBoxA --- */

/* Botones: 0 = OK. Devuelve IDOK (1) al hacer clic en OK o pulsar Enter. */
int MessageBoxA(void *h, const char *text, const char *caption, uint32_t type)
{
    uint32_t info[4];
    uint32_t ev[5];
    int wx, wy, ww, wh;
    int tx, ty;
    user32_button_t btn;

    (void)h;
    (void)type;
    if (sys_gfxinfo(info) != 0) {
        /* Sin modo grafico: fallback a consola. */
        console_print("[user32] sin framebuffer\n");
        return 1;
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
    drawtext(tx, ty, "Haz clic en OK o pulsa Enter para cerrar", COLOR_TEXT);

    btn.x = wx + ww / 2 - 30;
    btn.y = wy + wh - 42;
    btn.w = 60;
    btn.h = 22;
    btn.label = "OK";
    btn.fg = COLOR_BTN_TX;
    btn.bg = COLOR_BTN;
    btn.fg_p = COLOR_BTN;
    btn.bg_p = COLOR_TEXT;
    btn.hovered = 0;
    btn.pressed = 0;
    user32_draw_button(&btn);

    /* Bucle de eventos (no bloqueante): clic sobre OK o Enter (EV_KEY)
     * cierran el dialogo. El scheduler desaloja el bucle ocupado por
     * tick, asi que no congela el resto del sistema. */
    for (;;) {
        if (sys_event(ev) != 0)
            continue;
        if (user32_button_feed(&btn, ev))
            return 1;
        if (ev[0] == EV_KEY && ev[4] == '\n')
            return 1;
    }
}

typedef struct {
    char     name[32];
    uint32_t fn;
} win32_export_t;

win32_export_t __exports[] __attribute__((section(".exports"))) = {
    { "MessageBoxA", (uint32_t)&MessageBoxA },
    { "MyOS_PollEvent", (uint32_t)sys_event },
    { "MyOS_DrawButton", (uint32_t)user32_draw_button },
    { "MyOS_WidgetHit", (uint32_t)point_in_rect },
    { "MyOS_ButtonFeed", (uint32_t)user32_button_feed },
    { "", 0 },
};
