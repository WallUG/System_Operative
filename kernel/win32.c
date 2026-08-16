/* MyOS - kernel/win32.c
 * Modulos Win32 ring 3 fijos (decision "Modulos ring 3 fijos").
 *
 * kernel32.dll / user32.dll / ntdll.dll son programas ELF32 enlazados
 * en la region fija de DLL (0xB0000000+, tools/dll.ld) con una tabla
 * .exports. El kernel los carga del FS (kernel32.elf, user32.elf,
 * ntdll.elf) en win32_init():
 *   - valida la cabecera ELF y guarda el binario en RAM (dll_bin),
 *   - extrae la tabla .exports (shdr) a una win32_module_t.
 * win32_map_all(pd) se llama al crear cada tarea de usuario (shell
 * "run", sys_exec): mapea el binario de cada modulo a frames nuevos
 * en su base fija (paginacion USER). Los .exe ven los modulos siempre
 * en 0xB0000000.. -> imports sin relocaciones.
 *
 * La tabla de imports de los .exe (pe.c) resuelve contra
 * win32_resolve("dll", "func"). */

#include <stdint.h>
#include <string.h>
#include "win32.h"
#include "mem/paging.h"
#include "mem/pmm.h"
#include "mem/heap.h"
#include "fs/mefs.h"
#include "kprint.h"

/* Cabeceras ELF32 (iguales que en elf.c; para el modulo necesitamos
 * ademas la tabla de secciones shdr: .exports). */
typedef struct {
    uint8_t  e_ident[16];
    uint16_t e_type, e_machine;
    uint32_t e_version, e_entry, e_phoff, e_shoff, e_flags;
    uint16_t e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum,
             e_shstrndx;
} __attribute__((packed)) elf32_ehdr_t;
typedef struct {
    uint32_t sh_name, sh_type, sh_flags, sh_addr, sh_offset, sh_size;
    uint32_t sh_link, sh_info, sh_addralign, sh_entsize;
} __attribute__((packed)) elf32_shdr_t;
typedef struct {
    uint32_t p_type, p_offset, p_vaddr, p_paddr;
    uint32_t p_filesz, p_memsz, p_flags, p_align;
} __attribute__((packed)) elf32_phdr_t;

static win32_module_t mods[WIN32_MAX_DLLS];
static int  mod_count = 0;

struct dll_desc {
    const char *fs_name;      /* nombre en el FS: "kernel32.elf" */
    const char *dll_name;     /* "kernel32.dll" (imports del .exe) */
    uint32_t    base;         /* region: 0xB0000000 + i*0x100000 */
};
#define DLL_BASE_STEP 0x100000
static const struct dll_desc dll_descs[] = {
    { "kernel32.elf", "kernel32.dll", WIN32_REGION_BASE + DLL_BASE_STEP * 0 },
    { "user32.elf",   "user32.dll",   WIN32_REGION_BASE + DLL_BASE_STEP * 1 },
    { "ntdll.elf",    "ntdll.dll",    WIN32_REGION_BASE + DLL_BASE_STEP * 2 },
    { "msvcrt.elf",   "msvcrt.dll",   WIN32_REGION_BASE + DLL_BASE_STEP * 3 },
    { "gdi32.elf",    "gdi32.dll",    WIN32_REGION_BASE + DLL_BASE_STEP * 4 },
    { "comctl32.elf", "comctl32.dll", WIN32_REGION_BASE + DLL_BASE_STEP * 5 },
    { "comdlg32.elf", "comdlg32.dll", WIN32_REGION_BASE + DLL_BASE_STEP * 6 },
    { "advapi32.elf", "advapi32.dll", WIN32_REGION_BASE + DLL_BASE_STEP * 7 },
    { "shell32.elf",  "shell32.dll",  WIN32_REGION_BASE + DLL_BASE_STEP * 8 },
    { "ole32.elf",    "ole32.dll",    WIN32_REGION_BASE + DLL_BASE_STEP * 9 },
    { "shlwapi.elf",  "shlwapi.dll",  WIN32_REGION_BASE + DLL_BASE_STEP * 10 },
    { "winspool.elf", "winspool.drv", WIN32_REGION_BASE + DLL_BASE_STEP * 11 },
};

