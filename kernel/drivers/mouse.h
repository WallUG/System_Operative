/* MyOS - kernel/drivers/mouse.h
 * Raton PS/2 (IRQ12), Fase 13. Driver minimo: init del dispositivo,
 * decodificacion del paquete de 3 bytes y cursor en el LFB.
 * Fase 14: cola FIFO global de eventos (un unico consumidor via SYS_EVENT). */

#ifndef MYOS_MOUSE_H
#define MYOS_MOUSE_H

#include <stdint.h>

#define EV_MOVE         1   /* movimiento: x, y, buttons            */
#define EV_BUTTON_DOWN  2   /* boton pulsado: x, y, buttons         */
#define EV_BUTTON_UP    3   /* boton soltado: x, y, buttons         */
#define EV_KEY          4   /* tecla del teclado: key = caracter    */

#define EV_QUEUE_MAX    64  /* tamano de las colas de eventos        */

typedef struct {
    int type;
    int x, y;           /* posicion del raton (EV_KEY: sin usar)   */
    int buttons;        /* botones del raton; en EV_KEY: mods (1=ctrl,2=alt,4=shift) */
    int key;            /* caracter (solo EV_KEY); 0x100+ para teclas especiales */
} mouse_event_t;

void mouse_init(void);
void mouse_irq(void);
void mouse_draw_cursor(void);
void mouse_cursor_invalidate(void);
int  mouse_read(int *x, int *y, int *buttons);
void mouse_event_push_key(int key);
void mouse_event_push_key_ext(int key, int mods);  /* mods: 1=ctrl 2=alt 4=shift */
int  mouse_event_dequeue(mouse_event_t *ev);

#endif
