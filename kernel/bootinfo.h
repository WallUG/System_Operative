/* MyOS - kernel/bootinfo.h
 * ABI bootloader -> kernel (Fase 7): estructura escrita por boot/boot.asm
 * en la direccion fisica fija 0x7000 y pasada como argumento cdecl a
 * kmain (entry.asm la preserva al fijar la pila del kernel).
 *
 * Campos:
 *   magic      'MYOS' (0x4D594F53): valida que haya boot_info.
 *   mode       BOOTINFO_MODE_DISK o BOOTINFO_MODE_CD.
 *   fs_source  DISK: LBA absoluto del sector 0 del FS (MEFS_FS_START);
 *              CD: direccion fisica de la imagen MEFS copiada a RAM.
 *   fs_size    bytes de la imagen MEFS en RAM (solo CD, multiplo de 512).
 *   kernel_addr  direccion de carga del kernel (0x10000, informativo).
 */

#ifndef MYOS_BOOTINFO_H
#define MYOS_BOOTINFO_H

#include <stdint.h>

#define BOOTINFO_ADDR       0x7000u
#define BOOTINFO_MAGIC      0x4D594F53u   /* 'MYOS' */
#define BOOTINFO_MODE_DISK  0
#define BOOTINFO_MODE_CD    1

typedef struct {
    uint32_t magic;
    uint32_t mode;
    uint32_t fs_source;
    uint32_t fs_size;
    uint32_t kernel_addr;
} bootinfo_t;

#endif
