/* MyOS - user/win32/user32.c
 * user32.dll: modulo Win32 fijo (ring 3) en 0xB0100000.
 * Fase 12: GUI minimo sobre el LFB VBE (800x600x32, mapeado por el
 * kernel en 0xA8000000 y consultable por SYS_GFXINFO).
 * MessageBoxA: dibuja una ventana (marco, titulo, texto, boton OK) en
 * el framebuffer y espera Enter (SYS_READ) para devolver IDOK. */

#include <stdint.h>

#define SYS_WRITE   7
#define SYS_READ    6
#define SYS_GFXINFO 15

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

static int sys_read(char *buf, uint32_t max)
{
    int r;
    __asm__ volatile("int $0x80"
                     : "=a"(r)
                     : "a"(SYS_READ), "b"(buf), "c"(max)
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

/* --- MessageBoxA --- */

/* Botones: 0 = OK. Devuelve IDOK (1) al pulsar Enter. */
int MessageBoxA(void *h, const char *text, const char *caption, uint32_t type)
{
    uint32_t info[4];
    char key[2];
    int wx, wy, ww, wh;
    int tx, ty;

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
    drawtext(tx, ty, "Pulsa Enter para cerrar", COLOR_TEXT);

    fillrect(wx + ww / 2 - 30, wy + wh - 42, 60, 22, COLOR_BTN);
    drawtext(wx + ww / 2 - 13, wy + wh - 40, "OK", COLOR_BTN_TX);

    /* Espera Enter (SYS_READ: teclado o serial). SYS_READ devuelve 0
     * si la linea fue vacia (solo Enter): ambas cierran el dialogo. */
    for (;;) {
        if (sys_read(key, sizeof(key)) >= 0)
            break;
    }
    return 1;                       /* IDOK */
}

typedef struct {
    char     name[32];
    uint32_t fn;
} win32_export_t;

win32_export_t __exports[] __attribute__((section(".exports"))) = {
    { "MessageBoxA", (uint32_t)&MessageBoxA },
    { "", 0 },
};
