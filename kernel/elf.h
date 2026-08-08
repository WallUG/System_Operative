/* MyOS - kernel/elf.h
 * Cargador minimo de ELF32 (ET_EXEC, i386) para programas de usuario.
 *
 * Solo interesa el encabezado y los segmentos PT_LOAD: cada segmento se
 * mapea en el PD de destino como paginas USER de 4 KiB (region
 * 0x80000000-0xBFFFFFFF), se copian los p_filesz bytes de datos y el
 * resto (bss) queda a cero. El kernel copia por la direccion fisica de
 * los frames (identity map 0-1 GiB), asi que no hace falta tocar CR3. */

#ifndef MYOS_ELF_H
#define MYOS_ELF_H

#include <stdint.h>

/* Crea un PD nuevo, mapea los segmentos del ELF en el buffer buf (en
 * memoria del kernel) y devuelve el PD y la direccion de entrada.
 * Devuelve 0 en exito, -1 si el ELF es invalido o no hay memoria. */
int elf_load(const void *buf, uint32_t size, uint32_t *pd, uint32_t *entry);
/* Igual pero sobre un PD ya existente (exec): libera el espacio de
 * usuario anterior y carga los segmentos. En error el espacio de
 * usuario queda liberado: el llamador debe matar la tarea. */
int elf_load_into(uint32_t pd, const void *buf, uint32_t size,
                  uint32_t *entry);

#endif