/* Extrae la tabla .exports y las secciones de relocaciones del binario
 * ELF (no copia: guarda offsets; win32_resolve escanea sobre la marcha).
 * Los modulos se enlazan con --emit-relocs (Makefile: ld -q), asi que
 * el binario lleva .rel.text/.rel.data con las direcciones absolutas
 * (R_386_32) que hay que parchear si se carga en otra base (Fase 20-C). */
static int parse_exports(win32_module_t *m, const uint8_t *bin)
{
    const elf32_ehdr_t *h = (const elf32_ehdr_t *)bin;
    const elf32_shdr_t *sh;
    const char *shstr;
    uint32_t i;

    if (h->e_shoff == 0 || h->e_shnum == 0)
        return -1;
    if (h->e_shstrndx >= h->e_shnum)
        return -1;
    sh = (const elf32_shdr_t *)(bin + h->e_shoff);
    shstr = (const char *)(bin + sh[h->e_shstrndx].sh_offset);

    m->rel_off = 0;
    m->rel_size = 0;
    m->rel_text_off = 0;
    m->rel_text_size = 0;
    m->rel_data_off = 0;
    m->rel_data_size = 0;
    m->rel_exports_off = 0;
    m->rel_exports_size = 0;
    m->rel_rodata_off = 0;
    m->rel_rodata_size = 0;
    for (i = 0; i < h->e_shnum; i++) {
        const char *nm = shstr + sh[i].sh_name;
        if (strcmp(nm, ".exports") == 0) {
            m->exports_off = sh[i].sh_offset;
            m->n_exports = sh[i].sh_size / sizeof(win32_export_t);
        } else if (strcmp(nm, ".rel.text") == 0) {
            m->rel_text_off = sh[i].sh_offset;
            m->rel_text_size = sh[i].sh_size;
        } else if (strcmp(nm, ".rel.data") == 0) {
            m->rel_data_off = sh[i].sh_offset;
            m->rel_data_size = sh[i].sh_size;
        } else if (strcmp(nm, ".rel.exports") == 0) {
            m->rel_exports_off = sh[i].sh_offset;
            m->rel_exports_size = sh[i].sh_size;
        } else if (strcmp(nm, ".rel.rodata") == 0) {
            m->rel_rodata_off = sh[i].sh_offset;
            m->rel_rodata_size = sh[i].sh_size;
        }
    }
    if (m->n_exports == 0)
        return -1;
    m->rel_off = m->rel_text_off ? m->rel_text_off : m->rel_data_off;
    m->rel_size = m->rel_text_size + m->rel_data_size +
                  m->rel_exports_size + m->rel_rodata_size;
    return 0;
}

/* R_386_32 = 1, R_386_PC32 = 2 (ELF32 i386). Con --emit-relocs el tipo
 * original se conserva en r_info (alto byte del ELF little-endian: la
 * info es (sym<<8)|type). Las relocs .rel (no .rela) tienen "addend"
 * implicito = valor en el destino. */
#define R_386_32 1

/* Convierte una VA del modulo (r_offset de las relocs, relativa a la
 * base de enlazado) a offset dentro del archivo ELF, usando el primer
 * PT_LOAD (los datos empiezan en file offset 0x1000, no en 0). */
static uint32_t reloc_va_to_off(const win32_module_t *m, uint32_t va)
{
    const elf32_ehdr_t *h = (const elf32_ehdr_t *)m->bin;
    uint32_t i;
    for (i = 0; i < h->e_phnum; i++) {
        const elf32_phdr_t *ph =
            (const elf32_phdr_t *)(m->bin + h->e_phoff) + i;
        if (ph->p_type == 1) {
            uint32_t end = ph->p_vaddr + ph->p_filesz;
            if (va >= ph->p_vaddr && va + 4 <= end)
                return ph->p_offset + (va - ph->p_vaddr);
        }
    }
    return (uint32_t)-1;
}

