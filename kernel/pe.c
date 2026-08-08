/* MyOS - kernel/pe.c
 * Cargador minimo de PE32 (Windows .exe, i386) para userland.
 *
 * Layout PE32:
 *   e_lfanew (offset 0x3C) -> firma "PE\0\0", luego COFF File Header
 *   (20 B): machine(2)=0x14C, nsec(2), ... SizeOfOptionalHeader(2),
 *   Characteristics(2).
 *   Optional Header PE32 (224 B): magic(2)=0x10B, AddressOfEntryPoint
 *   en +16, ImageBase en +28. Las secciones (40 B c/u) empiezan justo
 *   despues: Name[8], VirtualSize, VirtualAddress(RVA), SizeOfRawData,
 *   PointerToRawData, ... Characteristics.
 *
 * Se mapea [ImageBase + rva, + max(vsize, rawsize)) en el PD de destino
 * como paginas USER de 4 KiB, se copia el contenido del archivo y el
 * resto de cada pagina queda a cero (bss virtual del PE). El kernel
 * copia por la direccion fisica de los frames (identity map).
 *
 * Un .exe puede traer una seccion .idata autodescriptiva (formato
 * MyOS, ver pe.h): lista de slots {dll[16], fname[16], (resuelto)} que
 * pe_resolve_imports rellena contra los modulos Win32 fijos. Si no hay
 * .idata (hello.exe de makepe.py), el programa usa solo syscalls. */

#include <stdint.h>
#include <string.h>
#include "pe.h"
#include "win32.h"
#include "mem/paging.h"
#include "mem/pmm.h"
#include "kprint.h"

#define IMAGE_DOS_SIGNATURE        0x5A4D     /* 'MZ' */
#define IMAGE_NT_SIGNATURE         0x00004550 /* "PE\0\0" */
#define IMAGE_FILE_MACHINE_I386    0x14C
#define IMAGE_NT_OPTIONAL_HDR32_MAGIC 0x10B
#define PE32_OPT_HDR_SIZE          224
#define IMAGE_SIZEOF_SECTION       40

#define IMAGE_SCN_CNT_UNINITIALIZED_DATA 0x80

static int check_header(const uint8_t *buf, uint32_t size)
{
    uint32_t pe_off, sec_off;
    uint16_t machine, nsec, opt_size;

    if (size < 0x40 + 24)
        return -1;
    if (*(uint16_t *)(void *)&buf[0] != IMAGE_DOS_SIGNATURE) {
        return -1;
    }
    pe_off = *(uint32_t *)(void *)&buf[0x3C];
    if (pe_off + 24 > size)
        return -1;
    if (*(uint32_t *)(void *)&buf[pe_off] != IMAGE_NT_SIGNATURE)
        return -1;
    machine = *(uint16_t *)(void *)&buf[pe_off + 4];
    opt_size = *(uint16_t *)(void *)&buf[pe_off + 20];
    nsec = *(uint16_t *)(void *)&buf[pe_off + 6];
    if (machine != IMAGE_FILE_MACHINE_I386)
        return -1;
    if (opt_size < PE32_OPT_HDR_SIZE)
        return -1;
    if (*(uint16_t *)(void *)&buf[pe_off + 24]
        != IMAGE_NT_OPTIONAL_HDR32_MAGIC)
        return -1;
    if (nsec == 0)
        return -1;
    sec_off = pe_off + 24 + opt_size;
    if (sec_off + (uint32_t)nsec * IMAGE_SIZEOF_SECTION > size)
        return -1;
    return 0;
}

