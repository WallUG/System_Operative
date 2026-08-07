/* MyOS - kernel/mem/mmap.h
 * Mapa de memoria E820: lo recoge el bootloader (int 0x15, EAX=0xE820)
 * en el buffer 0x7E00 (dword contador + entradas de 20 bytes).
 * Aqui se copia a un array estatico del kernel para su consulta. */

#ifndef MYOS_MMAP_H
#define MYOS_MMAP_H

#include <stdint.h>

#define E820_MAX_ENTRIES 32

typedef struct {
    uint32_t base_low;
    uint32_t base_high;
    uint32_t len_low;
    uint32_t len_high;
    uint32_t type;          /* 1 = usable, 2 = reservado, 3 = ACPI, 4 = NVS... */
} __attribute__((packed)) e820_entry_t;

#define E820_USABLE 1

void mmap_init(void);
uint32_t e820_count(void);
const e820_entry_t *e820_get(uint32_t i);
uint64_t mmap_total_usable(void);   /* suma de longitudes tipo 1 */

#endif
