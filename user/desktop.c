/* MyOS - user/desktop.c
 * Escritorio (Fase 17 + Fase 21): dos ventanas del WM creadas por la app:
 *   - Fondo (WM_FLAG_BG | NOFRAME, pantalla completa): wallpaper.
 *   - Barra de tareas (WM_FLAG_FIXED | NOFRAME, borde inferior) con un
 *     boton por app (Fase 21): clic lanza la app (fork + exec) como
 *     proceso hijo con su propio PD. Se lanzan tanto apps nativas
 *     (.elf) como apps Win32 reales (.exe) — el kernel las distingue
 *     automaticamente (PE vs ELF).
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

#define MAX_APPS    4

typedef struct {
    const char *label;
    const char *file;       /* .elf nativo o .exe Win32 */
} app_t;

static const app_t apps[MAX_APPS] = {
    { "EXPLORADOR",  "explorer.elf"   },
    { "METAPAD",     "metapad.exe"    },
    { "MENSAJE",     "messagebox.exe" },
    { "DEMO",        "win_demo.elf"   },
};

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

/* Wallpaper en el buffer del fondo (el WM lo compone en z=0). */
static void paint_wallpaper(uint32_t *bg, int bw, int bh)
{
    int x, y;

    wl_fillrect(bg, bw, bh, 0, 0, bw, bh, COLOR_DESK);
    for (x = 8; x < bw; x += 96)
        wl_fillrect(bg, bw, bh, x, 0, 2, bh, COLOR_STRIP);
    for (y = 8; y < bh; y += 96)
        wl_fillrect(bg, bw, bh, 0, y, bw, 1, COLOR_STRIP);
    wl_drawtext(bg, bw, bh, 16, 16, "MyOS desktop (Fase 21)", COLOR_LBL);
    wl_drawtext(bg, bw, bh, 16, 36,
                "1-4 lanzan app, clic en la barra, q cierra", COLOR_HINT);
}

/* Lanza la app i-esima como hijo (fork + exec). */
static void launch_app(int i)
{
    int kid;
    char l[64];
    const char *pre = "esc: lanzando ";
    uint32_t p = 0, k = 0;

    while (*pre)
        l[p++] = *pre++;
    while (apps[i].file[k] && k < 30 && p < 60)
        l[p++] = apps[i].file[k++];
    l[p++] = '\n';
    l[p] = 0;
    sys_write(l, p);

    kid = sys_fork();
    if (kid == 0) {
        /* Hijo: imagen nueva (exec libera el espacio heredado). */
        if (sys_exec(apps[i].file) != 0) {
            sys_write("esc: exec fallo\n", 16);
        }
        sys_exit(2);
    } else if (kid < 0) {
        sys_write("esc: fork fallo\n", 16);
    }
}

int _start(void)
{
    uint32_t info[4], a[8], ev[5];
    uint32_t *bg, *tb;
    int idb, idt, i;

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

    /* Fondo: pantalla completa, siempre z=0, sin hit-test. */
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
    for (i = 0; i < MAX_APPS; i++)
        tb_button(tb, 6 + i * 136, 128, apps[i].label, 0);

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
    {
        char d[40];
        uint32_t p = 0;
        const char *pre = "esc: ventanas idb=";
        while (*pre)
            d[p++] = *pre++;
        {
            char n[12];
            uint32_t sl = wl_dec(n, (uint32_t)idb);
            uint32_t k;
            for (k = 0; k < sl; k++)
                d[p++] = n[k];
        }
        d[p++] = ' '; d[p++] = 'i'; d[p++] = 'd'; d[p++] = 't'; d[p++] = '=';
        {
            char n[12];
            uint32_t sl = wl_dec(n, (uint32_t)idt);
            uint32_t k;
            for (k = 0; k < sl; k++)
                d[p++] = n[k];
        }
        d[p++] = '\n'; d[p] = 0;
        sys_write(d, p);
    }

    for (;;) {
        int ai = -1;

        if (sys_event(ev) != 0)
            continue;
        switch (ev[0]) {
        case EV_KEY:
            if (ev[4] == 'q')
                goto salir;
            if (ev[4] >= '1' && ev[4] <= '0' + MAX_APPS)
                launch_app((int)(ev[4] - '1'));
            break;
        case EV_BUTTON_DOWN:
            for (i = 0; i < MAX_APPS; i++)
                if (wl_point_in((int)ev[1], (int)ev[2],
                                6 + i * 136, TB_Y, 128, TB_H))
                    ai = i;
            if (ai >= 0) {
                tb_button(tb, 6 + ai * 136, 128, apps[ai].label, 1);
                sys_winupdate(idt);
            }
            break;
        case EV_BUTTON_UP:
            for (i = 0; i < MAX_APPS; i++)
                if (wl_point_in((int)ev[1], (int)ev[2],
                                6 + i * 136, TB_Y, 128, TB_H))
                    ai = i;
            if (ai >= 0) {
                tb_button(tb, 6 + ai * 136, 128, apps[ai].label, 0);
                sys_winupdate(idt);
                launch_app(ai);
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