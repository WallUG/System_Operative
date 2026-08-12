/* MyOS - kernel/pe.c
 * Cargador minimo de PE32 (Windows .exe, i386) para userland.
 *
 * Layout PE32:
 *   e_lfanew (offset 0x3C) -> firma "PE\0\0", luego COFF File Header
 *   (20 B): machine(2)=0x14C, nsec(2), ... SizeOfOptionalHeader(2),
 *   Characteristics(2).
 *   Optional Header PE32 (224 B): magic(2)=0x10B, AddressOfEntryPoint
 *   en +16, ImageBase en +28. Data Directories en +96 (16 entradas de
 *   8 B; la 1 es la tabla de imports). Las secciones (40 B c/u)
 *   empiezan justo despues: Name[8], VirtualSize, VirtualAddress(RVA),
 *   SizeOfRawData, PointerToRawData, ... Characteristics.
 *
 * Se mapea [ImageBase + rva, + max(vsize, rawsize)) en el PD de destino
 * como paginas USER de 4 KiB, se copia el contenido del archivo y el
 * resto de cada pagina queda a cero (bss virtual del PE). El kernel
 * copia por la direccion fisica de los frames (identity map).
 *
 * Imports: formato ESTANDAR de Windows (Fase 9):
 *   - Data Directory [1] -> IMAGE_IMPORT_DESCRIPTOR*.
 *   - Cada descriptor: Name (RVA de "KERNEL32.dll"), OriginalFirstThunk
 *     y FirstThunk (RVA de la tabla de thunks). Cada thunk con el bit
 *     0x80000000 es un ordinal; si no, RVA de {Hint(2), nombre ASCII}.
 *   - La IAT (FirstThunk) se rellena con las VA resueltas contra los
 *     modulos Win32 fijos (win32_resolve, nombres case-insensitive):
 *     el kernel escribe por la direccion fisica del frame de la pagina.
 * Si no hay Directory[1] el .exe usa el formato historico .idata de
 * MyOS (pe_resolve_imports), para compat con makepe.py. */

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
#define IMAGE_NT_DIRECTORY_ENTRY_IMPORT  1

#define IMAGE_SCN_CNT_UNINITIALIZED_DATA 0x80

/* .exe reales como metapad vienen con ImageBase en memoria baja
 * (0x00400000): MyOS solo mapea usuario en 0x80000000-0xC0000000.
 * Se reubica la imagen con la tabla .reloc a esta base fija. */
#define PE_REBASE_BASE      0x81000000u
#define IMAGE_DIR_ENTRY_BASERELOC 5
#define IMAGE_REL_BASED_HIGHLOW  3

/* Convierte una RVA (de seccion) a offset en el archivo mirando la
 * tabla de secciones (solo RVA relativos a secciones, no absolutos). */
static uint32_t pe_rva_to_off(const uint8_t *buf, uint32_t rva)
{
    uint32_t pe_off = *(uint32_t *)(void *)&buf[0x3C];
    uint32_t opt_size = *(uint16_t *)(void *)&buf[pe_off + 20];
    uint32_t nsec = *(uint16_t *)(void *)&buf[pe_off + 6];
    uint32_t sec = pe_off + 24 + opt_size;
    uint32_t i;

    for (i = 0; i < nsec; i++) {
        uint32_t s = sec + i * IMAGE_SIZEOF_SECTION;
        uint32_t vsize = *(uint32_t *)(void *)&buf[s + 8];
        uint32_t va = *(uint32_t *)(void *)&buf[s + 12];
        uint32_t raw_size = *(uint32_t *)(void *)&buf[s + 16];
        uint32_t raw_ptr = *(uint32_t *)(void *)&buf[s + 20];
        if (raw_size != 0 && rva >= va && rva < va + vsize)
            return raw_ptr + (rva - va);
    }
    return 0;
}

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

