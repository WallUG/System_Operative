/* MyOS - kernel/mem/heap.h
 * Heap del kernel: first-fit con split en 0x2000000, 4 MiB.
 * El rango se reserva en el PMM para que nadie mas lo reparta. */

#ifndef MYOS_HEAP_H
#define MYOS_HEAP_H

#include <stdint.h>

#define HEAP_START 0x2000000        /* 32 MiB */
#define HEAP_SIZE  0x400000         /* 4 MiB */

void heap_init(void);
void *kmalloc(uint32_t size);       /* NULL si no hay hueco */
void kfree(void *ptr);
void heap_dump(void);               /* estado de todos los bloques */

#endif
