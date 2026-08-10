/* MyOS - kernel/mem/paging.c
 * Paginacion con proteccion de memoria (Fase 7).
 *
 * PD global del kernel: identity 0-1 GiB con paginas de 4 MiB (PSE),
 * supervisor (bit USER apagado: ring 3 nunca puede acceder). La region
 * 1 GiB-4 GiB queda sin mapear.
 *
 * PDs de usuario: copian los PDEs del kernel y usan paginas de 4 KiB
 * (PDEs 512-767 -> PTs asignadas perezosamente) con bits USER/RW para
 * el codigo/pila del programa en 0x80000000-0xBFFFFFFF.
 *
 * Como el kernel ve todo el rango 0-1 GiB por identity map, las
 * operaciones sobre paginas de usuario (copiar en fork, cargar un
 * segmento ELF) se hacen por la direccion fisica del frame. */

#include <stdint.h>
#include <string.h>
#include "paging.h"
#include "pmm.h"

#define CR4_PSE     0x10
#define CR0_PG      0x80000000u

#define PDE_PRESENT 0x001
#define PDE_RW      0x002
#define PDE_USER    0x004
#define PDE_PS      0x080

#define PT_COUNT    1024

static uint32_t kernel_pd_addr;

void paging_init(void)
{
    uint32_t *pd;
    uint32_t cr4, cr0;
    uint32_t i;

    kernel_pd_addr = pmm_alloc_frame();
    if (kernel_pd_addr == 0)
        return;                     /* PMM agotado: seguir sin paginacion */

    pd = (uint32_t *)kernel_pd_addr;
    for (i = 0; i < PT_COUNT; i++)
        pd[i] = 0;

    /* Identity 0-1 GiB, paginas de 4 MiB (PSE), SIN bit USER: el
     * kernel queda aislado de ring 3. */
    for (i = 0; i < KERNEL_PDE_END; i++)
        pd[i] = (i << 22) | PDE_PRESENT | PDE_RW | PDE_PS;

    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= CR4_PSE;
    __asm__ volatile("mov %0, %%cr4" : : "r"(cr4));

    __asm__ volatile("mov %0, %%cr3" : : "r"(kernel_pd_addr));

    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= CR0_PG;
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0));
}

uint32_t paging_kernel_pd(void)
{
    return kernel_pd_addr;
}

uint32_t paging_pd_addr(void)
{
    return kernel_pd_addr;
}

void paging_switch(uint32_t pd)
{
    __asm__ volatile("mov %0, %%cr3" : : "r"(pd) : "memory");
}

/* --- LFB VBE (Fase 12) --- */

void paging_map_kernel_lfb(void)
{
    uint32_t *pd = (uint32_t *)paging_kernel_pd();

    if (vbe_lfb_phys == 0)
        return;
    pd[vbe_lfb_phys >> 22] = vbe_lfb_phys | PDE_PRESENT | PDE_RW | PDE_PS;
}

int paging_is_lfb_frame(uint32_t frame)
{
    return vbe_lfb_phys != 0 &&
           frame >= vbe_lfb_phys &&
           frame < vbe_lfb_phys + VBE_LFB_PAGES * PAGE_SIZE;
}

int paging_user_map_lfb(uint32_t pd)
{
    uint32_t i;

    if (vbe_lfb_phys == 0)
        return 0;
    for (i = 0; i < VBE_LFB_PAGES; i++) {
        if (paging_user_map_frame(pd, VBE_LFB_USER_VA + i * PAGE_SIZE,
                                  vbe_lfb_phys + i * PAGE_SIZE) != 0)
            return -1;
    }
    return 0;
}

static int in_user_range(uint32_t vaddr, uint32_t size)
{
    return vaddr >= USER_VADDR_BASE && size <= USER_VADDR_END - vaddr &&
           (vaddr & 0xFFF) == 0 && (size & 0xFFF) == 0;
}

static int user_pde(uint32_t vaddr)
{
    return vaddr >> 22;
}

/* Devuelve la PT de un PDE de usuario, creandola (a cero) si falta. */
static uint32_t *get_or_create_pt(uint32_t *pd, uint32_t vaddr, int create)
{
    uint32_t *pt;
    uint32_t pde = user_pde(vaddr);
    uint32_t pt_addr;

    if (pd[pde] & PDE_PRESENT) {
        pt_addr = pd[pde] & 0xFFFFF000u;
        return (uint32_t *)pt_addr;
    }
    if (!create)
        return NULL;

    pt_addr = pmm_alloc_frame();
    if (pt_addr == 0)
        return NULL;
    pt = (uint32_t *)pt_addr;
    memset(pt, 0, PAGE_SIZE);
    pd[pde] = pt_addr | PDE_PRESENT | PDE_RW | PDE_USER;
    return pt;
}