/* Mapea las secciones del PE en pd. 0 = OK, -1 error (deshace en error).
 * image_base = base FINAL (tras posible reubicacion). */
static int load_sections(uint32_t pd, const uint8_t *buf, uint32_t size,
                         uint32_t image_base)
{
    uint32_t pe_off = *(uint32_t *)(void *)&buf[0x3C];
    uint32_t opt = pe_off + 24;
    uint32_t opt_size = *(uint16_t *)(void *)&buf[pe_off + 20];
    uint32_t nsec = *(uint16_t *)(void *)&buf[pe_off + 6];
    uint32_t sec = opt + opt_size;
    uint32_t i;

    /* Los CRTs mingw inspeccionan la pagina de la cabecera DOS/NT en la
     * base de la imagen ('MZ', e_lfanew, ...): si ninguna seccion la
     * ocupa (RVA=0), mapearla despues del loop. */
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

    {
        uint32_t page = paging_user_frame(pd, image_base);
        if (page == 0) {
            page = pmm_alloc_frame();
            uint32_t n = size < PAGE_SIZE ? size : PAGE_SIZE;
            if (page == 0)
                return -1;
            memset((void *)page, 0, PAGE_SIZE);
            memcpy((void *)page, buf, n);
            if (paging_user_map_frame(pd, image_base, page) != 0) {
                pmm_free_frame(page);
                return -1;
            }
        }
    }
    return 0;
}

static uint32_t pe_entry(const uint8_t *buf, uint32_t image_base)
{
    uint32_t pe_off = *(uint32_t *)(void *)&buf[0x3C];
    uint32_t opt = pe_off + 24;
    uint32_t entry_rva = *(uint32_t *)(void *)&buf[opt + 16];
    return image_base + entry_rva;
}

static uint32_t pe_image_base(const uint8_t *buf)
{
    uint32_t pe_off = *(uint32_t *)(void *)&buf[0x3C];
    return *(uint32_t *)(void *)&buf[pe_off + 24 + 28];
}

/* Aplica la tabla .reloc del .exe (data directory 5) reasentando la
 * imagen de old_base a new_base: cada bloque relocaliza una pagina
 * (PageRVA + entradas de 16 bits type/offset); tipo 3 = HIGH/LOW (dword
 * += delta). Escribe por el frame fisico (identity map) del PD. */
static void pe_apply_relocs(uint32_t pd, const uint8_t *buf, uint32_t size,
                            uint32_t old_base, uint32_t new_base)
{
    uint32_t pe_off = *(uint32_t *)(void *)&buf[0x3C];
    uint32_t opt = pe_off + 24;
    uint32_t dir_rva, dir_size, off, end, delta;

    if (old_base == new_base)
        return;
    dir_rva = *(uint32_t *)(void *)&buf[opt + 96 + IMAGE_DIR_ENTRY_BASERELOC * 8];
    dir_size = *(uint32_t *)(void *)&buf[opt + 96 + IMAGE_DIR_ENTRY_BASERELOC * 8 + 4];
    if (dir_rva == 0 || dir_size == 0)
        return;                 /* sin tabla: asumo imagen loadable en la base */
    off = pe_rva_to_off(buf, dir_rva);
    end = off + dir_size;
    if (end > size)
        end = size;
    delta = new_base - old_base;

    while (off + 8 <= end) {
        uint32_t page_rva = *(uint32_t *)(void *)&buf[off];
        uint32_t block_size = *(uint32_t *)(void *)&buf[off + 4];
        uint32_t nword, j;

        if (page_rva == 0 || block_size < 8)
            break;
        if (off + block_size > end)
            break;
        nword = (block_size - 8) / 2;
        for (j = 0; j < nword; j++) {
            uint16_t w = *(uint16_t *)(void *)&buf[off + 8 + j * 2];
            uint32_t type = w >> 12;
            uint32_t rel = w & 0xFFFu;
            if (type == IMAGE_REL_BASED_HIGHLOW) {
                uint32_t va = new_base + page_rva + rel;
                uint32_t frame = paging_user_frame(pd, va & ~0xFFFu);
                if (frame != 0)
                    *(uint32_t *)(void *)(frame + (va & 0xFFF)) += delta;
            }
            /* type 0 (ABSOLUTE) y otros: ignorar */
        }
        off += block_size;
    }
}

