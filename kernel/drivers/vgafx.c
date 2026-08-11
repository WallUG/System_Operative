/* MyOS - kernel/drivers/vgafx.c
 * Consola grafica sobre el LFB VBE (800x600x32, formato 0x00RRGGBB,
 * igual que user32.dll). Cuando VBE esta activo el kernel no puede usar
 * el buffer de texto 0xB8000 (no se muestra en modo grafico), asi que
 * todo el texto de kprint/shell se dibuja aqui con la fuente VGA 8x16
 * (la misma que usa user32, ver tools/font2c.py). */

#include <stdint.h>
#include "vgafx.h"
#include "vbe.h"
#include "string.h"
#include "font8x16.h"
#include "winmgr.h"

#define COLOR_BG       0x00000000u
#define COLOR_FG       0x00FFFFFFu

static int vgafx_row = 0;
static int vgafx_col = 0;

/* Mientras el WM tiene ventanas (escritorio), la consola NO pinta en el
 * LFB (el escritorio manda sobre la pantalla); el texto sigue yendose
 * por serial intacto. Al cerrarse la ultima ventana el cursor continua
 * donde estaba y la siguiente linea vuelve a aparecer. */
static int vgafx_suppressed(void)
{
    return wm_has_windows();
}

static void vgafx_putglyph(int x, int y, char c, uint32_t fg, uint32_t bg)
{
    volatile uint32_t *lfb = (volatile uint32_t *)vbe_lfb_phys;
    const unsigned char *g = font8x16_basic[c - 32];
    int i, j;

    for (j = 0; j < 16; j++) {
        unsigned char bits = g[j];
        for (i = 0; i < 8; i++) {
            uint32_t color = (bits & (0x80u >> i)) ? fg : bg;
            lfb[(y + j) * VBE_SCREEN_W + (x + i)] = color;
        }
    }
}

void vgafx_clear(void)
{
    volatile uint32_t *lfb = (volatile uint32_t *)vbe_lfb_phys;
    uint32_t i, npix = VBE_SCREEN_W * VBE_SCREEN_H;

    for (i = 0; i < npix; i++)
        lfb[i] = COLOR_BG;
    vgafx_row = 0;
    vgafx_col = 0;
}

static void vgafx_scroll(void)
{
    volatile uint32_t *lfb = (volatile uint32_t *)vbe_lfb_phys;
    int row, y;

    for (row = 0; row < VGAFX_ROWS - 1; row++) {
        for (y = 0; y < 16; y++)
            memcpy((void *)(lfb + (row * 16 + y) * VBE_SCREEN_W),
                   (const void *)(lfb + ((row + 1) * 16 + y) * VBE_SCREEN_W),
                   VBE_SCREEN_W * 4);
    }
    for (y = 0; y < 16; y++) {
        uint32_t *base = (uint32_t *)(lfb + ((VGAFX_ROWS - 1) * 16 + y)
                                      * VBE_SCREEN_W);
        for (int i = 0; i < VBE_SCREEN_W; i++)
            base[i] = COLOR_BG;
    }
}

void vgafx_putc(char c)
{
    if (vgafx_suppressed())
        return;
    switch (c) {
    case '\n':
        vgafx_row++;
        vgafx_col = 0;
        break;
    case '\r':
        vgafx_col = 0;
        return;
    case '\b':
        if (vgafx_col > 0)
            vgafx_col--;
        return;
    default:
        vgafx_putglyph(vgafx_col * 8, vgafx_row * 16, c,
                       COLOR_FG, COLOR_BG);
        vgafx_col++;
        if (vgafx_col >= VGAFX_COLS) {
            vgafx_col = 0;
            vgafx_row++;
        }
        break;
    }
    if (vgafx_row >= VGAFX_ROWS) {
        vgafx_scroll();
        vgafx_row = VGAFX_ROWS - 1;
    }
}

void vgafx_init(void)
{
    vgafx_clear();
}
