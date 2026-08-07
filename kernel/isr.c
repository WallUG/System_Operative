/* MyOS - kernel/isr.c
 * Handlers en C invocados desde kernel/isr.asm.
 * - isr_handler: excepciones de CPU -> kpanic con diagnostico.
 * - irq_handler: EOI al PIC y despacho a los drivers por numero de IRQ. */

#include <stdint.h>
#include "idt.h"
#include "panic.h"
#include "pic.h"
#include "io.h"
#include "drivers/timer.h"
#include "drivers/keyboard.h"
#include "task/task.h"

static const char *const exception_names[32] = {
    "Division by zero", "Debug", "Non-maskable interrupt",
    "Breakpoint", "Overflow", "BOUND range exceeded",
    "Invalid opcode", "Device not available", "Double fault",
    "Coprocessor segment overrun", "Invalid TSS", "Segment not present",
    "Stack-segment fault", "General protection fault", "Page fault",
    "Reserved", "x87 FP error", "Alignment check", "Machine check",
    "SIMD FP exception", "Virtualization exception", "Control protection",
    "Reserved", "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved", "Reserved"
};

void isr_handler(registers_t *regs)
{
    if (regs->int_no == 14) {
        /* Page fault: CR2 tiene la direccion que causo el fallo */
        uint32_t cr2;
        __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
        kpanic_page_fault(cr2, regs);
    }
    if (regs->int_no < 32) {
        kpanic(exception_names[regs->int_no], regs);
    }
    /* vectores 32-47 no deberian llegar aqui (los captura irq_handler) */
}

void irq_handler(registers_t *regs)
{
    uint8_t irq = (uint8_t)(regs->int_no - 32);

    if (irq == 0)
        timer_tick();
    else if (irq == 1)
        keyboard_irq();
    /* IRQs 2-15 sin driver aun */

    /* EOI ANTES del scheduler: sched_tick puede cambiar de pila y nunca
     * volver aqui (iret de la otra tarea); el PIC debe quedar liberado. */
    pic_send_eoi(irq);

    if (irq == 0)
        sched_tick(regs);       /* scheduler round-robin (Fase 5) */
}
