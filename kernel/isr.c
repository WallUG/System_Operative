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

    pic_send_eoi(irq);
}