/* Aplica las relocaciones R_386_32 del modulo al binario en RAM del
 * kernel (una sola vez, en load_one): cada word absoluto apunta a la
 * base vieja (la del enlazado); le sumamos delta = new_base - old_base
 * para que apunte a la base de carga. Asi el binario queda con las
 * direcciones correctas para la base elegida y win32_map solo copia. */
static int apply_relocs(win32_module_t *m, uint32_t old_base,
                        uint32_t new_base)
{
    const uint8_t *bin = m->bin;
    uint32_t delta = new_base - old_base;
    uint32_t i;

    if (delta == 0)
        return 0;
    for (i = 0; i < m->rel_text_size; i += 8) {
        uint32_t off = m->rel_text_off + i;
        uint32_t r_offset = *(uint32_t *)(void *)(bin + off);
        uint32_t info = *(uint32_t *)(void *)(bin + off + 4);
        uint32_t type = info & 0xFFu;
        uint32_t fo;
        if (type != R_386_32)
            continue;
        fo = reloc_va_to_off(m, r_offset);
        if (fo == (uint32_t)-1 || fo + 4 > m->file_size)
            continue;
        *(uint32_t *)(void *)(bin + fo) += delta;
    }
    for (i = 0; i < m->rel_data_size; i += 8) {
        uint32_t off = m->rel_data_off + i;
        uint32_t r_offset = *(uint32_t *)(void *)(bin + off);
        uint32_t info = *(uint32_t *)(void *)(bin + off + 4);
        uint32_t type = info & 0xFFu;
        uint32_t fo;
        if (type != R_386_32)
            continue;
        fo = reloc_va_to_off(m, r_offset);
        if (fo == (uint32_t)-1 || fo + 4 > m->file_size)
            continue;
        *(uint32_t *)(void *)(bin + fo) += delta;
    }
    for (i = 0; i < m->rel_exports_size; i += 8) {
        uint32_t off = m->rel_exports_off + i;
        uint32_t r_offset = *(uint32_t *)(void *)(bin + off);
        uint32_t info = *(uint32_t *)(void *)(bin + off + 4);
        uint32_t type = info & 0xFFu;
        uint32_t fo;
        if (type != R_386_32)
            continue;
        fo = reloc_va_to_off(m, r_offset);
        if (fo == (uint32_t)-1 || fo + 4 > m->file_size)
            continue;
        *(uint32_t *)(void *)(bin + fo) += delta;
    }
    for (i = 0; i < m->rel_rodata_size; i += 8) {
        uint32_t off = m->rel_rodata_off + i;
        uint32_t r_offset = *(uint32_t *)(void *)(bin + off);
        uint32_t info = *(uint32_t *)(void *)(bin + off + 4);
        uint32_t type = info & 0xFFu;
        uint32_t fo;
        if (type != R_386_32)
            continue;
        fo = reloc_va_to_off(m, r_offset);
        if (fo == (uint32_t)-1 || fo + 4 > m->file_size)
            continue;
        *(uint32_t *)(void *)(bin + fo) += delta;
    }
    return 0;
}

/* Busca el export `fn` dentro del binario del modulo (m->bin, en RAM
 * del kernel): devuelve la VA de la funcion o 0. */
static uint32_t module_find_export(win32_module_t *m, const char *fn)
{
    const win32_export_t *ex;
    uint32_t j;

    if (m->n_exports == 0)
        return 0;
    ex = (const win32_export_t *)(m->bin + m->exports_off);
    for (j = 0; j < m->n_exports; j++) {
        if (strcmp(ex[j].name, fn) == 0)
            return ex[j].fn;    /* VA del modulo en el PD */
    }
    return 0;
}

/* El linkage script tools/dll.ld ancla la seccion .exports en la pagina
 * de la base? no: puede estar en cualquier VA; la resolvemos aqui: cada
 * export guarda fn = VA-absoluta del modulo (el ELF esta enlazado a la
 * base fija, asi que fn ya es la direccion correcta en el PD). */

