/* MyOS - kernel/drivers/serial.c
 * UART 16550 en COM1, inicializado a 115200 8N1.
 * Util para debug headless: en QEMU, -serial stdio la muestra en stdout. */

#include <stdint.h>
#include "serial.h"
#include "io.h"

static void serial_wait_thr_empty(void)
{
    /* Tope de reintentos: en QEMU el chardev puede aplicar backpressure
     * (socket lleno -> THRE a 0 durante mucho tiempo). Un poll infinito
     * con IF=0 (int 0x80 es interrupt gate) congelaria el kernel. Con
     * tope, el caracter se descarta y se sigue; en hardware real THRE
     * se activa en microsegundos (1 sola pasada). */
    int spins = 200000;
    while (!(inb(COM1 + 5) & 0x20) && --spins > 0)  /* LSR bit 5: THR vacio */
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

int serial_read_char(void)
{
    /* LSR bit 0 = RX data ready (sin bloqueo: no RX IRQ habilitado) */
    if (!(inb(COM1 + 5) & 0x01))
        return -1;
    return inb(COM1) & 0xFF;
}
