/* MyOS - kernel/kmain.c
 * Punto de entrada del kernel en C (llamado desde kernel/entry.asm).
 * C freestanding: sin libc del host, solo <stdint.h>/<stddef.h>.
 * Nota: se evita la division entera (necesitaria __udivsi3 de libgcc);
 * hasta la Fase 4 se usa aritmetica de shifts (hex). */

#include <stdint.h>
#include "drivers/vga.h"
#include "drivers/serial.h"
#include "string.h"

/* Imprime un entero sin signo en hexadecimal (0x..). */
static void print_hex(uint32_t n)
{
    const char *digits = "0123456789ABCDEF";
    vga_puts("0x");
    for (int shift = 28; shift >= 0; shift -= 4)
        vga_putc(digits[(n >> shift) & 0xF]);
}

void kmain(void)
{
    char dst[16];

    serial_init();
    vga_init();

    vga_puts("MyOS 0.2.0 - kmain() en C freestanding\n");
    serial_puts("MyOS 0.2.0 - kmain() en C freestanding\n");

    /* Evidencia de la mini-libc y del layout de memoria */
    vga_puts("mini-libc: strlen(\"freestanding\") = ");
    print_hex((uint32_t)strlen("freestanding"));
    vga_puts(" (0xB = 11)\n");

    memcpy(dst, "memcpy funciona", 15);
    dst[15] = '\0';
    vga_puts(dst);
    vga_puts("\n");

    vga_puts("Kernel en 0x10000, pila en .bss, VGA 80x25 + COM1 115200\n");
    serial_puts("mini-libc + VGA + COM1 OK. Fase 3: IDT\n");

    for (;;) {
        __asm__ volatile("hlt");
    }
}
