/* MyOS - kernel/mem/heap.c
 * Asignador first-fit con split (sin coalescing, suficiente para esta
 * fase). Cada bloque lleva un header de 16 bytes: magic, size (bytes
 * de payload), flag de libre y puntero al siguiente bloque.
 * El rango [HEAP_START, HEAP_START+HEAP_SIZE) se reserva en el PMM
 * durante heap_init() para que el PMM no lo entregue como frames. */

#include <stdint.h>
#include "heap.h"
#include "pmm.h"
#include "kprint.h"

#define HEAP_MAGIC 0x4D4F5301u      /* 'MOS' + 0x01 */
#define HEAP_ALIGN 8

typedef struct heap_header {
    uint32_t magic;
    uint32_t size;                  /* bytes de payload */
    uint8_t  free;
    uint8_t  pad[3];
    struct heap_header *next;
} heap_header_t;                    /* sizeof = 16 */

static heap_header_t *head;

void heap_init(void)
{
    pmm_reserve_range(HEAP_START, HEAP_SIZE);

    head = (heap_header_t *)HEAP_START;
    head->magic = HEAP_MAGIC;
    head->size = HEAP_SIZE - sizeof(heap_header_t);
    head->free = 1;
    head->next = 0;
}

void *kmalloc(uint32_t size)
{
    heap_header_t *h;

    if (size == 0)
        size = 1;
    size = (size + HEAP_ALIGN - 1) & ~(uint32_t)(HEAP_ALIGN - 1);

    for (h = head; h; h = h->next) {
        if (h->magic != HEAP_MAGIC || !h->free)
            continue;
        if (h->size < size)
            continue;

        /* split si queda sitio para otro bloque util (header + 8 B) */
        if (h->size >= size + sizeof(heap_header_t) + HEAP_ALIGN) {
            heap_header_t *rest = (heap_header_t *)((uint8_t *)h +
                                    sizeof(heap_header_t) + size);
            rest->magic = HEAP_MAGIC;
            rest->size = h->size - size - sizeof(heap_header_t);
            rest->free = 1;
            rest->next = h->next;
            h->size = size;
            h->next = rest;
        }
        h->free = 0;
        return (void *)((uint8_t *)h + sizeof(heap_header_t));
    }
    return 0;
}

void kfree(void *ptr)
{
    heap_header_t *h;

    if (!ptr)
        return;
    h = (heap_header_t *)((uint8_t *)ptr - sizeof(heap_header_t));
    if (h->magic != HEAP_MAGIC)
        return;                     /* puntero invalido: ignorar */
    h->free = 1;
    /* sin coalescing por ahora */
}

void heap_dump(void)
{
    heap_header_t *h;

    kprint("[heap] base=0x2000000 tam=0x400000 bloques:\n");
    for (h = head; h; h = h->next) {
        if (h->magic != HEAP_MAGIC) {
            kprint("[heap] BLOQUE CORRUPTO (magic perdido)\n");
            break;
        }
        kprint(h->free ? "  libre size=" : "  usado size=");
        kprint_uint(h->size);
        kprint(" payload=");
        kprint_hex32((uint32_t)((uint8_t *)h + sizeof(heap_header_t)));
        kprint("\n");
    }
}