/* Convierte un ordinal a nombre de export "ord#N": comctl32.dll exporta
 * InitCommonControls como ordinal 8 e InitCommonControlsEx como 17, y
 * algunos .exe reales los importan por ordinal (metapad.exe). */
static void pe_ordinal_name(uint32_t ord, char *out)
{
    char tmp[12];
    uint32_t t = 0, i;

    out[0] = 'o'; out[1] = 'r'; out[2] = 'd'; out[3] = '#';
    if (ord == 0) {
        out[4] = '0';
        out[5] = 0;
        return;
    }
    while (ord) {
        tmp[t++] = (char)('0' + ord % 10);
        ord /= 10;
    }
    for (i = 0; i < t; i++)
        out[4 + i] = tmp[t - 1 - i];
    out[4 + t] = 0;
}

/* Rellena la tabla de imports del .exe (.idata en WIN32_IDATA_VA) con
 * las direcciones de los modulos Win32 fijos (win32.c). Formato MyOS
 * historico (makepe.py): slots {dll[16], fn[16], resolved}. Escribe por
 * la direccion fisica de los frames (identity map). */
static void pe_resolve_imports_myos(uint32_t pd)
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

/* Compara dos nombres de DLL ignorando mayusculas ("KERNEL32.dll" ==
 * "kernel32.dll"). 1 si iguales. */
static int dll_name_eq(const char *a, const char *b)
{
    for (;;) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb)
            return 0;
        if (ca == 0)
            return 1;
        a++, b++;
    }
}

/* Escribe 0 en un slot de la IAT de un .exe cargado (frame USER). */
static void pe_iat_zero(uint32_t pd, uint32_t va)
{
    uint32_t frame = paging_user_frame(pd, va & ~0xFFFu);
    if (frame != 0)
        *(uint32_t *)(void *)(frame + (va & 0xFFF)) = 0;
}

/* Resuelve la tabla de imports ESTANDAR de Windows del .exe en buf y
 * rellena la IAT (FirstThunk) escribiendo por el frame de la pagina
 * USER en pd. Devuelve el numero de imports resueltos (-1 si no hay
 * tabla de imports o error de formato). */
