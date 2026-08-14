/* MyOS - kernel/bootscreen.c
 * Pantalla de carga estilo Windows (Fase 22): fondo azul, titulo,
 * barra de progreso y texto de fase, dibujada directamente al LFB
 * durante el arranque del kernel. El log de boot sigue yendo al
 * serial (COM1) para debug/tests; al terminar, bootscreen_done()
 * limpia y deja la consola vgafx lista para el prompt. */

#include <stdint.h>
#include "drivers/vbe.h"
#include "drivers/vgafx.h"
#include "drivers/font8x16.h"

#define BS_W        VBE_SCREEN_W
#define BS_H        VBE_SCREEN_H

#define BS_BG       0x00204A80u   /* azul arranque */
#define BS_TITLE    0x00FFFFFFu   /* blanco */
#define BS_SUB      0x00B0D0F0u   /* azul claro */
#define BS_FRAME    0x00A0B8D0u   /* borde barra */
#define BS_FILL     0x0030B000u   /* verde progreso */
#define BS_FILL2    0x0070E000u   /* verde claro progreso */

static int bs_active = 0;

static void bs_px(int x, int y, uint32_t c)
{
    volatile uint32_t *lfb = (volatile uint32_t *)vbe_lfb_phys;
    if (x < 0 || x >= BS_W || y < 0 || y >= BS_H)
        return;
    lfb[y * BS_W + x] = c;
}

static void bs_fill(int x, int y, int w, int h, uint32_t c)
{
    int i, j;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            bs_px(x + i, y + j, c);
}

static void bs_text(int x, int y, const char *s, uint32_t c)
{
    while (s && *s) {
        unsigned char ch = (unsigned char)*s;
        const unsigned char *g;
        int i, j;
        if (ch < 32 || ch > 126)
            ch = '?';
        g = font8x16_basic[ch - 32];
        for (j = 0; j < 16; j++)
            for (i = 0; i < 8; i++)
                if (g[j] & (0x80u >> i))
                    bs_px(x + i, y + j, c);
        x += 8;
        s++;
    }
}

/* Barra + fondo base: dibuja una vez la pantalla estatica. */
static void bs_draw_base(void)
{
    bs_fill(0, 0, BS_W, BS_H, BS_BG);
    bs_text((BS_W - 10 * 8) / 2, 80, "MyOS 0.4.0", BS_TITLE);
    bs_text((BS_W - 36 * 8) / 2, 110, "Iniciando el sistema...", BS_SUB);
    bs_fill(100, 440, BS_W - 200, 4, BS_FRAME);     /* marco barra */
    bs_fill(100, 484, BS_W - 200, 2, BS_FRAME);
    bs_fill(100, 440, 4, 46, BS_FRAME);
    bs_fill(BS_W - 104, 440, 4, 46, BS_FRAME);
}

void bootscreen_start(void)
{
    bs_active = 1;
    bs_draw_base();
}

/* Texto de fase + relleno de la barra al pct% (0-100). El texto va
 * encima de la barra (y=410); la barra ocupa y=444..482. */
void bootscreen_status(const char *fase, int pct)
{
    char pct_s[8];
    int i, len = 0, p = pct;
    int x;

    if (!bs_active)
        return;

    if (pct > 100)
        pct = 100;

    /* texto de fase centrado, encima de la barra */
    bs_fill(104, 406, BS_W - 208, 26, BS_BG);
    while (fase && fase[len])
        len++;
    x = (BS_W - len * 8) / 2;
    bs_text(x, 408, fase, BS_SUB);

    /* pct% a la derecha del texto */
    pct_s[0] = (char)('0' + p / 100);
    pct_s[1] = (char)('0' + (p / 10) % 10);
    pct_s[2] = '%';
    pct_s[3] = 0;
    if (p >= 100)
        pct_s[0] = '1';
    else if (p < 10)
        pct_s[1] = (char)('0' + p);
    bs_text(x + len * 8 + 12, 408, pct_s, BS_TITLE);

    /* barra: relleno verde progresivo con punta verde claro */
    for (i = 0; i < (BS_W - 208) * pct / 100; i++) {
        for (int j = 0; j < 38; j++) {
            uint32_t c = (i < 2) ? BS_FILL2 : BS_FILL;
            bs_px(104 + i, 444 + j, c);
        }
    }
}

void bootscreen_done(void)
{
    bs_active = 0;
    vgafx_clear();
}

int bootscreen_active(void)
{
    return bs_active;
}