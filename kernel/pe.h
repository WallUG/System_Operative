/* MyOS - kernel/pe.h
 * Cargador minimo de PE32 (Windows .exe, machine 0x14C i386) para
 * programas de usuario (plan "Soporte Windows (.exe + Win32)").
 *
 * Solo interesa: DOS header (MZ), e_lfanew, "PE\0\0", COFF header y
 * Optional Header PE32; cada seccion (VirtualAddress absoluta =
 * ImageBase + RVA) se mapea en el PD de destino como paginas USER de
 * 4 KiB en la region 0x80000000-0xBFFFFFFF, copiando los SizeOfRawData
 * bytes que haya en el archivo y dejando el resto (bss virtual) a cero.
 * Igual que elf_load, el kernel copia por la direccion fisica de los
 * frames (identity map), sin tocar CR3.
 *
 * Los .exe producidos por tools/makepe.py se resuelven identicos a los
 * .elf de usuario (mismo ImageBase/entry), asi que conviven en el mismo
 * espacio de usuario. Un .exe puede traer una seccion .idata con la
 * tabla de imports autodescriptiva (slots {dll[16], fname[16]}) que
 * pe_load_resolve_imports rellena contra los modulos Win32 fijos. */

#ifndef MYOS_PE_H
#define MYOS_PE_H

#include <stdint.h>

/* Crea un PD nuevo, mapea las secciones del PE32 en buf (memoria del
 * kernel) y devuelve PD y direccion de entrada (ImageBase + entry RVA).
 * 0 = OK, -1 = PE invalido o sin memoria. */
int pe_load(const void *buf, uint32_t size, uint32_t *pd, uint32_t *entry);

/* Igual pero sobre un PD existente (exec de un .exe): libera el espacio
 * de usuario previo y carga el nuevo. En error el espacio de usuario
 * queda liberado: el llamador debe matar la tarea. */
int pe_load_into(uint32_t pd, const void *buf, uint32_t size,
                 uint32_t *entry);

/* Tabla de modulos Win32 fijos (ver kernel/win32.c). Cada modulo es una
 * imagen (ELF32) cargada a una direccion virtual fija de usuario con su
 * propia lista de exports. */
#define WIN32_MAX_MODULES   4

struct win32_module {
    const char *name;      /* "kernel32.dll", ... (15 chars max) */
    uint32_t    base;      /* direccion virtual fija (region DLL) */
    uint32_t    entry;      /* no usado */
};

#endif