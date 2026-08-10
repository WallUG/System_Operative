/* MyOS - kernel/drivers/mouse.h
 * Raton PS/2 (IRQ12), Fase 13. Driver minimo: init del dispositivo,
 * decodificacion del paquete de 3 bytes y cursor en el LFB. */

#ifndef MYOS_MOUSE_H
#define MYOS_MOUSE_H

void mouse_init(void);
void mouse_irq(void);
void mouse_draw_cursor(void);
int  mouse_read(int *x, int *y, int *buttons);

#endif