static int load_one(const struct dll_desc *d, uint32_t idx,
                    win32_module_t *m)
{
    int sz = (int)mefs_size(d->fs_name);
    uint8_t *buf;

    memset(m, 0, sizeof(*m));
    if (sz <= 0)
        return -1;
    buf = kmalloc((uint32_t)sz);
    if (buf == 0)
        return -1;
    if (mefs_read(d->fs_name, buf, (uint32_t)sz) != sz) {
        kfree(buf);
        return -1;
    }
    if (memcmp(buf, "\x7f" "ELF\x01\x01", 6) != 0) {
        kfree(buf);
        return -1;
    }
    if (parse_exports(m, buf) != 0) {
        kfree(buf);
        return -1;
    }
    /* guardar binario para win32_map (mantener en RAM del kernel) */
    m->bin = buf;
    m->file_size = (uint32_t)sz;
    m->base = d->base + WIN32_RELOC_DELTA;
    m->dll_idx = idx;
    /* Fase 20-C: parchear las referencias absolutas del binario a la
     * base de carga (relocs .rel.text/.rel.data, enlazadas con -q). */
    if (apply_relocs(m, d->base, m->base) != 0) {
        kfree(buf);
        m->bin = 0;
        return -1;
    }
    return 0;
}

int win32_init(void)
{
    uint32_t i;
    int n = 0;

    for (i = 0; i < sizeof(dll_descs) / sizeof(dll_descs[0]); i++) {
        if (load_one(&dll_descs[i], i, &mods[n]) != 0) {
            kprint("win32: no cargo ");
            kprint(dll_descs[i].fs_name);
            kprint("\n");
            continue;
        }
        n++;
    }
    mod_count = n;
    return n > 0 ? 0 : -1;
}

/* Mapea los segmentos PT_LOAD del binario (enlazado a la base fija) a
 * paginas USER en su VA absoluta (p_vaddr). El binario tiene las
 * secciones a offset 0x1000+ en el archivo, asi que un mapeo lineal
 * (base + file_offset) dejaria la cabecera ELF en la base y el codigo
 * en otro lado. */
static int map_module(uint32_t pd, int idx)
{
    win32_module_t *m = &mods[idx];
    const elf32_ehdr_t *h = (const elf32_ehdr_t *)m->bin;
    uint32_t i;
    uint32_t old_base = 0, delta = 0;

    /* Fase 20-C: el binario se enlazo a su base fija (primer PT_LOAD);
     * lo mapeamos en m->base (que puede ser distinta si WIN32_RELOC_DELTA
     * es != 0). delta desplaza las VAs de los segmentos. */
    for (i = 0; i < h->e_phnum; i++) {
        const elf32_phdr_t *ph =
            (const elf32_phdr_t *)(m->bin + h->e_phoff) + i;
        if (ph->p_type == 1 && ph->p_vaddr != 0) {
            old_base = ph->p_vaddr & ~0xFFFu;
            break;
        }
    }
    delta = m->base - old_base;

    for (i = 0; i < h->e_phnum; i++) {
        const elf32_phdr_t *ph =
            (const elf32_phdr_t *)(m->bin + h->e_phoff) + i;
        uint32_t va, off, total;

        if (ph->p_type != 1)    /* PT_LOAD */
            continue;
        va = ph->p_vaddr + delta;
        off = ph->p_offset;
        total = ph->p_memsz;
        while (total > 0) {
            uint32_t frame = pmm_alloc_frame();
            uint32_t n = total > PAGE_SIZE ? PAGE_SIZE : total;

            if (frame == 0)
                return -1;
            memset((void *)frame, 0, PAGE_SIZE);
            if (off < ph->p_offset + ph->p_filesz) {
                uint32_t nf = ph->p_offset + ph->p_filesz - off;
                if (nf > n)
                    nf = n;
                memcpy((void *)frame, m->bin + off, nf);
                off += nf;
            }
            if (paging_user_map_frame(pd, va, frame) != 0) {
                pmm_free_frame(frame);
                return -1;
            }
            va += PAGE_SIZE;
            total -= n;
        }
    }
    return 0;
}

int win32_map(uint32_t pd, uint32_t mod_idx)
{
    if (mod_idx >= (uint32_t)mod_count)
        return -1;
    return map_module(pd, (int)mod_idx);
}

