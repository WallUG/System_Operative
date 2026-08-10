/* MyOS - kernel/mem/pmm.h
 * Gestor de memoria fisica (PMM) con bitmap: un bit por frame de 4 KiB.
 * El bitmap vive en el .bss del kernel (variable estatica de pmm.c),
 * NUNCA en una direccion fija: el .bss del kernel (mods de win32,
 * sector_buf/entries de mefs, kbd_buffer, ...) ya cubre hasta 0x228A4
 * y una direccion fija como 0x20000 chocaria con esas variables.
 * Cubre hasta PMM_MAX_MEM (512 MiB).
 * Bit = 1 -> frame en uso; bit = 0 -> frame libre. */

#ifndef MYOS_PMM_H
#define MYOS_PMM_H

#include <stdint.h>

#define PMM_BLOCK_SIZE  4096
#define PMM_MAX_MEM     0x20000000  /* 512 MiB (0x20000 frames = 16 KiB) */
#define PMM_FIRST_FREE  0x100000    /* frames < 1 MiB quedan reservados */

void pmm_init(void);
uint32_t pmm_free_count(void);
uint32_t pmm_alloc_frame(void);     /* direccion fisica, 0 = sin memoria */
void pmm_free_frame(uint32_t addr);
void pmm_reserve_range(uint32_t base, uint32_t size);

#endif
