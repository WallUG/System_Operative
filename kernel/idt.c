/* MyOS - kernel/idt.c
 * Construye la IDT: 256 gates alineados a 8 bytes. Los handlers son los
 * stubs globales isr0..isr31 (excepciones) e irq0..irq15 (hardware). */

#include <stdint.h>
#include "idt.h"

typedef struct {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  zero;
    uint8_t  type_attr;
    uint16_t offset_high;
} __attribute__((packed)) idt_entry_t;

typedef struct {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed)) idt_ptr_t;

static idt_entry_t idt[256];
static idt_ptr_t   idt_ptr;

extern void idt_load(void *ptr);   /* en kernel/isr.asm: lidt [eax] */

/* Stubs de excepciones (kernel/isr.asm) */
#define DECL_ISR(n) extern void isr##n(void);
DECL_ISR(0)  DECL_ISR(1)  DECL_ISR(2)  DECL_ISR(3)  DECL_ISR(4)  DECL_ISR(5)
DECL_ISR(6)  DECL_ISR(7)  DECL_ISR(8)  DECL_ISR(9)  DECL_ISR(10) DECL_ISR(11)
DECL_ISR(12) DECL_ISR(13) DECL_ISR(14) DECL_ISR(15) DECL_ISR(16) DECL_ISR(17)
DECL_ISR(18) DECL_ISR(19) DECL_ISR(20) DECL_ISR(21) DECL_ISR(22) DECL_ISR(23)
DECL_ISR(24) DECL_ISR(25) DECL_ISR(26) DECL_ISR(27) DECL_ISR(28) DECL_ISR(29)
DECL_ISR(30) DECL_ISR(31)

/* Stubs de IRQ (kernel/isr.asm) */
#define DECL_IRQ(n) extern void irq##n(void);
DECL_IRQ(0)  DECL_IRQ(1)  DECL_IRQ(2)  DECL_IRQ(3)  DECL_IRQ(4)  DECL_IRQ(5)
DECL_IRQ(6)  DECL_IRQ(7)  DECL_IRQ(8)  DECL_IRQ(9)  DECL_IRQ(10) DECL_IRQ(11)
DECL_IRQ(12) DECL_IRQ(13) DECL_IRQ(14) DECL_IRQ(15)

static void idt_set_gate(uint8_t n, uint32_t handler, uint16_t selector,
                         uint8_t type_attr)
{
    idt[n].offset_low  = (uint16_t)(handler & 0xFFFF);
    idt[n].selector    = selector;
    idt[n].zero        = 0;
    idt[n].type_attr   = type_attr;
    idt[n].offset_high = (uint16_t)((handler >> 16) & 0xFFFF);
}

void idt_init(void)
{
    static const void *isrs[32] = {
        &isr0, &isr1, &isr2, &isr3, &isr4, &isr5, &isr6, &isr7,
        &isr8, &isr9, &isr10, &isr11, &isr12, &isr13, &isr14, &isr15,
        &isr16, &isr17, &isr18, &isr19, &isr20, &isr21, &isr22, &isr23,
        &isr24, &isr25, &isr26, &isr27, &isr28, &isr29, &isr30, &isr31
    };
    static const void *irqs[16] = {
        &irq0, &irq1, &irq2, &irq3, &irq4, &irq5, &irq6, &irq7,
        &irq8, &irq9, &irq10, &irq11, &irq12, &irq13, &irq14, &irq15
    };

    idt_ptr.limit = sizeof(idt) - 1;
    idt_ptr.base  = (uint32_t)&idt;

    for (int i = 0; i < 32; i++)
        idt_set_gate((uint8_t)i, (uint32_t)isrs[i], 0x08, IDT_GATE_INT);
    for (int i = 0; i < 16; i++)
        idt_set_gate((uint8_t)(32 + i), (uint32_t)irqs[i], 0x08, IDT_GATE_INT);

    idt_load(&idt_ptr);
}
