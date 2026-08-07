# Fase 3 — Interrupciones y Excepciones (IDT)

## Objetivo
Sin manejo de interrupciones, cualquier excepción de CPU (división por cero, page fault, acceso inválido) provoca un **triple fault** y reinicia la máquina virtual sin ningún mensaje. Esta fase es la que da al kernel capacidad de reaccionar a hardware (teclado, timer) y errores.

## Estructura de la IDT

256 entradas, cada una de 16 bytes en modo long (8 bytes en modo protegido de 32 bits). Las primeras 32 (0–31) son excepciones de CPU reservadas por Intel; 32–47 típicamente IRQs de hardware remapeadas; el resto disponibles para interrupciones de software (syscalls, p. ej. `int 0x80`).

```c
// idt.h
typedef struct {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  ist;
    uint8_t  type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
} __attribute__((packed)) idt_entry_t;

typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) idt_ptr_t;
```

```c
// idt.c
static idt_entry_t idt[256];
static idt_ptr_t idt_ptr;

void idt_set_gate(int n, uint64_t handler, uint16_t selector, uint8_t type_attr) {
    idt[n].offset_low  = handler & 0xFFFF;
    idt[n].selector    = selector;
    idt[n].ist         = 0;
    idt[n].type_attr   = type_attr;   // 0x8E = interrupt gate presente, ring 0
    idt[n].offset_mid  = (handler >> 16) & 0xFFFF;
    idt[n].offset_high = (handler >> 32) & 0xFFFFFFFF;
    idt[n].zero        = 0;
}

extern void idt_load(uint64_t);   // en ASM: lidt [rdi]

void idt_init(void) {
    idt_ptr.limit = sizeof(idt) - 1;
    idt_ptr.base  = (uint64_t)&idt;
    // registrar gates de excepciones (isr0..isr31) e IRQs (irq0..irq15) aquí
    idt_load((uint64_t)&idt_ptr);
}
```

## Stubs de ISR/IRQ en ASM

La CPU invoca el handler directamente en modo kernel; el ASM debe guardar registros, llamar al handler en C, y restaurar registros con `iretq`. Las excepciones que "empujan" un código de error (8, 10-14, 17) requieren tratamiento especial para no desalinear la pila.

```nasm
%macro ISR_NOERR 1
global isr%1
isr%1:
    push 0        ; código de error dummy
    push %1
    jmp isr_common_stub
%endmacro

%macro ISR_ERR 1
global isr%1
isr%1:
    push %1
    jmp isr_common_stub
%endmacro

extern isr_handler
isr_common_stub:
    pusha           ; (o push de r8-r15/rax..rdx en 64 bits)
    call isr_handler
    popa
    add esp, 8      ; limpiar código de error + número de interrupción
    iretq
```

Registra 0–31 con la macro correspondiente según si Intel define código de error para esa excepción (ver tabla en osdev.org "Exceptions"), y 32–47 para IRQs vía la macro IRQ análoga.

## PIC (8259) — remapeo obligatorio

Por defecto el PIC mapea IRQs a los vectores 0x08–0x0F, que colisionan con las excepciones de CPU. Hay que remapearlo a 0x20–0x2F antes de habilitar interrupciones:

```c
void pic_remap(void) {
    outb(0x20, 0x11); outb(0xA0, 0x11);   // ICW1: modo init
    outb(0x21, 0x20); outb(0xA1, 0x28);   // ICW2: offsets (0x20 y 0x28)
    outb(0x21, 0x04); outb(0xA1, 0x02);   // ICW3: cascada
    outb(0x21, 0x01); outb(0xA1, 0x01);   // ICW4: modo 8086
    outb(0x21, 0x0);  outb(0xA1, 0x0);    // desenmascarar todo
}
```

Para hardware moderno/multiprocesador se usa APIC/IOAPIC en vez de PIC, pero el PIC es suficiente y más simple para un primer OS funcional; documenta esto como deuda técnica si se omite APIC.

## Handler en C (despacho)

```c
void isr_handler(registers_t regs) {
    if (regs.int_no < 32) {
        // excepción de CPU: imprimir diagnóstico y detener (o manejar page fault, etc.)
        panic("CPU exception", regs.int_no, regs.err_code);
    }
}

void irq_handler(registers_t regs) {
    if (regs.int_no >= 40) outb(0xA0, 0x20);   // EOI al PIC esclavo si aplica
    outb(0x20, 0x20);                            // EOI al PIC maestro
    if (irq_routines[regs.int_no - 32]) irq_routines[regs.int_no - 32](&regs);
}
```

**Nunca olvides el EOI (End Of Interrupt)** — sin él, el PIC deja de entregar nuevas interrupciones de esa línea (o de todas, según el caso) y el sistema parece "colgado" sin razón aparente.

## Primeros drivers a partir de la IDT: timer y teclado

- **PIT (timer, IRQ0)**: configurar el divisor de frecuencia (puerto 0x43/0x40) para generar un tick periódico; útil como base del scheduler (Fase 5).
- **Teclado (IRQ1)**: leer scancode desde el puerto 0x60 en el handler, traducirlo con una tabla scancode→ASCII, y colocarlo en un buffer circular que el resto del kernel consulta.

## Checklist antes de avanzar a Fase 4
- [ ] Una excepción provocada intencionalmente (p. ej. división por cero) es capturada por el handler y muestra diagnóstico en vez de reiniciar la VM
- [ ] El PIC está remapeado y las IRQ 0 y 1 (timer, teclado) generan interrupciones visibles (contador incrementando, teclas impresas en pantalla)
- [ ] `sti` se ejecuta solo después de que la IDT y el PIC estén completamente configurados
