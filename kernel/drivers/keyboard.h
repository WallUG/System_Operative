/* MyOS - kernel/drivers/keyboard.h */
#ifndef MYOS_KEYBOARD_H
#define MYOS_KEYBOARD_H

/* Limpia el buffer (llamar antes de sti). */
void keyboard_init(void);
/* Llamado desde el IRQ1 handler: lee el scancode del puerto 0x60 y lo
 * encola traducido a ASCII en un buffer circular. */
void keyboard_irq(void);
/* Lee el siguiente caracter; -1 si el buffer esta vacio. */
int keyboard_read(void);
/* Descarta las teclas pendientes (Fase 22: al volver a la consola). */
void keyboard_flush(void);

#endif
