/* MyOS - kernel/mem/pmm.c
 * Bitmap de frames fisicos. Inicializa todo como "en uso" y despues
 * libera las regiones tipo 1 (usable) del E820 a partir de 1 MiB.
 * Lo que queda reservado de serie: < 1 MiB (IVT, BIOS, video, kernel,
 * buffer E820 y el propio bitmap) y el rango del heap (ver heap.c). */

#include <stdint.h>
#include "pmm.h"
#include "mmap.h"

static uint8_t *bitmap = (uint8_t *)PMM_BITMAP_ADDR;
static uint32_t max_frames = PMM_MAX_MEM / PMM_BLOCK_SIZE;   /* 0x20000 */
static uint32_t free_frames;

static inline void bit_set(uint32_t frame)
{
    bitmap[frame / 8] |= (uint8_t)(1 << (frame % 8));
}

static inline void bit_clear(uint32_t frame)
{
    bitmap[frame / 8] &= (uint8_t)~(1 << (frame % 8));
}

static inline int bit_test(uint32_t frame)
{
    return (bitmap[frame / 8] >> (frame % 8)) & 1;
}

void pmm_init(void)
{
    uint32_t i;

    for (i = 0; i < max_frames; i++)
        bit_set(i);
    free_frames = 0;

    for (i = 0; i < e820_count(); i++) {
        const e820_entry_t *e = e820_get(i);
        uint32_t base, len;
        uint32_t f0, f1, f;

        if (e->type != E820_USABLE || e->base_high != 0)
            continue;
        base = e->base_low;
        len = e->len_low;

        /* Solo nos interesa lo que hay a partir de 1 MiB */
        if (base < PMM_FIRST_FREE) {
            uint32_t cut = PMM_FIRST_FREE - base;
            if (len <= cut)
                continue;
            base += cut;
            len -= cut;
        }
        if (base >= PMM_MAX_MEM)
            continue;
        if (base + len > PMM_MAX_MEM)
            len = PMM_MAX_MEM - base;

        f0 = base / PMM_BLOCK_SIZE;
        f1 = (base + len - 1) / PMM_BLOCK_SIZE;   /* inclusive */
        for (f = f0; f <= f1; f++) {
            if (!bit_test(f))
                continue;           /* ya libre: no duplicar contador */
            bit_clear(f);
            free_frames++;
        }
    }
}

uint32_t pmm_free_count(void)
{
    return free_frames;
}

uint32_t pmm_alloc_frame(void)
{
    uint32_t i;
    for (i = 0; i < max_frames; i++) {
        if (!bit_test(i)) {
            bit_set(i);
            free_frames--;
            return i * PMM_BLOCK_SIZE;
        }
    }
    return 0;   /* sin memoria */
}

void pmm_free_frame(uint32_t addr)
{
    uint32_t f = addr / PMM_BLOCK_SIZE;
    if (f >= max_frames)
        return;
    if (!bit_test(f))
        return;             /* ya estaba libre: no tocar */
    bit_clear(f);
    free_frames++;
}

void pmm_reserve_range(uint32_t base, uint32_t size)
{
    uint32_t f0 = base / PMM_BLOCK_SIZE;
    uint32_t f1 = (base + size - 1) / PMM_BLOCK_SIZE;
    uint32_t f;

    for (f = f0; f <= f1 && f < max_frames; f++) {
        if (!bit_test(f))
            free_frames--;
        bit_set(f);
    }
}
