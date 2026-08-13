/* MyOS - kernel/fs/mefs.h
 * MEFS (MyOS Easy FS): filesystem propio, v2 (Fase 20-A).
 *
 * Layout en el disco (sectores de 512 B), relativo al inicio del FS
 * (LBA absoluto MEFS_FS_START):
 *   sector 0        : superbloque (512 B)
 *   sector 1..+dir  : directorio: MEFS_MAX_FILES entradas de 32 B
 *   +bitmap         : bitmap de bloques libres (1 bit por sector de datos)
 *   +data           : datos de los archivos (bloques asignados via bitmap)
 *
 * Superbloque (offsets dentro del sector 0):
 *   0  magic "MEFS02\n\0" (8 B)
 *   8  uint32 num_files
 *   12 uint32 dir_lba        (absoluto)
 *   16 uint32 dir_size       (bytes)
 *   20 uint32 bitmap_lba     (absoluto)
 *   24 uint32 bitmap_sectors
 *   28 uint32 data_start     (absoluto: primer sector de datos)
 *   32 uint32 fs_capacity    (sectores totales de la region FS del disco)
 *
 * Entrada de directorio (32 B): name[16], size, lba, flags, parent.
 *   flags bit0 = IS_DIR. parent = indice de la entrada del directorio padre;
 *   0xFFFFFFFF (MEFS_ROOT) = raiz.
 */

#ifndef MYOS_MEFS_H
#define MYOS_MEFS_H

#include <stdint.h>

#define MEFS_NAME_LEN   16
#define MEFS_MAX_FILES  64          /* root + subdirectorios */
#define MEFS_FS_START   129         /* LBA donde empieza el FS (boot+kernel) */

#define MEFS_FLAG_DIR   1           /* entrada es un directorio */
#define MEFS_ROOT       0xFFFFFFFFu /* parent de la raiz */

/* offsets del superbloque (v2) */
#define MEFS_SB_NUMFILES 8
#define MEFS_SB_DIRLBA   12
#define MEFS_SB_DIRSIZE  16
#define MEFS_SB_BITMAP   20
#define MEFS_SB_BITMAPSZ 24
#define MEFS_SB_DATA     28
#define MEFS_SB_CAP      32

typedef struct {
    char     name[MEFS_NAME_LEN];
    uint32_t size;
    uint32_t lba;                   /* primer sector de datos (0 = vacio/dir) */
    uint32_t flags;                 /* bit0 = IS_DIR */
    uint32_t parent;                /* indice de la entrada del padre */
} mefs_entry_t;

/* Inicializa desde ATA (modo disco) o imagen RAM (CD). */
int  mefs_init(void);
int  mefs_init_mem(const uint8_t *image, uint32_t size);

/* --- lectura (root; metapad/SYS_DLIST) --- */
/* Numero de archivos (entradas no-directorio de la raiz). */
int  mefs_file_count(void);
/* Copia nombre del i-esimo archivo raiz a name. */
int  mefs_list(uint32_t i, char *name);
int  mefs_size(const char *name);
int  mefs_read(const char *name, uint8_t *buffer, uint32_t max);
int  mefs_read_off(const char *name, uint8_t *buffer, uint32_t off,
                   uint32_t max);
int  mefs_dir_get(uint32_t i, char *name, uint32_t *size);

/* --- escritura (Fase E, v2) --- */
int  mefs_create(const char *name);
int  mefs_write(const char *name, const uint8_t *buf, uint32_t len);
int  mefs_delete(const char *name);
int  mefs_flush(void);
int  mefs_writable(void);

/* --- subdirectorios / formato (Fase 20-A) --- */
/* Enumerar un directorio (parent = indice de entrada dir, MEFS_ROOT para
 * raiz). Devuelve 0 con la entrada idx-esima (name/size/flags), -1 al fin. */
int  mefs_ls(uint32_t parent, uint32_t idx, char *name, uint32_t *size,
             uint32_t *flags);
/* Crea un directorio (parent = indice de la entrada dir padre). */
int  mefs_mkdir(const char *name, uint32_t parent);
/* Busca una entrada llamada 'name' bajo 'parent'; devuelve su indice o -1. */
int  mefs_lookup(uint32_t parent, const char *name);
/* Parent de la entrada 'idx' (MEFS_ROOT si no existe/raiz). */
uint32_t mefs_parent(uint32_t idx);
/* Nombre de la entrada 'idx'. Devuelve 0 o -1 si no existe. */
int  mefs_name(uint32_t idx, char *name);
/* Formatea el disco (modo ATA): resetea directorio + bitmap a limpio.
 * fs_capacity = sectores totales de la region FS. Devuelve 0 o -1. */
int  mefs_format(uint32_t fs_capacity);

#endif
