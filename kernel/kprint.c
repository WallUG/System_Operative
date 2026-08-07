/* MyOS - kernel/kprint.c
 * Utilidades de impresion compartidas: VGA + COM1 simultaneos.
 * (Division inline de i386; sin helpers de libgcc.) */

#include <stdint.h>
#include "kprint.h"
#include "drivers/vga.h"
#include "drivers/serial.h"

void kprint_uint(uint32_t n)
{
    char tmp[12];
    int k = 0;
    do {
        tmp[k++] = (char)('0' + n % 10);
        n /= 10;
    } while (n > 0);
    while (k > 0) {
        k--;
        vga_putc(tmp[k]);
        serial_putc(tmp[k]);
    }
}
