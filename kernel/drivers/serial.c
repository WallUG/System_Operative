/* MyOS - kernel/drivers/serial.c
 * UART 16550 en COM1, inicializado a 115200 8N1.
 * Util para debug headless: en QEMU, -serial stdio la muestra en stdout. */

#include <stdint.h>
#include "serial.h"
#include "io.h"

static void serial_wait_thr_empty(void)
{
    while (!(inb(COM1 + 5) & 0x20))  /* LSR bit 5: THR vacio */
        ;
}

void serial_init(void)
{
    outb(COM1 + 1, 0x00);           /* sin interrupciones */
    outb(COM1 + 3, 0x80);           /* DLAB = 1 */
    outb(COM1 + 0, 0x01);           /* divisor 1 -> 115200 */
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);           /* 8N1, DLAB = 0 */
    outb(COM1 + 2, 0xC7);           /* FIFO 14 bytes */
    outb(COM1 + 4, 0x0B);           /* DTR + RTS + OUT2 */
}

void serial_putc(char c)
{
    serial_wait_thr_empty();
    outb(COM1, (uint8_t)c);
}

void serial_puts(const char *s)
{
    while (*s)
        serial_putc(*s++);
}