static int pe_resolve_imports_std(uint32_t pd, const uint8_t *buf,
                                  uint32_t size, uint32_t image_base)
{
    uint32_t pe_off = *(uint32_t *)(void *)&buf[0x3C];
    uint32_t opt = pe_off + 24;
    uint32_t dir_rva = *(uint32_t *)(void *)&buf[opt + 96 + 8];
    uint32_t desc_off, n = 0;
    uint32_t i;

    if (dir_rva == 0)
        return 0;               /* .exe sin imports (makepe.py) */
    desc_off = 0;
    for (i = 0;; i++) {
        uint32_t d = pe_rva_to_off(buf, dir_rva + i * 20);
        uint32_t oft, ft, name_rva;
        uint32_t j;

        if (d == 0 || d + 20 > size)
            break;
        oft = *(uint32_t *)(void *)&buf[d + 0];       /* OriginalFirstThunk */
        name_rva = *(uint32_t *)(void *)&buf[d + 12]; /* Name -> DLL */
        ft = *(uint32_t *)(void *)&buf[d + 16];       /* FirstThunk (IAT) */
        if (ft == 0)
            break;
        {
            uint32_t no = pe_rva_to_off(buf, name_rva);
            const char *dll;
            if (no == 0 || no >= size)
                break;
            dll = (const char *)(buf + no);
            for (j = 0;; j++) {
                uint32_t th_off = pe_rva_to_off(buf, oft + j * 4);
                uint32_t th;
                uint32_t fn_rva, fo;
                const char *fname;
                uint32_t addr;

                if (th_off == 0 || th_off + 4 > size)
                    break;
                th = *(uint32_t *)(void *)&buf[th_off];
                if (th == 0)
                    break;
                if (th & 0x80000000) {
                    char oname[24];
                    pe_ordinal_name(th & 0xFFFF, oname);
                    addr = win32_resolve(dll, oname);
                    if (addr == 0) {
                        kprint("pe: ordinal no resuelto: ");
                        kprint(dll);
                        kprint("!");
                        kprint(oname);
                        kprint("\n");
                        pe_iat_zero(pd, image_base + ft + j * 4);
                    } else {
                        goto iat_store;
                    }
                    continue;
                }
                fn_rva = th;
                fo = pe_rva_to_off(buf, fn_rva);
                if (fo == 0 || fo + 4 > size)
                    break;
                fname = (const char *)(buf + fo + 2);   /* saltar Hint */
                addr = win32_resolve(dll, fname);
                if (addr == 0) {
                    kprint("pe: import no resuelto: ");
                    kprint(dll);
                    kprint("!");
                    kprint(fname);
                    kprint("\n");
                    /* import sin resolver: el slot queda a 0 -> el exe
                     * falle con un call a 0 (crash controlado), no
                     * saltando al RVA del INT que dejaba el archivo. */
                    pe_iat_zero(pd, image_base + ft + j * 4);
                    continue;
                }
            iat_store:
                {
                    uint32_t va = image_base + ft + j * 4;
                    uint32_t frame =
                        paging_user_frame(pd, va & ~0xFFFu);
                    if (frame == 0) {
                        kprint("pe: IAT fuera de seccion\n");
                        continue;
                    }
                    *(uint32_t *)(void *)(frame + (va & 0xFFF)) = addr;
                    n++;
                }
            }
        }
    }
    return (int)n;
}

int pe_load(const void *buf, uint32_t size, uint32_t *pd_out,
            uint32_t *entry_out, uint32_t *base_out)
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
    /* .exe reales (metapad) usan ImageBase baja (0x00400000): reubicar
     * la imagen a la region de usuario y parchear .reloc. */
    {
        uint32_t old_base = pe_image_base(buf);
        uint32_t image_base = old_base;
        if (image_base < USER_VADDR_BASE || image_base >= USER_VADDR_END)
            image_base = PE_REBASE_BASE;
        if (load_sections(pd, buf, size, image_base) != 0) {
            kprint("pe:C\n");
            paging_free_user_space(pd);
            paging_free_pd(pd);
            return 3;
        }
        pe_apply_relocs(pd, buf, size, old_base, image_base);
        if (pe_resolve_imports_std(pd, buf, size, image_base) <= 0)
            pe_resolve_imports_myos(pd);    /* formato MyOS (makepe.py) */
        *pd_out = pd;
        *entry_out = pe_entry(buf, image_base);
        *base_out = image_base;
        return 0;
    }
}

int pe_load_into(uint32_t pd, const void *buf, uint32_t size,
                 uint32_t *entry_out, uint32_t *base_out)
{
    if (check_header(buf, size) != 0)
        return -1;
    paging_free_user_space(pd);
    {
        uint32_t old_base = pe_image_base(buf);
        uint32_t image_base = old_base;
        if (image_base < USER_VADDR_BASE || image_base >= USER_VADDR_END)
            image_base = PE_REBASE_BASE;
        if (load_sections(pd, buf, size, image_base) != 0)
            return -1;
        pe_apply_relocs(pd, buf, size, old_base, image_base);
        if (pe_resolve_imports_std(pd, buf, size, image_base) <= 0)
            pe_resolve_imports_myos(pd);
        *entry_out = pe_entry(buf, image_base);
        *base_out = image_base;
        return 0;
    }
}
