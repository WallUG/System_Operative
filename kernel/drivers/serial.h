/* MyOS - kernel/drivers/serial.h */
#ifndef MYOS_SERIAL_H
#define MYOS_SERIAL_H

#define COM1 0x3F8

void serial_init(void);
void serial_putc(char c);
void serial_puts(const char *s);

/* RX: devuelve el caracter recibido en COM1 o -1 si no hay dato. */
int serial_read_char(void);

#endif
