/* MyOS - kernel/mem/paging.h
 * Paginacion i386 con PSE: paginas de 4 MiB mapeando identity 0-1 GiB.
 * El PD se pide al PMM (un frame) y es valido por ser identity map. */

#ifndef MYOS_PAGING_H
#define MYOS_PAGING_H

#include <stdint.h>

#define PAGE_SIZE_4MB   0x400000
#define PAGES_4MB       256         /* 256 x 4 MiB = 1 GiB identity */

void paging_init(void);
uint32_t paging_pd_addr(void);

#endif
