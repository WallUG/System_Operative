/* MyOS - kernel/mem/paging.c
 * Habilita paginacion identity (virtual == fisica) para 0-1 GiB usando
 * paginas de 4 MiB (PSE). Con PSE el PD basta: cada entrada describe
 * una pagina de 4 MiB (bit PS=1). El frame del PD se reserva con
 * pmm_alloc_frame(), que solo devuelve memoria < 512 MiB (< 1 GiB),
 * por lo que el propio PD queda visible tras habilitar la paginacion. */

#include <stdint.h>
#include "paging.h"
#include "pmm.h"

#define CR4_PSE     0x10
#define CR0_PG      0x80000000u

#define PDE_PRESENT 0x1
#define PDE_RW      0x2
#define PDE_PS      0x80            /* page size 4 MiB */

static uint32_t pd_addr;

void paging_init(void)
{
    uint32_t *pd;
    uint32_t cr4, cr0;
    uint32_t i;

    pd_addr = pmm_alloc_frame();
    if (pd_addr == 0)
        return;                     /* PMM agotado: seguir sin paginacion */

    pd = (uint32_t *)pd_addr;
    for (i = 0; i < 1024; i++)
        pd[i] = 0;

    /* Identity map 0..1 GiB con paginas de 4 MiB */
    for (i = 0; i < PAGES_4MB; i++)
        pd[i] = (i << 22) | PDE_PRESENT | PDE_RW | PDE_PS;

    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= CR4_PSE;
    __asm__ volatile("mov %0, %%cr4" : : "r"(cr4));

    __asm__ volatile("mov %0, %%cr3" : : "r"(pd_addr));

    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= CR0_PG;
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0));
}

uint32_t paging_pd_addr(void)
{
    return pd_addr;
}
