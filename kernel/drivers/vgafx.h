/* MyOS - kernel/drivers/vgafx.h
 * Consola grafica sobre el LFB VBE (800x600x32): dibuja el texto del
 * kernel/shell en el framebuffer cuando el modo grafico esta activo.
 * Fase 12 (el modo texto 0xB8000 no se ve en modo grafico). */
#ifndef MYOS_VGAFX_H
#define MYOS_VGAFX_H

/* Dimensiones en celdas: 800/8 x 600/16. */
#define VGAFX_COLS      100
#define VGAFX_ROWS      37

void vgafx_init(void);
void vgafx_clear(void);
void vgafx_putc(char c);

#endif
