/* MyOS - kernel/gdt.c
 * GDT del kernel (Fase 6): reemplaza a la del bootloader (0x08/0x10
 * identicos) y anade segmentos de usuario (DPL=3) y la TSS.
 *
 *  0x00 NULL      0x08 code ring0  0x10 data ring0
 *  0x18 code ring3 (0x1B con RPL3)  0x20 data ring3 (0x23 con RPL3)
 *  0x28 TSS (esp0 = pila de kernel para interrupciones desde ring 3)
 *  0x30 FS user (0x33 con RPL3): base = WIN32_TIB_VA (TIB de mingw)
 *
 * El TSS se marca busy con ltr; la CPU lee ss0/esp0 al interrumpir una
 * tarea en ring 3 y cambia a la pila de kernel antes de entrar al stub. */

#include <stdint.h>
#include "gdt.h"
#include "io.h"
#include "win32.h"

#define GDT_COUNT 7
#define GDT_FS_USER 6           /* selector 0x33 (DPL 3) */

/* Descriptor de segmento/TSS de 8 bytes (layout Intel). */
typedef struct {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed)) gdt_entry_t;

typedef struct {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed)) gdt_ptr_t;

/* TSS de 32 bits: solo ss0/esp0 interesan (resto a 0). */
typedef struct {
    uint32_t link, esp0, ss0, esp1, ss1, esp2, ss2;
    uint32_t cr3, eip, eflags, eax, ecx, edx, ebx, esp, ebp, esi, edi;
    uint32_t es, cs, ss, ds, fs, gs, ldt;
    uint16_t trap, iomap;
} __attribute__((packed)) tss_t;

/* Pila de kernel dedicada: la usa la CPU al interrumpir ring 3 (esp0). */
static uint8_t tss_stack[16384] __attribute__((aligned(16)));
static tss_t tss;
static gdt_entry_t gdt[GDT_COUNT];
static gdt_ptr_t gdt_ptr;

extern void gdt_load(void *ptr);          /* lgdt [ptr]   (gdt_asm.asm) */
extern void tss_load(uint32_t selector);  /* ltr ax       (gdt_asm.asm) */

static void gdt_set(uint8_t idx, uint32_t base, uint32_t limit,
                    uint8_t access, uint8_t gran)
{
    gdt[idx].limit_low   = (uint16_t)(limit & 0xFFFF);
    gdt[idx].base_low    = (uint16_t)(base & 0xFFFF);
    gdt[idx].base_mid    = (uint8_t)((base >> 16) & 0xFF);
    gdt[idx].access      = access;
    gdt[idx].granularity = (uint8_t)(gran | ((limit >> 16) & 0x0F));
    gdt[idx].base_high   = (uint8_t)((base >> 24) & 0xFF);
}

void gdt_init(void)
{
    /* 0x08/0x10: identicos a la GDT del bootloader (plano, ring 0). */
    gdt_set(0, 0, 0, 0, 0);                    /* NULL */
    gdt_set(1, 0, 0xFFFFF, 0x9A, 0xC0);        /* code ring0, 4K gran, 32-bit */
    gdt_set(2, 0, 0xFFFFF, 0x92, 0xC0);        /* data ring0 */
    gdt_set(3, 0, 0xFFFFF, 0xFA, 0xC0);        /* code ring3 (DPL=3) */
    gdt_set(4, 0, 0xFFFFF, 0xF2, 0xC0);        /* data ring3 (DPL=3) */

    /* TSS: base real, limite = sizeof-1, present ring0, type 0x9 = 32-bit
     * available TSS (ltr lo marca busy). */
    tss.ss0 = 0x10;                            /* kernel data */
    tss.esp0 = (uint32_t)tss_stack + sizeof(tss_stack);
    gdt_set(5, (uint32_t)&tss, sizeof(tss) - 1, 0x89, 0x00);

    /* FS de usuario: segmento DPL3 con base = TIB (los CRTs de mingw
     * leen %fs:0x18 nada mas entrar). Todo el rango 0-4 GiB para que
     * [fs:0x18] de cada tarea apunte a SU TIB mapeado en WIN32_TIB_VA. */
    gdt_set(6, WIN32_TIB_VA, 0xFFFFF, 0xF2, 0xC0);

    gdt_ptr.limit = sizeof(gdt) - 1;
    gdt_ptr.base  = (uint32_t)&gdt;

    gdt_load(&gdt_ptr);
    tss_load(0x28);                            /* selector TSS (RPL 0) */

    /* FS global (ring 0 y ring 3: los stubs nunca lo tocan). */
    __asm__ volatile("movw $0x33, %%ax; movw %%ax, %%fs" : : : "ax", "memory");
}

void gdt_set_esp0(uint32_t esp0)
{
    /* La CPU cachea base/limite de la TSS en ltr, pero relee ss0/esp0
     * de memoria en cada interrupcion/syscall desde ring 3. */
    tss.esp0 = esp0;
}
