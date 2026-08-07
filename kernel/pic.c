/* MyOS - kernel/pic.c
 * Programmable Interrupt Controller (8259A) dual.
 * Por defecto mapea las IRQ a los vectores 0x08-0x0F, colisionando con
 * las excepciones de CPU; hay que remapearlas a 0x20-0x2F (gates 32-47).
 * Deuda tecnica documentada en DESIGN.md: APIC/IOAPIC para SMP/moderno. */

#include <stdint.h>
#include "pic.h"
#include "io.h"

#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1

#define ICW1_INIT       0x11   /* modo init + espera ICW4 */
#define ICW1_ICW4       0x01
#define ICW3_MASTER_S2  0x04   /* cascada: el maestro tiene el esclavo en IRQ2 */
#define ICW3_SLAVE_S2   0x02   /* el esclavo cuelga del IRQ2 */
#define ICW4_8086       0x01   /* modo 8086 */

void pic_remap(void)
{
    outb(PIC1_CMD, ICW1_INIT);
    outb(PIC2_CMD, ICW1_INIT);
    outb(PIC1_DATA, 0x20);     /* ICW2: IRQ0-7  -> vectores 0x20-0x27 */
    outb(PIC2_DATA, 0x28);     /* ICW2: IRQ8-15 -> vectores 0x28-0x2F */
    outb(PIC1_DATA, ICW3_MASTER_S2);
    outb(PIC2_DATA, ICW3_SLAVE_S2);
    outb(PIC1_DATA, ICW4_8086);
    outb(PIC2_DATA, ICW4_8086);
    outb(PIC1_DATA, 0x00);     /* desenmascarar todo */
    outb(PIC2_DATA, 0x00);
}

void pic_send_eoi(uint8_t irq)
{
    if (irq >= 8)
        outb(PIC2_CMD, 0x20);  /* EOI al esclavo (IRQ8-15) */
    outb(PIC1_CMD, 0x20);      /* EOI al maestro (siempre) */
}