int win32_map_all(uint32_t pd)
{
    int i;
    for (i = 0; i < mod_count; i++) {
        kprint("m");
        kprint_uint((uint32_t)i);
        kprint(":");
        if (map_module(pd, i) != 0)
            return -1;
    }
    if (paging_user_map_lfb(pd) != 0)
        return -1;
    return win32_tib_map(pd);
}

/* --- TIB de las tareas de usuario (CRT mingw) --- */
/* El CRT mingw lee %fs:0x18 -> TIB.Self y [Self+4] = StackBase al
 * arrancar. Con el segmento FS de la GDT (base = WIN32_TIB_VA) y una
 * pagina USER por tarea aqui, cada PD ve su propio TIB. */

int win32_tib_map(uint32_t pd)
{
    uint32_t frame = pmm_alloc_frame();
    uint32_t *tib;
    if (frame == 0)
        return -1;
    tib = (uint32_t *)frame;            /* identity map del kernel */
    tib[0x00 / 4] = 0xFFFFFFFF;         /* SEH frame head (ninguno) */
    tib[0x04 / 4] = USER_ESP0_TOP;      /* StackBase */
    tib[0x08 / 4] = USER_ESP0_TOP - USER_STACK_SIZE;   /* StackLimit */
    tib[0x18 / 4] = WIN32_TIB_VA;       /* Self: %fs:0x18 */
    return paging_user_map_frame(pd, WIN32_TIB_VA, frame);
}

/* Linea de comandos del proceso (GetCommandLineA): se copia al TIB de
 * la tarea en WIN32_TIB_CMDLINE_OFF; ring 3 la lee por %fs. */
int win32_tib_set_cmdline(uint32_t pd, const char *cmd)
{
    uint32_t frame, i;
    char *dst;

    frame = paging_user_frame(pd, WIN32_TIB_VA);
    if (frame == 0)
        return -1;
    dst = (char *)(frame + WIN32_TIB_CMDLINE_OFF);
    for (i = 0; i < WIN32_TIB_CMDLINE_LEN - 1 && cmd[i]; i++)
        dst[i] = cmd[i];
    dst[i] = 0;
    return 0;
}

/* Escribe en [USER_ESP0_INIT] la direccion de msvcrt!_crt_ret: es el
 * "return address" que el CRT de mingw deja ubicado en la base de la
 * pila para el ret final de mainCRTStartup (lea esp,ecx-4 / ret). */
int win32_crt_ret_init(uint32_t pd)
{
    uint32_t ret_va = win32_resolve("msvcrt.dll", "_crt_ret");
    uint32_t frame;
    if (ret_va == 0)
        return -1;
    frame = paging_user_frame(pd, USER_ESP0_INIT & ~0xFFFu);
    if (frame == 0)
        return -1;
    *(uint32_t *)(frame + (USER_ESP0_INIT & 0xFFF)) = ret_va;
    return 0;
}

/* Compara nombres ignorando mayusculas: los .exe de mingw importan
 * "KERNEL32.dll", nuestros modulos se llaman "kernel32.dll". */
static int name_ci_eq(const char *a, const char *b)
{
    for (;;) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb)
            return 0;
        if (ca == 0)
            return 1;
        a++;
        b++;
    }
}

/* Indice de modulo por nombre de DLL (ignorando mayusculas). */
static int find_module(const char *dname)
{
    int i;

    for (i = 0; i < mod_count; i++) {
        if (mods[i].dll_idx < sizeof(dll_descs) / sizeof(dll_descs[0])
            && name_ci_eq(dll_descs[mods[i].dll_idx].dll_name, dname))
            return i;
    }
    return -1;
}

uint32_t win32_resolve(const char *dll, const char *fn)
{
    int idx = find_module(dll);

    if (idx < 0)
        return 0;
    return module_find_export(&mods[idx], fn);
}

uint32_t win32_module_base(const char *dll)
{
    int idx = find_module(dll);

    if (idx < 0)
        return 0;
    return mods[idx].base;
}

uint32_t win32_resolve_base(uint32_t base, const char *fn)
{
    int i;
    for (i = 0; i < mod_count; i++)
        if (mods[i].base == base)
            return module_find_export(&mods[i], fn);
    return 0;
}

int win32_count(void)
{
    return mod_count;
}