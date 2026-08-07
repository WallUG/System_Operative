/* MyOS - kernel/mem/mmap.c
 * Copia el mapa E820 del buffer del bootloader (0x7E00) a un array
 * estatico. Se asume RAM < 4 GiB para las sumas en uint64. */

#include <stdint.h>
#include "mmap.h"

#define MMAP_BUFFER_ADDR 0x7E00

static e820_entry_t entries[E820_MAX_ENTRIES];
static uint32_t count;

void mmap_init(void)
{
    uint32_t *cnt = (uint32_t *)MMAP_BUFFER_ADDR;
    count = *cnt;
    if (count > E820_MAX_ENTRIES)
        count = E820_MAX_ENTRIES;

    e820_entry_t *src = (e820_entry_t *)(MMAP_BUFFER_ADDR + 4);
    for (uint32_t i = 0; i < count; i++)
        entries[i] = src[i];
}

uint32_t e820_count(void)
{
    return count;
}

const e820_entry_t *e820_get(uint32_t i)
{
    if (i >= count)
        return 0;
    return &entries[i];
}

uint64_t mmap_total_usable(void)
{
    uint64_t total = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (entries[i].type == E820_USABLE) {
            total |= ((uint64_t)entries[i].len_high << 32);
            total += entries[i].len_low;
        }
    }
    return total;
}
