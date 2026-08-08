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
};

/* Extrae la tabla .exports del binario ELF (copiada a mods). */
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

    for (i = 0; i < h->e_shnum; i++) {
        if (strcmp(shstr + sh[i].sh_name, ".exports") != 0)
            continue;
        uint32_t n = sh[i].sh_size / sizeof(win32_export_t);
        if (n > WIN32_EXPORT_MAX)
            n = WIN32_EXPORT_MAX;
        m->n_exports = n;
        memcpy(m->exports, bin + sh[i].sh_offset,
               n * sizeof(win32_export_t));
        return 0;
    }
    return -1;
}

/* El linkage script tools/dll.ld ancla la seccion .exports en la pagina
 * de la base? no: puede estar en cualquier VA; la resolvemos aqui: cada
 * export guarda fn = VA-absoluta del modulo (el ELF esta enlazado a la
 * base fija, asi que fn ya es la direccion correcta en el PD). */

static int load_one(const struct dll_desc *d, win32_module_t *m)
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
    m->base = d->base;
    return 0;
}

int win32_init(void)
{
    uint32_t i;
    int n = 0;

    for (i = 0; i < 3; i++) {
        if (load_one(&dll_descs[i], &mods[n]) != 0) {
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

    for (i = 0; i < h->e_phnum; i++) {
        const elf32_phdr_t *ph =
            (const elf32_phdr_t *)(m->bin + h->e_phoff) + i;
        uint32_t va, off, total;

        if (ph->p_type != 1)    /* PT_LOAD */
            continue;
        va = ph->p_vaddr;
        off = ph->p_offset;
        total = ph->p_filesz;
        while (total > 0) {
            uint32_t frame = pmm_alloc_frame();
            uint32_t n = total > PAGE_SIZE ? PAGE_SIZE : total;

            if (frame == 0)
                return -1;
            memset((void *)frame, 0, PAGE_SIZE);
            memcpy((void *)frame, m->bin + off, n);
            if (paging_user_map_frame(pd, va, frame) != 0) {
                pmm_free_frame(frame);
                return -1;
            }
            va += PAGE_SIZE;
            off += n;
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
    return 0;
}

/* Indice de modulo por nombre de DLL (comparando con dll_descs). */
static int find_module(const char *dname)
{
    int i;

    for (i = 0; i < mod_count; i++) {
        uint32_t bi = (mods[i].base - WIN32_REGION_BASE) / DLL_BASE_STEP;
        if (bi < 3 && strcmp(dll_descs[bi].dll_name, dname) == 0)
            return i;
    }
    return -1;
}

uint32_t win32_resolve(const char *dll, const char *fn)
{
    win32_module_t *m;
    uint32_t j;
    int idx = find_module(dll);

    if (idx < 0)
        return 0;
    m = &mods[idx];
    for (j = 0; j < m->n_exports; j++) {
        if (strcmp(m->exports[j].name, fn) == 0)
            return m->exports[j].fn;   /* VA fija en el PD */
    }
    return 0;
}

int win32_count(void)
{
    return mod_count;
}