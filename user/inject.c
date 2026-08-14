/* MyOS - user/inject.c
 * Inyector de eventos sinteticos para tests headless (Fase 23-A3).
 * El raton del monitor de QEMU no inyecta PS/2 fiable, asi que esta
 * app (lanzada con Enter desde el explorer) empuja eventos
 * EV_MOVE/DOWN/UP por SYS_MOUSE_INJECT en la cola global del kernel;
 * el WM los enruta como eventos reales.
 *
 * Secuencia con esperas largas y marcadores al serial (el test del
 * host sincroniza los sendkeys con 'inj: hechoN'):
 *   espera ~12 s (margen para que el explorer lance metapad y cargue)
 *   clic1: explorer (200,445) - franja visible bajo metapad (y>440)
 *   espera ~7 s
 *   clic2: metapad (85,200) - franja izquierda (x 80..100)
 *   espera ~7 s
 *   clic3: boton X de metapad (676,43) -> WM_CLOSE */

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

/* Espera en ring 3. TCG: ~22.5M iteraciones/s, asi que wait_big ~= 11 s
 * (margen para que metapad cargue ~8 s tras el end+Enter del host) y
 * wait_med ~= 9 s (margen para que el host haga sus sendkeys). */
static void wait_big(void)
{
    volatile uint32_t n = 320000000;
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
    log_line("inj: start\n");

    wait_big();                 /* metapad debe cargar y estar encima */
    log_line("inj: clic1 explorer\n");
    inject(EV_MOVE, 200, 445);
    inject(EV_BUTTON_DOWN, 200, 445);
    inject(EV_BUTTON_UP, 200, 445);
    log_line("inj: hecho1\n");

    wait_med();
    log_line("inj: clic2 metapad\n");
    inject(EV_MOVE, 85, 200);
    inject(EV_BUTTON_DOWN, 85, 200);
    inject(EV_BUTTON_UP, 85, 200);
    log_line("inj: hecho2\n");

    wait_med();
    log_line("inj: clic3 X metapad\n");
    inject(EV_MOVE, 676, 43);
    inject(EV_BUTTON_DOWN, 676, 43);
    inject(EV_BUTTON_UP, 676, 43);
    log_line("inj: hecho3\n");

    log_line("inj: fin\n");
    return 0;
}