/* Mapea las secciones del PE en pd. 0 = OK, -1 error (deshace en error). */
static int load_sections(uint32_t pd, const uint8_t *buf, uint32_t size)
{
    uint32_t pe_off = *(uint32_t *)(void *)&buf[0x3C];
    uint32_t opt = pe_off + 24;
    uint32_t opt_size = *(uint16_t *)(void *)&buf[pe_off + 20];
    uint32_t nsec = *(uint16_t *)(void *)&buf[pe_off + 6];
    uint32_t image_base = *(uint32_t *)(void *)&buf[opt + 28];
    uint32_t sec = opt + opt_size;
    uint32_t i;

    for (i = 0; i < nsec; i++) {
        uint32_t s = sec + i * IMAGE_SIZEOF_SECTION;
        uint32_t vsize = *(uint32_t *)(void *)&buf[s + 8];
        uint32_t rva = *(uint32_t *)(void *)&buf[s + 12];
        uint32_t raw_size = *(uint32_t *)(void *)&buf[s + 16];
        uint32_t raw_ptr = *(uint32_t *)(void *)&buf[s + 20];
        uint32_t vaddr, vlen, a, rem;
        const uint8_t *src;

        vaddr = image_base + rva;
        vlen = vsize > raw_size ? vsize : raw_size;
        if (vaddr < USER_VADDR_BASE || vaddr + vlen > USER_VADDR_END) {
            return -1;
        }

        /* paginas a cero (bss) + copiar datos por la direccion fisica */
        for (a = vaddr; a < PAGE_ALIGN(vaddr + vlen); a += PAGE_SIZE) {
            uint32_t frame = pmm_alloc_frame();
            if (frame == 0) {
                    return -1;
            }
            memset((void *)frame, 0, PAGE_SIZE);
            if (paging_user_map_frame(pd, a, frame) != 0) {
                    pmm_free_frame(frame);
                return -1;
            }
        }

        rem = raw_size;
        src = buf + raw_ptr;
        while (rem > 0) {
            uint32_t frame = paging_user_frame(pd, vaddr);
            uint32_t n = PAGE_SIZE - (vaddr & 0xFFF);
            if (n > rem)
                n = rem;
            if (frame == 0) {
                        return -1;
            }
            memcpy((void *)(frame + (vaddr & 0xFFF)), src, n);
            vaddr += n;
            src += n;
            rem -= n;
        }
    }
    return 0;
}

static uint32_t pe_entry(const uint8_t *buf)
{
    uint32_t pe_off = *(uint32_t *)(void *)&buf[0x3C];
    uint32_t opt = pe_off + 24;
    uint32_t image_base = *(uint32_t *)(void *)&buf[opt + 28];
    uint32_t entry_rva = *(uint32_t *)(void *)&buf[opt + 16];
    return image_base + entry_rva;
}

/* Rellena la tabla de imports del .exe (.idata en WIN32_IDATA_VA) con
 * las direcciones de los modulos Win32 fijos (win32.c). El .exe la
 * declara como idata_slot_t {dll[16], fn[16], resolved}. Cada slot se
 * escribe por la direccion fisica de su frame (identity map). */
static void pe_resolve_imports(uint32_t pd)
{
    uint32_t i;

    for (i = 0; i < WIN32_IDATA_MAX; i++) {
        uint32_t va = WIN32_IDATA_VA + i * 36;
        uint32_t frame = paging_user_frame(pd, va);
        uint8_t *p;

        if (frame == 0)
            break;                      /* iva fuera de seccion */
        p = (uint8_t *)frame;
        if (p[0] == 0)
            break;                      /* fin de tabla */
        uint32_t addr = win32_resolve((const char *)p, (const char *)p + 16);
        *(uint32_t *)(p + 32) = addr;
    }
}

int pe_load(const void *buf, uint32_t size, uint32_t *pd_out,
            uint32_t *entry_out)
{
    uint32_t pd;

    if (check_header(buf, size) != 0) {
        kprint("pe:A\n");
        return 1;
    }
    pd = paging_create_user_pd();
    if (pd == 0) {
        kprint("pe:B frames libres=");
        kprint_uint(pmm_free_count());
        kprint(" primer(no0)=");
        const uint8_t *bm = (const uint8_t *)0x20000u;
        for (uint32_t bi = 0; bi < 32; bi++) {
            uint8_t b = bm[bi];
            if (b != 0xFF) {
                kprint_hex32((uint32_t)b);
                kprint("@");
                kprint_hex32(bi);
                kprint("\n");
                break;
            }
        }
        return 2;
    }
    if (load_sections(pd, buf, size) != 0) {
        kprint("pe:C\n");
        paging_free_user_space(pd);
        paging_free_pd(pd);
        return 3;
    }
    pe_resolve_imports(pd);
    *pd_out = pd;
    *entry_out = pe_entry(buf);
    return 0;
}

int pe_load_into(uint32_t pd, const void *buf, uint32_t size,
                 uint32_t *entry_out)
{
    if (check_header(buf, size) != 0)
        return -1;
    paging_free_user_space(pd);
    if (load_sections(pd, buf, size) != 0)
        return -1;
    pe_resolve_imports(pd);
    *entry_out = pe_entry(buf);
    return 0;
}
