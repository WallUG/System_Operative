/* MyOS - kernel/fs/mefs.h
 * MEFS (MyOS Easy FS): filesystem propio de Fase 6, solo lectura.
 *
 * Layout en el disco (sectores de 512 B), relativo al inicio del FS:
 *   sector 0 : superbloque: 8 B magic "MEFS01\n", uint32 num_files,
 *              uint32 dir_lba, uint32 dir_size (bytes)
 *   sector 1 : directorio: num_files entradas de 32 B:
 *              char name[16], uint32 size, uint32 lba, uint32 unused
 *   siguientes: datos de cada archivo en sectores contiguos (el lba de
 *              la entrada es el sector absoluto del disco).
 * En la imagen os-image.bin el FS empieza en el sector 64 (LBA 64):
 *   0 boot | 1..63 kernel | 64.. FS. */

#ifndef MYOS_MEFS_H
#define MYOS_MEFS_H

#include <stdint.h>

#define MEFS_NAME_LEN   16
#define MEFS_MAX_FILES  32
#define MEFS_FS_START   129         /* LBA donde empieza el FS (tras boot + kernel) */

typedef struct {
    char     name[MEFS_NAME_LEN];
    uint32_t size;
    uint32_t lba;                   /* sector absoluto del disco */
    uint32_t unused;
} mefs_entry_t;

/* Lee el superbloque + directorio a RAM (memoria estatica) desde el
 * disco ATA (modo disco). */
int  mefs_init(void);
/* Igual pero desde una imagen del FS ya copiada en RAM (arranque CD):
 * sector 0 de la imagen == LBA MEFS_FS_START del disco. */
int  mefs_init_mem(const uint8_t *image, uint32_t size);
/* Numero de archivos. */
int  mefs_file_count(void);
/* Copia nombre del i-esimo archivo a name (max MEFS_NAME_LEN bytes). */
int  mefs_list(uint32_t i, char *name);
/* Devuelve el tamano del archivo, -1 si no existe. */
int  mefs_size(const char *name);
/* Copia 'max' bytes del archivo a buffer; devuelve bytes leidos o -1. */
int  mefs_read(const char *name, uint8_t *buffer, uint32_t max);

#endif
