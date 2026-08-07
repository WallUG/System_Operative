/* MyOS - kernel/drivers/vga.h */
#ifndef MYOS_VGA_H
#define MYOS_VGA_H

void vga_init(void);
void vga_clear(void);
void vga_putc(char c);
void vga_puts(const char *s);

#endif
