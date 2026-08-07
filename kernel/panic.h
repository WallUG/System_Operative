/* MyOS - kernel/panic.h */
#ifndef MYOS_PANIC_H
#define MYOS_PANIC_H

#include "idt.h"

/* Imprime diagnostico de una excepcion/fallo y detiene el sistema
 * (cli + hlt). Recibe el nombre del evento y el marco de registros. */
void kpanic(const char *name, registers_t *regs) __attribute__((noreturn));

/* Variante especifica de page fault: incluye CR2 (direccion que fallo). */
void kpanic_page_fault(uint32_t cr2, registers_t *regs)
    __attribute__((noreturn));

#endif