uint32_t paging_create_user_pd(void)
{
    uint32_t pd_addr = pmm_alloc_frame();
    uint32_t *pd, *kpd;
    uint32_t i;

    if (pd_addr == 0)
        return 0;
    pd = (uint32_t *)pd_addr;
    memset(pd, 0, PAGE_SIZE);

    /* Copiar la identidad del kernel (supervisor, PSE) y dejar las
     * PTs de usuario vacias. */
    kpd = (uint32_t *)kernel_pd_addr;
    for (i = 0; i < KERNEL_PDE_END; i++)
        pd[i] = kpd[i];

    /* Heredar tambien la superpagina del LFB (0xFD000000): el kernel
     * escribe ahi (consola grafica vgafx) con el CR3 del proceso
     * durante los syscalls. */
    if (vbe_lfb_phys != 0)
        pd[vbe_lfb_phys >> 22] = kpd[vbe_lfb_phys >> 22];

    return pd_addr;
}

static int map_page(uint32_t pd, uint32_t vaddr, uint32_t frame)
{
    uint32_t *pt;
    uint32_t pte_idx = (vaddr >> 12) & 0x3FF;

    if (!in_user_range(vaddr, PAGE_SIZE) || (frame & 0xFFF) != 0)
        return -1;
    pt = get_or_create_pt((uint32_t *)pd, vaddr, 1);
    if (pt == NULL)
        return -1;
    if (pt[pte_idx] & PDE_PRESENT)
        return -1;                  /* ya mapeada: no sobreescribir */
    pt[pte_idx] = frame | PDE_PRESENT | PDE_RW | PDE_USER;
    return 0;
}

int paging_user_map(uint32_t pd, uint32_t vaddr, uint32_t size)
{
    uint32_t a;
    uint32_t frame;

    if (!in_user_range(vaddr, size))
        return -1;
    for (a = vaddr; a < vaddr + size; a += PAGE_SIZE) {
        frame = pmm_alloc_frame();
        if (frame == 0)
            return -1;
        memset((void *)frame, 0, PAGE_SIZE);
        if (map_page(pd, a, frame) != 0) {
            pmm_free_frame(frame);
            return -1;
        }
    }
    return 0;
}

int paging_user_map_frame(uint32_t pd, uint32_t vaddr, uint32_t frame)
{
    return map_page(pd, vaddr, frame);
}

uint32_t paging_user_frame(uint32_t pd, uint32_t vaddr)
{
    uint32_t *pt;
    uint32_t pte_idx;

    if (!in_user_range(vaddr, PAGE_SIZE))
        return 0;
    pt = get_or_create_pt((uint32_t *)pd, vaddr, 0);
    if (pt == NULL)
        return 0;
    pte_idx = (vaddr >> 12) & 0x3FF;
    if (!(pt[pte_idx] & PDE_PRESENT))
        return 0;
    return pt[pte_idx] & 0xFFFFF000u;
}

int paging_is_user(uint32_t pd, uint32_t vaddr)
{
    uint32_t *pt;
    uint32_t pte_idx;

    if (vaddr < USER_VADDR_BASE || vaddr >= USER_VADDR_END)
        return 0;
    pt = get_or_create_pt((uint32_t *)pd, vaddr, 0);
    if (pt == NULL)
        return 0;
    pte_idx = (vaddr >> 12) & 0x3FF;
    return (pt[pte_idx] & (PDE_PRESENT | PDE_USER)) == (PDE_PRESENT | PDE_USER);
}

int paging_copy_user_space(uint32_t dst_pd, uint32_t src_pd)
{
    uint32_t *spd = (uint32_t *)src_pd;
    uint32_t *dpd = (uint32_t *)dst_pd;
    uint32_t *spt, *dpt;
    uint32_t pde, i;
    uint32_t frame;

    for (pde = USER_PDE_FIRST; pde < USER_PDE_LAST; pde++) {
        if (!(spd[pde] & PDE_PRESENT))
            continue;
        spt = (uint32_t *)(spd[pde] & 0xFFFFF000u);
        dpt = get_or_create_pt(dpd, pde << 22, 1);
        if (dpt == NULL)
            return -1;
        for (i = 0; i < PT_COUNT; i++) {
            if (!(spt[i] & PDE_PRESENT))
                continue;
            if (paging_is_lfb_frame(spt[i] & 0xFFFFF000u)) {
                dpt[i] = spt[i];    /* LFB: compartido, sin copiar */
                continue;
            }
            frame = pmm_alloc_frame();
            if (frame == 0)
                return -1;
            memcpy((void *)frame, (void *)(spt[i] & 0xFFFFF000u), PAGE_SIZE);
            dpt[i] = (spt[i] & 0xFFF) | frame;  /* copia flags (USER/RW) */
        }
    }
    return 0;
}

void paging_free_user_space(uint32_t pd)
{
    uint32_t *tpd = (uint32_t *)pd;
    uint32_t *pt;
    uint32_t pde, i;

    for (pde = USER_PDE_FIRST; pde < USER_PDE_LAST; pde++) {
        if (!(tpd[pde] & PDE_PRESENT))
            continue;
        pt = (uint32_t *)(tpd[pde] & 0xFFFFF000u);
        for (i = 0; i < PT_COUNT; i++) {
            if (pt[i] & PDE_PRESENT &&
                !paging_is_lfb_frame(pt[i] & 0xFFFFF000u))
                pmm_free_frame(pt[i] & 0xFFFFF000u);   /* LFB: no se libera */
        }
        pmm_free_frame((uint32_t)pt);
        tpd[pde] = 0;
    }
}

void paging_free_pd(uint32_t pd)
{
    pmm_free_frame(pd);
}
