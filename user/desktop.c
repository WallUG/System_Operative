/* MyOS - user/desktop.c
 * Escritorio (Fase 17 del roadmap GUI): dos ventanas del WM creadas por
 * la app:
 *   - Fondo (WM_FLAG_BG | NOFRAME, pantalla completa): el wallpaper se
 *     pinta en el buffer de la app, no en el LFB; el WM lo compone
 *     siempra en z=0 y el snapshot del fondo sigue siendo la consola
 *     (al cerrar el escritorio, vuelve).
 *   - Barra de tareas (WM_FLAG_FIXED | NOFRAME, borde inferior) con un
 *     boton por app. Clic: fork + exec de la app (explorer.elf) en un
 *     proceso hijo con su propio PD; cuando cierra, el WM recomponre
 *     con el snapshot y el escritorio queda intacto.
 * 'q' cierra el escritorio (el WM restaura la consola). */

#include <stdint.h>
#include "winlib.h"

#define TB_Y   572            /* barra de tareas pegada al borde inferior */
#define TB_H   28

#define COLOR_DESK 0x00304860u    /* fondo del escritorio */
#define COLOR_STRIP 0x003C5C78u   /* franjas de fondo */
#define COLOR_HINT  0x00C0D0E0u
#define COLOR_TB_BG 0x00202838u   /* fondo de la barra de tareas */
#define COLOR_TB_ED 0x00406080u   /* borde superior de la barra */
#define COLOR_BTN   0x00406090u   /* boton normal */
#define COLOR_BTN_H 0x005078B0u   /* boton presionado */
#define COLOR_LBL   0x00FFFFFFu
#define COLOR_LBL_P 0x00E0F0FFu

static uint32_t scr_w, scr_h;

/* Dibuja un boton de la barra de tareas en el buffer de la taskbar
 * (pitch = scr_w). bx = posicion X en cliente. */
static void tb_button(uint32_t *tb, int bx, int lbw, const char *label,
                      int pressed)
{
    uint32_t bg = pressed ? COLOR_BTN_H : COLOR_BTN;
    uint32_t fg = pressed ? COLOR_LBL_P : COLOR_LBL;
    uint32_t lw = wl_strlen(label) * 8;

    wl_fillrect(tb, (int)scr_w, (int)scr_h, bx, 4, lbw, TB_H - 8, bg);
    wl_fillrect(tb, (int)scr_w, (int)scr_h, bx, 4, lbw, 1, COLOR_TB_ED);
    wl_drawtext(tb, (int)scr_w, (int)scr_h,
                bx + (lbw - (int)lw) / 2, 4 + (TB_H - 8 - 16) / 2, label, fg);
}

/* Wallpaper en el buffer del fondo (el WM lo compone en z=0; el snapshot
 * del LFB sigue siendo la consola). */
static void paint_wallpaper(uint32_t *bg, int bw, int bh)
{
    int x, y;

    wl_fillrect(bg, bw, bh, 0, 0, bw, bh, COLOR_DESK);
    for (x = 8; x < bw; x += 96)
        wl_fillrect(bg, bw, bh, x, 0, 2, bh, COLOR_STRIP);
    for (y = 8; y < bh; y += 96)
        wl_fillrect(bg, bw, bh, 0, y, bw, 1, COLOR_STRIP);
    wl_drawtext(bg, bw, bh, 16, 16, "MyOS desktop (Fase 17)", COLOR_LBL);
    wl_drawtext(bg, bw, bh, 16, 36,
                "Clic en la barra de tareas para lanzar una app; q cierra",
                COLOR_HINT);
}

int _start(void)
{
    uint32_t info[4], a[8], ev[5];
    uint32_t *bg, *tb;
    int idb, idt;

    sys_write("esc: escritorio iniciando\n", 26);
    if (sys_gfxinfo(info) != 0) {
        sys_write("esc: sin modo grafico\n", 22);
        return 1;
    }
    scr_w = info[1];
    scr_h = info[2];

    bg = (uint32_t *)sys_malloc(scr_w * scr_h * 4);
    tb = (uint32_t *)sys_malloc(scr_w * TB_H * 4);
    if (bg == (void *)0 || tb == (void *)0) {
        sys_write("esc: malloc fallo\n", 18);
        return 1;
    }
    paint_wallpaper(bg, (int)scr_w, (int)scr_h);

    /* Fondo: pantalla completa, siempre z=0, sin hit-test (los clics
     * sobre el escritorio van al foco). */
    a[0] = (uint32_t)"Fondo"; a[1] = 0; a[2] = 0;
    a[3] = scr_w; a[4] = scr_h; a[5] = (uint32_t)bg;
    a[6] = scr_w * scr_h * 4;
    a[7] = WM_FLAG_BG | WM_FLAG_NOFRAME;
    idb = sys_wincreate(a);
    if (idb < 0) {
        sys_write("esc: fondo fallo\n", 17);
        return 1;
    }

    wl_fillrect(tb, (int)scr_w, (int)scr_h, 0, 0,
                (int)scr_w, TB_H, COLOR_TB_BG);
    wl_fillrect(tb, (int)scr_w, (int)scr_h, 0, 0,
                (int)scr_w, 1, COLOR_TB_ED);
    wl_drawtext(tb, (int)scr_w, (int)scr_h, (int)scr_w - 66, 6,
                "q:salir", COLOR_HINT);
    tb_button(tb, 6, 128, "EXPLORADOR", 0);

    a[0] = (uint32_t)"Taskbar"; a[1] = 0; a[2] = TB_Y;
    a[3] = scr_w; a[4] = TB_H; a[5] = (uint32_t)tb;
    a[6] = scr_w * TB_H * 4;
    a[7] = WM_FLAG_FIXED | WM_FLAG_NOFRAME;
    idt = sys_wincreate(a);
    if (idt < 0) {
        sys_write("esc: taskbar fallo\n", 19);
        return 1;
    }
    sys_write("esc: escritorio listo\n", 22);

    for (;;) {
        int kid;
        if (sys_event(ev) != 0)
            continue;
        switch (ev[0]) {
        case EV_KEY:
            if (ev[4] == 'q')
                goto salir;
            break;
        case EV_BUTTON_DOWN:
            if (wl_point_in((int)ev[1], (int)ev[2], 6, TB_Y, 128, TB_H)) {
                tb_button(tb, 6, 128, "EXPLORADOR", 1);
                sys_winupdate(idt);
            }
            break;
        case EV_BUTTON_UP:
            if (wl_point_in((int)ev[1], (int)ev[2], 6, TB_Y, 128, TB_H)) {
                tb_button(tb, 6, 128, "EXPLORADOR", 0);
                sys_winupdate(idt);
                sys_write("esc: lanzando explorer.elf\n", 28);
                kid = sys_fork();
                if (kid == 0) {
                    /* Hijo: imagen nueva (exec libera el espacio
                     * heredado); el WM no le hereda ninguna ventana
                     * (viven en el kernel, del PD del padre). */
                    if (sys_exec("explorer.elf") != 0)
                        sys_write("esc: exec explorer.elf fallo\n", 30);
                    sys_exit(2);
                } else if (kid < 0) {
                    sys_write("esc: fork fallo\n", 16);
                }
            }
            break;
        }
    }

salir:
    sys_write("esc: fin del escritorio\n", 24);
    sys_winclose(idb);
    sys_winclose(idt);
    return 0;
}