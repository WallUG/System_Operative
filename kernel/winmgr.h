/* MyOS - kernel/winmgr.h
 * Gestor de ventanas en el kernel (Fase 16, opcion B elegida por el
 * usuario). Ventanas con backing buffer en el espacio de usuario de la
 * app (ella pinta su area cliente) y composicion centralizada: el
 * kernel dibuja marco + titulo + boton X y blitea el cliente al LFB
 * en orden z. El kernel consume los eventos de arrastre y raise; el
 * resto se entrega a la app por SYS_EVENT. */

#ifndef MYOS_WINMGR_H
#define MYOS_WINMGR_H

#include <stdint.h>
#include "drivers/mouse.h"

#define WM_MAX_WINS   8
#define WM_TITLE_H    20          /* alto de la barra de titulo          */
#define WM_FRAME      2           /* borde de la ventana                 */
#define WM_X_BTN      16          /* boton X: 16x16, arriba a la derecha */

/* Evento entregado a la app al pulsar el boton X: key = win id.
 * La app decide cerrarse con SYS_WINCLOSE. */
#define EV_WINCLOSE   5

int wm_create(const char *title, int x, int y, int w, int h,
              uint32_t buf_va, uint32_t buf_sz, uint32_t pd);
int wm_close(int id);
int wm_move(int id, int dx, int dy);
int wm_update(int id);
int wm_info(int id, uint32_t *out);
int wm_filter_event(mouse_event_t *ev, uint32_t pd);

#endif
