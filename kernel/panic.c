/* MyOS - kernel/panic.c
 * Salida de diagnostico ante excepciones de CPU no manejables.
 * El objetivo es que el error se vea (pantalla + serial) en vez de
 * reiniciar silenciosamente (triple fault). */

#include <stdint.h>
#include "panic.h"
#include "drivers/vga.h"
#include "drivers/serial.h"

static void append_dec(char *buf, int *pos, uint32_t n)
{
    char tmp[12];
    int k = 0;
    do {
        tmp[k++] = (char)('0' + n % 10);
        n /= 10;
    } while (n > 0);
    while (k > 0)
        buf[(*pos)++] = tmp[--k];
}

static void append_hex(char *buf, int *pos, uint32_t n)
{
    const char *d = "0123456789ABCDEF";
    buf[(*pos)++] = '0';
    buf[(*pos)++] = 'x';
    for (int s = 28; s >= 0; s -= 4)
        buf[(*pos)++] = d[(n >> s) & 0xF];
}

static void append_str(char *buf, int *pos, const char *s)
{
    while (*s)
        buf[(*pos)++] = *s++;
}

void kpanic(const char *name, registers_t *regs)
{
    char buf[96];
    int p = 0;

    /* Construir un unico bloque y emitirlo igual a VGA y COM1 */
    append_str(buf, &p, "\n*** KERNEL PANIC ***\nEvento: ");
    append_str(buf, &p, name);
    append_str(buf, &p, "\nint_no=");
    append_dec(buf, &p, regs->int_no);
    append_str(buf, &p, " err=");
    append_dec(buf, &p, regs->err_code);
    append_str(buf, &p, " eip=");
    append_hex(buf, &p, regs->eip);
    append_str(buf, &p, " cs=");
    append_dec(buf, &p, regs->cs);
    append_str(buf, &p, " eflags=");
    append_dec(buf, &p, regs->eflags);
    append_str(buf, &p, "\nSistema detenido.\n");
    buf[p] = '\0';

    vga_puts(buf);
    serial_puts(buf);

    for (;;) {
        __asm__ volatile("cli; hlt");
    }
}
