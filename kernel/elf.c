/* MyOS - kernel/elf.c
 * Cargador minimo de ELF32 ET_EXEC i386 (ver elf.h).
 *
 * Layout ELF32:
 *   e_ehdr:  e_ident[16] (0x7F 'E' 'L' 'F', clase 1=32bit, dato 1=LE),
 *           e_type(2)=2 ET_EXEC, e_machine(2)=3 i386, e_entry(4),
 *           e_phoff(4), e_phnum(2) en el offset 44, e_phentsize(2).
 *   e_phdr:  p_type(4)=1 PT_LOAD, p_offset(4), p_vaddr(4), p_filesz(4),
 *           p_memsz(4) -> entradas de 20 bytes desde e_phoff.
 *
 * Un segmento puede no estar alineado a pagina: se mapea el rango
 * [vaddr & ~0xFFF, round_up(vaddr+memsz)) y se copian los datos en el
 * offset dentro de la primera pagina. */

#include <stdint.h>
#include <string.h>
#include "elf.h"
#include "mem/paging.h"
#include "mem/pmm.h"

#define ELFCLASS32      1
#define ELF_DATA_LE     1
#define ET_EXEC         2
#define EM_386          3
#define PT_LOAD         1
#define ELF_MAGIC0      0x7F

typedef struct {
    uint8_t  e_ident[16];
    uint16_t e_type, e_machine;
    uint32_t e_version, e_entry, e_phoff, e_shoff, e_flags;
    uint16_t e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum,
             e_shstrndx;
} __attribute__((packed)) elf32_ehdr_t;

typedef struct {
    uint32_t p_type, p_offset, p_vaddr, p_paddr;
    uint32_t p_filesz, p_memsz, p_flags, p_align;
} __attribute__((packed)) elf32_phdr_t;

static int check_header(const uint8_t *buf, uint32_t size)
{
    const elf32_ehdr_t *h = (const elf32_ehdr_t *)buf;

    if (size < sizeof(elf32_ehdr_t))
        return -1;
    if (!(buf[0] == ELF_MAGIC0 && buf[1] == 'E' && buf[2] == 'L'
          && buf[3] == 'F'))
        return -1;
    if (h->e_ident[4] != ELFCLASS32 || h->e_ident[5] != ELF_DATA_LE)
        return -1;
    if (h->e_type != ET_EXEC || h->e_machine != EM_386)
        return -1;
    if (h->e_phentsize < sizeof(elf32_phdr_t) || h->e_phnum == 0)
        return -1;
    return 0;
}

/* Valida todos los PT_LOAD sin asignar memoria (bajo sospecha antes de
 * liberar el espacio de usuario en exec). */
static int check_segments(const uint8_t *buf, uint32_t size)
{
    const elf32_ehdr_t *h = (const elf32_ehdr_t *)buf;
    uint32_t i;

    for (i = 0; i < h->e_phnum; i++) {
        const elf32_phdr_t *ph;
        uint32_t off = h->e_phoff + i * h->e_phentsize;
        if (off + sizeof(elf32_phdr_t) > size)
            return -1;
        ph = (const elf32_phdr_t *)(buf + off);
        if (ph->p_type != PT_LOAD)
            continue;
        if (ph->p_filesz > ph->p_memsz)
            return -1;
        if (ph->p_offset + ph->p_filesz > size)
            return -1;
        if (ph->p_vaddr < USER_VADDR_BASE
            || ph->p_vaddr + ph->p_memsz > USER_VADDR_END)
            return -1;
    }
    return 0;
}

static int load_segments(uint32_t pd, const uint8_t *buf)
{
    const elf32_ehdr_t *h = (const elf32_ehdr_t *)buf;
    uint32_t i;

    for (i = 0; i < h->e_phnum; i++) {
        const elf32_phdr_t *ph =
            (const elf32_phdr_t *)(buf + h->e_phoff + i * h->e_phentsize);
        uint32_t page_start, off, map_end, a, rem, dst;
        const uint8_t *src;

        if (ph->p_type != PT_LOAD)
            continue;

        page_start = ph->p_vaddr & ~0xFFFu;
        off = ph->p_vaddr - page_start;
        map_end = PAGE_ALIGN(off + ph->p_memsz);
        if (page_start + map_end > USER_VADDR_END)
            return -1;

        /* Mapear las paginas del segmento (frames a cero: bss) y
         * copiar los datos por la direccion fisica (identity map). */
        for (a = page_start; a < page_start + map_end; a += PAGE_SIZE) {
            uint32_t frame = pmm_alloc_frame();
            if (frame == 0)
                return -1;
            memset((void *)frame, 0, PAGE_SIZE);
            if (paging_user_map_frame(pd, a, frame) != 0) {
                pmm_free_frame(frame);
                return -1;
            }
        }

        rem = ph->p_filesz;
        dst = ph->p_vaddr;
        src = buf + ph->p_offset;
        while (rem > 0) {
            uint32_t frame = paging_user_frame(pd, dst);
            uint32_t n = PAGE_SIZE - (dst & 0xFFF);
            if (n > rem)
                n = rem;
            if (frame == 0)
                return -1;
            memcpy((void *)(frame + (dst & 0xFFF)), src, n);
            dst += n;
            src += n;
            rem -= n;
        }
    }
    return 0;
}

int elf_load(const void *buf, uint32_t size, uint32_t *pd_out,
             uint32_t *entry_out)
{
    const elf32_ehdr_t *h = (const elf32_ehdr_t *)buf;
    uint32_t pd;

    if (check_header(buf, size) != 0 || check_segments(buf, size) != 0)
        return -1;
    pd = paging_create_user_pd();
    if (pd == 0)
        return -1;
    if (load_segments(pd, buf) != 0) {
        paging_free_user_space(pd);
        paging_free_pd(pd);
        return -1;
    }
    *pd_out = pd;
    *entry_out = h->e_entry;
    return 0;
}

int elf_load_into(uint32_t pd, const void *buf, uint32_t size,
                  uint32_t *entry_out)
{
    const elf32_ehdr_t *h = (const elf32_ehdr_t *)buf;

    if (check_header(buf, size) != 0 || check_segments(buf, size) != 0)
        return -1;
    paging_free_user_space(pd);
    if (load_segments(pd, buf) != 0)
        return -1;
    *entry_out = h->e_entry;
    return 0;
}
