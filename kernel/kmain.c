/* MyOS - kernel/kmain.c
 * Punto de entrada del kernel en C (llamado desde kernel/entry.asm).
 * Fase 3: IDT + PIC remapeado + PIT (IRQ0) + teclado (IRQ1).
 * Demo: 1) status del subsistema de interrupciones; 2) ventana de 3
 * segundos de teclado (echo); 3) excepcion #DE intencional -> kpanic. */

#include <stdint.h>
#include "io.h"
#include "idt.h"
#include "pic.h"
#include "kprint.h"
#include "drivers/vga.h"
#include "drivers/serial.h"
#include "drivers/timer.h"
#include "drivers/keyboard.h"
#include "string.h"

void kmain(void)
{
    serial_init();
    vga_init();

    idt_init();
    pic_remap();
    timer_init(100);            /* 100 ticks/segundo */
    keyboard_init();

    vga_puts("MyOS 0.3.0 - interrupciones activas\n");
    serial_puts("MyOS 0.3.0 - interrupciones activas\n");
    vga_puts("IDT: 256 gates (excepciones 0-31, IRQ 32-47)\n");
    serial_puts("IDT: 256 gates (excepciones 0-31, IRQ 32-47)\n");
    vga_puts("PIC remapeado: IRQ0-15 -> vectores 0x20-0x2F\n");
    serial_puts("PIC remapeado: IRQ0-15 -> vectores 0x20-0x2F\n");
    vga_puts("PIT: 100 Hz. Teclado: PS/2 IRQ1.\n");
    serial_puts("PIT: 100 Hz. Teclado: PS/2 IRQ1.\n");

    sti();                      /* solo ahora: IDT y PIC listos */

    /* --- Demo 1: PIT. Espera ~1s y muestra el tick counter --- */
    {
        uint32_t start = timer_get_ticks();
        while (timer_get_ticks() - start < 100)
            halt();             /* las IRQ despiertan el hlt */
        vga_puts("PIT OK: 100 ticks en 1 s. ticks=");
        serial_puts("PIT OK: 100 ticks en 1 s. ticks=");
        kprint_uint(timer_get_ticks());
        vga_puts("\n");
        serial_puts("OK\n");
    }

    /* --- Demo 2: teclado. Ventana de 3 s; echo de lo pulsado --- */
    {
        uint32_t start = timer_get_ticks();
        int keys = 0;
        vga_puts("Ventana de teclado (3 s) - pulsa algo...\n");
        serial_puts("Ventana de teclado (3 s)...\n");
        while (timer_get_ticks() - start < 300) {
            int c = keyboard_read();
            if (c >= 0) {
                vga_putc((char)c);
                serial_putc((char)c);
                keys++;
            }
            halt();
        }
        vga_puts("\nTeclas recibidas: ");
        serial_puts("\nTeclas recibidas: ");
        kprint_uint((uint32_t)keys);
        vga_puts("\n");
        serial_puts("\n");
    }

    /* --- Demo 3: excepcion intencional (#DE = division by zero).
     * 100 / 0 compila a divl inline; la CPU lanza vector 0 y la IDT lo
     * captura: kpanic muestra el diagnostico y detiene el sistema.
     * Nota: el resultado se imprime para evitar que -O2 elimine la
     * division como codigo muerto. --- */
    vga_puts("Provocando #DE: division por cero...\n");
    serial_puts("Provocando #DE: division por cero...\n");
    {
        volatile uint32_t zero = 0;
        uint32_t r = 100u / zero;   /* nunca deberia retornar */
        kprint_uint(r);              /* inalcanzable: #DE dispara antes */
    }

    for (;;) {
        halt();
    }
}
