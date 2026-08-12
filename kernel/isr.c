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
#include "drivers/mouse.h"
#include "task/task.h"
#include "syscall.h"
#include "kprint.h"
#include "mem/paging.h"

/* Bucle de muerte de tareas (sti;hlt) en kernel/task/switch.asm. */
extern void task_stub_exit(void);

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
    if (regs->int_no == 0x80) {
        syscall_handler(regs);
        return;                     /* el epilogo del stub hace el iret */
    }
    if (regs->int_no == 14) {
        /* Page fault: CR2 tiene la direccion que causo el fallo */
        uint32_t cr2;
        __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
        if (regs->cs == 0x1B) {
            /* #PF de una tarea de usuario: proteccion de memoria en
             * accion. Matar la tarea y seguir con las demas. */
            uint32_t cr3;
            __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
            kprint("USER #PF en 0x");
            kprint_hex32(cr2);
            kprint(" eip=");
            kprint_hex32(regs->eip);
            kprint(" esp=");
            kprint_hex32(regs->user_esp);
            kprint(" cr3=");
            kprint_hex32(cr3);
            kprint(" err=");
            kprint_hex32(regs->err_code);
            kprint("\n");
            {
                uint32_t *pd = (uint32_t *)cr3;
                uint32_t pde = pd[(regs->eip >> 22) & 0x3FF];
                uint32_t pte = 0;
                kprint("  pde=");
                kprint_hex32(pde);
                kprint(" pte=");
                if (pde & 1) {
                    uint32_t ptva = pde & 0xFFFFF000u;
                    if (ptva >= 0x10000000u) {  /* frame fuera de rango */
                        kprint("?(pde)");
                    } else {
                        uint32_t *pt = (uint32_t *)ptva;
                        pte = pt[(regs->eip >> 12) & 0x3FF];
                        kprint_hex32(pte);
                    }
                } else kprint_hex32(pte);
                kprint(" pg0=");
                kprint_hex32(pd[0]);
                kprint("\n  frm+eip=");
                if (pte & 1) {
                    uint32_t fva = pte & 0xFFFFF000u;
                    if (fva < 0x10000000u && regs->eip >= 0x10000000u) {
                        volatile uint8_t *f =
                            (volatile uint8_t *)(fva + (regs->eip & 0xFFF));
                        int i;
                        for (i = 0; i < 16; i++)
                            kprint_hex32((uint32_t)f[i] << 24);
                    } else {
                        kprint("(no-mapped)");
                    }
                }
                kprint("\n");
            }
            kprint("  [eip]=");
            {
                uint32_t base = regs->eip & ~3u;
                int i;
                for (i = 0; i < 16; i++) {
                    uint8_t v = 0xEE;
                    if (base < 0x10000000u) {
                        volatile uint8_t *p = (volatile uint8_t *)base;
                        v = p[i];
                    } else {
                        uint32_t frame = paging_user_frame(cr3, base & ~0xFFFu);
                        if (frame != 0)
                            v = *(volatile uint8_t *)(frame + (base & 0xFFF) + i);
                        else
                            break;
                    }
                    kprint_hex32((uint32_t)v << 24);
                    kprint(" ");
                }
            }
            kprint("\n");
            {
                /* Stack de usuario: 12 dwords con walk manual de paginas */
                uint32_t sp = regs->user_esp;
                uint32_t *pd2 = (uint32_t *)cr3;
                int i;
                kprint("  [stack]");
                for (i = 0; i < 12; i++) {
                    uint32_t va = sp + (uint32_t)i * 4;
                    uint32_t pde2 = pd2[(va >> 22) & 0x3FF];
                    uint32_t val = 0xDEAD0000u + (uint32_t)i;
                    if (pde2 & 1) {
                        uint32_t ptva2 = pde2 & 0xFFFFF000u;
                        if (ptva2 < 0x10000000u) {
                            uint32_t *pt = (uint32_t *)ptva2;
                            uint32_t pte2 = pt[(va >> 12) & 0x3FF];
                            if (pte2 & 1) {
                                uint32_t fva2 = pte2 & 0xFFFFF000u;
                                if (fva2 < 0x10000000u) {
                                    uint8_t *f = (uint8_t *)
                                        (fva2 + (va & 0xFFF));
                                    val = (uint32_t)f[0] |
                                          ((uint32_t)f[1] << 8) |
                                          ((uint32_t)f[2] << 16) |
                                          ((uint32_t)f[3] << 24);
                                }
                            }
                        }
                    }
                    kprint_hex32(val);
                    kprint(" ");
                }
            }
            kprint("\n");
            kprint(" - tarea eliminada (proteccion de memoria)\n");
            sched_kill_current();
            /* iret a task_stub_exit en ring 0 (marco con cs=0x08). */
            regs->eip = (uint32_t)task_stub_exit;
            regs->cs = 0x08;
            regs->eflags = 0x202;
            regs->eax = 0;
            return;
        }
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
    else if (irq == 12)
        mouse_irq();                /* raton PS/2 (Fase 13) */
    /* IRQs 2-11, 13-15 sin driver aun */

    /* EOI ANTES del scheduler: sched_tick puede cambiar de pila y nunca
     * volver aqui (iret de la otra tarea); el PIC debe quedar liberado. */
    pic_send_eoi(irq);

    if (irq == 0)
        sched_tick(regs);       /* scheduler round-robin (Fase 5) */
}
