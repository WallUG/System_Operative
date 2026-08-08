/* MyOS - kernel/idt.h
 * Tabla de descriptores de interrupcion (IDT) de 32 bits.
 * Cada gate son 8 bytes: offset bajo, selector, 0, atributos, offset alto.
 * Los primeros 32 vectores son excepciones de CPU; 32-47 IRQs del PIC
 * remapeadas; el resto disponibles (p. ej. syscalls). */

#ifndef MYOS_IDT_H
#define MYOS_IDT_H

#include <stdint.h>

/* Marco de registros que reciben los handlers en C desde isr.asm.
 * Layout exacto de la pila en los stubs: pusha; push es; push ds
 * -> orden (baja a alta direccion): ds, es, edi..eax (pusha), int_no,
 *    err_code, eip, cs, eflags (empujados por la CPU). */
typedef struct {
    uint32_t ds;                    /* offset 0: pusheado ultimo */
    uint32_t es;
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;  /* pusha (orden CPU) */
    uint32_t int_no;                /* numero de vector */
    uint32_t err_code;              /* error code (0 si no aplica) */
    uint32_t eip, cs, eflags;       /* marco empujado por la CPU */
    uint32_t user_esp, user_ss;     /* solo si cambio de ring */
} registers_t;

#define IDT_GATE_INT 0x8E           /* interrupt gate, presente, ring 0, 32-bit */
#define IDT_GATE_INT_DPL3 0xEE      /* idem pero accesible desde ring 3 */

void idt_init(void);
/* Registra un handler de software (p. ej. int 0x80) accesible desde
 * ring 3. Llamar despues de idt_init. */
void idt_register_dpl3(uint8_t n, uint32_t handler);

#endif
