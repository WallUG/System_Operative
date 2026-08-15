/* MyOS - kernel/mem/uheap.h
 * Heap de usuario por proceso (Fase 23-C10). */

#ifndef MYOS_UHEAP_H
#define MYOS_UHEAP_H

#include <stdint.h>

void    uheap_reset(void);
void   *uheap_alloc(uint32_t pd, uint32_t size);   /* NULL si no cabe */
void    uheap_free(uint32_t pd, void *ptr);
uint32_t uheap_size(void *ptr);

#endif
