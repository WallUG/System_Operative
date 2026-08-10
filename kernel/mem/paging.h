/* MyOS - kernel/mem/paging.h
 * Paginacion i386 con proteccion de memoria (Fase 7).
 *
 * El kernel se mapea identity 0-1 GiB con paginas de 4 MiB (PSE) y bit
 * USER apagado (supervisor): ring 3 no puede tocar nada del kernel.
 * Cada tarea de usuario tiene su propio PD: copia los PDEs del kernel
 * (supervisor) y mapea sus paginas de 4 KiB (USER) en la region de
 * usuario 0x80000000-0xBFFFFFFF (PDEs 512-767). CR3 se conmuta con la
 * tarea en el cambio de contexto (switch.asm).
 *
 * Los frames del PMM son visibles al kernel por el identity map (todo
 * < 1 GiB), asi que el kernel puede copiar/escribir el contenido de una
 * pagina de usuario por su direccion fisica directamente. */

#ifndef MYOS_PAGING_H
#define MYOS_PAGING_H

#include <stdint.h>

#include "drivers/vbe.h"

#define PAGE_SIZE           0x1000
#define PAGE_ALIGN(a)       (((a) + 0xFFF) & ~0xFFFu)

/* Region virtual de usuario: 0x80000000 .. 0xBFFFFFFF (PDEs 512..767). */
#define USER_VADDR_BASE     0x80000000u
#define USER_VADDR_END      0xC0000000u
#define USER_PDE_FIRST      (USER_VADDR_BASE >> 22)
#define USER_PDE_LAST       (USER_VADDR_END >> 22)      /* exclusivo */
#define USER_ESP0_TOP       USER_VADDR_END             /* cima pila user */
/* Esp inicial de ring 3: 8 B por debajo del tope. El prologo de los
 * CRTs (mingw: lea 4(%esp); push -4(%ecx)) lee el dword [esp] nada mas
 * entrar; con esp == TOP la lectura caeria en la primera pagina no
 * mapeada (#PF 0xC0000000). */
#define USER_ESP0_INIT      (USER_ESP0_TOP - 8)
/* Pila de usuario: 16 paginas (64 KiB). Los CRTs de Windows (msvcrt
 * de mingw-w64) arrancan con bastante consumo de pila, asi que se
 * mapean 64 KiB al crear/exec de cualquier tarea. */
#define USER_STACK_SIZE     (16 * PAGE_SIZE)

/* Kernel identity: 0-1 GiB (PDEs 0..255). */
#define KERNEL_PDE_END      256

/* LFB VBE (Fase 12): el kernel lo mapea identity como superpage
 * supervisor en 0xE0000000 y cada PD de usuario lo ve como paginas
 * USER/RW en VBE_LFB_USER_VA (dentro de la region de usuario). */
void paging_map_kernel_lfb(void);
/* Mapea el LFB VBE (frames fisicos 0xE0000000..) como USER en pd. */
int  paging_user_map_lfb(uint32_t pd);
/* 1 si `frame` es del LFB VBE (jamas se libera con pmm_free_frame). */
int  paging_is_lfb_frame(uint32_t frame);

/* Habilita paginacion: PD global del kernel, identity 0-1 GiB con
 * paginas de 4 MiB supervisor (sin bit USER). */
void paging_init(void);
/* PD del kernel (el que usan idle/tareas de kernel). */
uint32_t paging_kernel_pd(void);
/* Direccion fisica del PD global (compat Fase 4). */
uint32_t paging_pd_addr(void);

/* Crea un PD de tarea de usuario: copia los PDEs del kernel y deja las
 * PTs de usuario vacias. Devuelve la direccion fisica del PD (0 = sin
 * memoria). */
uint32_t paging_create_user_pd(void);
/* Mapea [vaddr, vaddr+size) en pd como paginas USER/RW de 4 KiB,
 * pidiendo un frame nuevo por pagina (a cero). vaddr y size deben ser
 * multiplos de 4 KiB y caer en la region de usuario. 0 = OK, -1 error. */
int paging_user_map(uint32_t pd, uint32_t vaddr, uint32_t size);
/* Mapea un frame concreto (dado por el kernel) como pagina USER en
 * vaddr (debe estar alineado a 4 KiB). 0 = OK, -1 error. */
int paging_user_map_frame(uint32_t pd, uint32_t vaddr, uint32_t frame);
/* Frame fisico mapeado como USER en vaddr de pd (0 si no lo esta). */
uint32_t paging_user_frame(uint32_t pd, uint32_t vaddr);
/* 1 si vaddr esta mapeada como USER en pd (rango y bits USER/P), 0 no. */
int paging_is_user(uint32_t pd, uint32_t vaddr);
/* Copia todas las paginas USER de src a dst (fork): asigna frames
 * nuevos en dst y copia el contenido. dst debe tener las mismas PTs
 * (p. ej. paging_create_user_pd recien creado). 0 = OK, -1 error. */
int paging_copy_user_space(uint32_t dst_pd, uint32_t src_pd);
/* Desmapea todas las paginas USER de pd, libera sus frames y sus PTs.
 * No toca el PD en si (exec reutiliza el mismo PD). */
void paging_free_user_space(uint32_t pd);
/* Libera el PD (tras paging_free_user_space). */
void paging_free_pd(uint32_t pd);
/* Carga un PD en CR3 (cambio de contexto / tarea nueva). */
void paging_switch(uint32_t pd);

#endif
