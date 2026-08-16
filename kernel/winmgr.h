/* MyOS - kernel/winmgr.h
 * Gestor de ventanas en el kernel (Fase 16, opcion B elegida por el
 * usuario). Ventanas con backing buffer en el espacio de usuario de la
 * app (ella pinta su area cliente) y composicion centralizada: el
 * kernel dibuja marco + titulo + boton X y blitea el cliente al LFB
 * en orden z. El kernel consume los eventos de arrastre y raise; el
 * resto se enruta a la app duena de la ventana (Fase 17: colas de
 * eventos por PD para multitarea grafica). */

#ifndef MYOS_WINMGR_H
#define MYOS_WINMGR_H

#include <stdint.h>
#include "drivers/mouse.h"

#define WM_MAX_WINS   8
#define WM_TITLE_H    20          /* alto de la barra de titulo          */
#define WM_MENU_H     20          /* alto de la barra de menu (si hay)   */
#define WM_TOOLBAR_H  22          /* alto de la barra de herramientas    */
#define WM_FRAME      2           /* borde de la ventana                 */
#define WM_X_BTN      16          /* boton X: 16x16, arriba a la derecha */

/* Evento entregado a la app al pulsar el boton X: key = win id.
 * La app decide cerrarse con SYS_WINCLOSE. */
#define EV_WINCLOSE   5

/* Flags de SYS_WINCREATE (campo flags del struct de 32 bytes). */
#define WM_FLAG_FIXED    0x1      /* siempre arriba (taskbar); sin drag  */
#define WM_FLAG_NOFRAME  0x2      /* sin marco/titulo/X: el cliente      */
                                  /* ocupa todo el rect de la ventana    */
#define WM_FLAG_BG       0x4      /* fondo del escritorio: siempre z=0,  */
                                  /* sin raise/foco y opaca al hit-test  */
                                  /* (los clics pasan al foco)           */

/* Resultados de wm_route(). */
#define WM_ROUTE_RAW      0       /* sin ventanas: entregar al llamador  */
#define WM_ROUTE_CONSUMED -1      /* el WM lo consume (drag)             */
#define WM_ROUTE_TO_PD    1       /* enrutado a la cola del PD devuelto  */

int wm_create(const char *title, int x, int y, int w, int h,
              uint32_t buf_va, uint32_t buf_sz, uint32_t flags,
              uint32_t pd);
int wm_close(int id, uint32_t pd);   /* solo la app duena puede cerrar */
int wm_move(int id, int dx, int dy);
int wm_update(int id);
/* Fase 20-D: actualiza solo el rect (blit por regiones). rect =
 * {x,y,w,h} en coordenadas del cliente de la ventana. */
int wm_update_rect(int id, const int32_t *rect);
void wm_redraw_rect(int rx, int ry, int rw, int rh);
int wm_vis(int id, int on);           /* Fase 24-P4: mostrar/ocultar   */
int wm_find_by_pid(uint32_t pid);     /* Fase 24-P4: id de la ventana  */
int wm_set_title(int id, const char *title);
int wm_info(int id, uint32_t *out);

/* Barra de menu (Fase D): on=1 activa la franja de menu con los labels
 * top-level en flat (cadena con NULs: "File\0Edit\0...\0\0"), on=0 la
 * quita. Reajusta el area cliente y recompone. */
int wm_set_menu(int id, int on, const char *flat);

/* Barra de herramientas (Fase 20-B): activa una franja de botones bajo
 * el menu. flat = etiquetas de los botones NUL-separadas ("\0\0" final).
 * Reajusta el area cliente y recompone. */
int wm_set_toolbar(int id, int on, const char *flat);

/* Limpieza al morir una tarea (Fase 17): retira todas sus ventanas,
 * cancela un drag en curso si era suyo, libera la cola de eventos de su
 * PD y, si era la ultima ventana, restaura la consola y suelta el
 * snapshot de fondo. La invoca sched_kill_current antes de liberar el
 * espacio de usuario. */
void wm_cleanup_pd(uint32_t pd);

/* 1 si hay ventanas visibles (la consola vgafx se suprime en pantalla). */
int wm_has_windows(void);

/* Enrutamiento de un evento (Fase 17): consume el drag, transforma el
 * boton X en EV_WINCLOSE y devuelve WM_ROUTE_TO_PD con el PD destino
 * (dueno de la ventana bajo el raton / foco) o WM_ROUTE_* sin ventanas.
 * Los eventos enrutados se encolan con wm_event_deliver() y cada app
 * los retira de su propia cola con wm_event_claim(). */
int  wm_route(mouse_event_t *ev);
void wm_event_deliver(uint32_t pd, const mouse_event_t *ev);
int  wm_event_claim(uint32_t pd, mouse_event_t *ev);

#endif
