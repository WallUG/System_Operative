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

/* Desplazamiento dentro del superbloque del campo "next_free_lba" (Fase E:
 * siguiente sector libre para asignar a archivos nuevos/crecidos). */
#define MEFS_SB_FREE    20

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
/* Igual pero empezando en 'off' (lectura posicional, para ReadFile). */
int  mefs_read_off(const char *name, uint8_t *buffer, uint32_t off,
                   uint32_t max);
/* Entrada i-esima del directorio: nombre (hasta 16) + tamano; -1 si no. */
int  mefs_dir_get(uint32_t i, char *name, uint32_t *size);

/* --- escritura (Fase E) ---
 * El directorio y el superbloque se cachean en RAM; mefs_flush() los
 * vuelca al disco (modo ATA). next_free_lba se persiste en el superbloque. */

/* Crea un archivo nuevo (nombre <= 15, no existe). Devuelve 0 o -1.
 * Aun no reserva sectores; se asignan en la primera mefs_write. */
int  mefs_create(const char *name);
/* Escribe 'len' bytes en 'name'. Si el archivo existe lo sobrescribe
 * (reasigna sectores y actualiza size). Devuelve 0 o -1. */
int  mefs_write(const char *name, const uint8_t *buf, uint32_t len);
/* Elimina el archivo (libera su nombre). Devuelve 0 o -1. */
int  mefs_delete(const char *name);
/* Vuelca superbloque (con next_free_lba) + directorio al disco. En modo
 * CD (imagen RAM) no hace nada. Devuelve 0 o -1. */
int  mefs_flush(void);
/* Devuelve 1 si el FS soporta escritura persistente (modo ATA). */
int  mefs_writable(void);

#endif
