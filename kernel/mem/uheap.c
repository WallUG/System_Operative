/* MyOS - kernel/mem/uheap.c
 * Heap de usuario por proceso (Fase 23-C10): first-fit con split y
 * coalesce, sobre [USER_HEAP_BASE, USER_HEAP_END) con paginas USER
 * mapeadas bajo demanda (el bump de la task extiende la region).
 * Cada bloque lleva un header de 16 bytes (magic, size, free, next),
 * como el heap del kernel (kernel/mem/heap.c). El estado del heap
 * (cabeza de la lista) vive en la task; el contenido, en el PD del
 * proceso. */

#include <stdint.h>
#include "../task/task.h"
#include "../mem/paging.h"

#define UHEAP_MAGIC 0x4D4F5302u      /* 'MOS' + 0x02 */
#define UHEAP_ALIGN 8

typedef struct uhdr {
    uint32_t magic;
    uint32_t size;                  /* bytes de payload */
    uint8_t  free;
    uint8_t  pad[3];
    struct uhdr *next;
} uhdr_t;                           /* sizeof = 16 */

/* La primera llamada de la task crea el bloque inicial. */
void uheap_reset(void)
{
    sched_user_heap_head_set(0);
}

void *uheap_alloc(uint32_t pd, uint32_t size)
{
    uint32_t head = sched_user_heap_head();
    uhdr_t *h;

    if (size == 0)
        size = 1;
    size = (size + UHEAP_ALIGN - 1) & ~(uint32_t)(UHEAP_ALIGN - 1);

    if (head == 0) {
        if (paging_is_user(pd, USER_HEAP_BASE) == 0 &&
            paging_user_map(pd, USER_HEAP_BASE, 0x1000) != 0)
            return 0;
        h = (uhdr_t *)USER_HEAP_BASE;
        h->magic = UHEAP_MAGIC;
        h->size = 0;
        h->free = 1;
        h->next = 0;
        sched_user_heap_head_set(USER_HEAP_BASE);
        /* el bump se adelanta al bloque inicial (que no tiene payload)
         * para que la expansion no lo pise; se mantiene alineado a
         * pagina (el mapeo bajo demanda asume tope alineado) */
        sched_user_heap_set(PAGE_ALIGN(USER_HEAP_BASE +
                                       sizeof(uhdr_t)));
        head = USER_HEAP_BASE;
    }

    /* first-fit con split */
    for (h = (uhdr_t *)head; h; h = h->next) {
        if (h->magic != UHEAP_MAGIC || !h->free)
            continue;
        if (h->size < size)
            continue;
        if (h->size >= size + sizeof(uhdr_t) + UHEAP_ALIGN) {
            uhdr_t *rest = (uhdr_t *)((uint8_t *)h +
                                      sizeof(uhdr_t) + size);
            rest->magic = UHEAP_MAGIC;
            rest->size = h->size - size - sizeof(uhdr_t);
            rest->free = 1;
            rest->next = h->next;
            h->size = size;
            h->next = rest;
        }
        h->free = 0;
        return (void *)((uint8_t *)h + sizeof(uhdr_t));
    }

    /* no hay hueco: expandir la region con el bump */
    {
        uint32_t cur = sched_user_heap();
        uint32_t need = sizeof(uhdr_t) + size;
        uint32_t next = PAGE_ALIGN(cur + need);
        uhdr_t *last;

        if (next > USER_HEAP_END)
            return 0;
        if (paging_is_user(pd, cur) == 0 &&
            paging_user_map(pd, cur, next - cur) != 0)
            return 0;
        for (last = (uhdr_t *)head; last && last->next;
             last = last->next) ;
        if (last == 0 || last->magic != UHEAP_MAGIC)
            return 0;
        h = (uhdr_t *)cur;
        h->magic = UHEAP_MAGIC;
        h->size = size;
        h->free = 0;
        h->next = 0;
        last->next = h;
        sched_user_heap_set(next);
        return (void *)((uint8_t *)h + sizeof(uhdr_t));
    }
}

void uheap_free(uint32_t pd, void *ptr)
{
    uint32_t head = sched_user_heap_head();
    uhdr_t *h = (uhdr_t *)((uint8_t *)ptr - sizeof(uhdr_t));
    uhdr_t *prev, *cur;
    (void)pd;

    if (ptr == 0 || head == 0)
        return;
    if (h->magic != UHEAP_MAGIC)
        return;                     /* puntero invalido: ignorar */
    h->free = 1;

    /* coalesce con el siguiente */
    if (h->next && h->next->magic == UHEAP_MAGIC && h->next->free) {
        h->size += sizeof(uhdr_t) + h->next->size;
        h->next = h->next->next;
    }
    /* coalesce con el anterior */
    prev = 0;
    for (cur = (uhdr_t *)head; cur && cur->next != h; cur = cur->next)
        prev = cur;
    if (cur && cur->free) {
        cur->size += sizeof(uhdr_t) + h->size;
        cur->next = h->next;
    }
    (void)prev;
}

/* Tamano del payload del bloque (HeapSize). 0 si invalido. */
uint32_t uheap_size(void *ptr)
{
    uhdr_t *h = (uhdr_t *)((uint8_t *)ptr - sizeof(uhdr_t));
    if (ptr == 0 || h->magic != UHEAP_MAGIC)
        return 0;
    return h->size;
}