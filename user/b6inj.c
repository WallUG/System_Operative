/* MyOS - user/b6inj.c
 * Inyector de eventos sinteticos para el test de la Fase 23-B6
 * (message loop real con routing por ventana). Se lanza desde el
 * explorer DESPUES de wintwo.exe; espera ~11 s (wintwo ya creo sus
 * ventanas) e inyecta un clic en la ventana A (260,222) y otro en la
 * ventana B (540,302) por SYS_MOUSE_INJECT, con marcadores al serial
 * para que el test del host sincronice. */
#include <stdint.h>
#include "winlib.h"

static void log_line(const char *s)
{
    sys_write(s, wl_strlen(s));
}

static void inject(int type, int x, int y)
{
    sys_mouse_inject(type, x, y, 1, 0);
}

static void wait_big(void)
{
    volatile uint32_t n = 900000000;
    while (n-- > 0)
        ;
}

static void wait_med(void)
{
    volatile uint32_t n = 200000000;
    while (n-- > 0)
        ;
}

int _start(void)
{
    log_line("b6i: start\n");
    wait_big();
    log_line("b6i: clickA\n");
    inject(EV_MOVE, 260, 222);
    inject(EV_BUTTON_DOWN, 260, 222);
    inject(EV_BUTTON_UP, 260, 222);
    wait_med();
    log_line("b6i: clickB\n");
    inject(EV_MOVE, 540, 302);
    inject(EV_BUTTON_DOWN, 540, 302);
    inject(EV_BUTTON_UP, 540, 302);
    wait_big();
    log_line("b6i: fin\n");
    return 0;
}