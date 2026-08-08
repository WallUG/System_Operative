/* MyOS - kernel/gdt.h */
#ifndef MYOS_GDT_H
#define MYOS_GDT_H

/* Selectores de la GDT del kernel (Fase 6). */
#define GDT_CODE_RING0  0x08
#define GDT_DATA_RING0  0x10
#define GDT_CODE_RING3  0x18    /* con RPL=3: 0x1B */
#define GDT_DATA_RING3  0x20    /* con RPL=3: 0x23 */
#define GDT_TSS_SEL     0x28

void gdt_init(void);
/* Cambia tss.esp0: la CPU lee ss0/esp0 de la TSS en memoria en cada
 * transicion a ring 0 (interrupcion/syscall), asi que cada tarea de
 * usuario necesita su pila de kernel propia antes de ser programada. */
void gdt_set_esp0(uint32_t esp0);

#endif
