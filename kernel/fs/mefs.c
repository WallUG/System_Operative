/* MyOS - kernel/fs/mefs.c
 * MEFS: filesystem propio, solo lectura, sobre el driver ATA PIO o
 * sobre una imagen del FS ya copiada en RAM (arranque por CD, Fase 7).
 * El directorio completo se cachea en RAM en mefs_init()/mefs_init_mem().
 *
 * La fuente de sectores es transparente: fs_read_sector despacha a ATA
 * (lba absoluto del disco) o a la imagen RAM (lba - MEFS_FS_START). */

#include <stdint.h>
#include <string.h>
#include "mefs.h"
#include "drivers/ata.h"

#define MEFS_MAGIC "MEFS01\n"

static mefs_entry_t entries[MEFS_MAX_FILES];
static int file_count;

/* Siguiente sector libre (Fase E): se persiste en el superbloque. */
static uint32_t next_free_lba;

/* Direccion virtual del sector lba: el FS es identity map (Fase 4),
 * cada archivo se lee con ata_read_sector a un buffer estatico. */
static uint8_t sector_buf[512];

/* Imagen MEFS en RAM (modo CD): todos los lba del FS son relativos a
 * esta imagen (el sector MEFS_FS_START de disco es el byte 0 de la
 * imagen). NULL = modo disco (ATA). */
static const uint8_t *fs_ram;
static uint32_t fs_ram_sectors;

static int fs_read_sector(uint32_t lba, uint8_t *buf)
{
    if (fs_ram != NULL) {
        uint32_t i;
        if (lba < MEFS_FS_START)
            return -1;
        i = lba - MEFS_FS_START;
        if (i >= fs_ram_sectors)
            return -1;
        memcpy(buf, fs_ram + (size_t)i * 512, 512);
        return 0;
    }
    return ata_read_sector(lba, buf);
}

static int fs_write_sector(uint32_t lba, const uint8_t *buf)
{
    if (fs_ram != NULL)     /* imagen RAM (CD): no persistente */
        return -1;
    return ata_write_sector(lba, buf);
}

static int mefs_load(void)
{
    uint8_t sb[512];
    uint32_t dir_lba, dir_size, s;
    uint8_t dir_buf[MEFS_MAX_FILES * 32];

    if (fs_read_sector(MEFS_FS_START, sb) != 0)
        return -1;
    if (memcmp(sb, MEFS_MAGIC, 8) != 0)
        return -1;

    file_count = *(uint32_t *)(sb + 8);
    if (file_count > MEFS_MAX_FILES)
        file_count = MEFS_MAX_FILES;

    next_free_lba = *(uint32_t *)(sb + MEFS_SB_FREE);
    if (next_free_lba == 0)
        next_free_lba = MEFS_FS_START + 1;   /* fallback */

    /* Directorio: puede ocupar mas de un sector (entradas de 32 B);
     * sector base + tamano vienen en el superbloque. */
    dir_lba  = *(uint32_t *)(sb + 12);
    dir_size = *(uint32_t *)(sb + 16);
    for (s = 0; s < (dir_size + 511) / 512; s++) {
        if (fs_read_sector(dir_lba + s, dir_buf + s * 512) != 0)
            return -1;
    }
    for (int i = 0; i < file_count; i++) {
        uint32_t *e = (uint32_t *)(dir_buf + i * 32);
        for (int j = 0; j < MEFS_NAME_LEN; j++)
            entries[i].name[j] = ((uint8_t *)e)[j];
        entries[i].name[MEFS_NAME_LEN - 1] = 0;
        entries[i].size  = e[4];
        entries[i].lba   = e[5];
        entries[i].unused = 0;
    }
    return 0;
}

int mefs_init(void)
{
    fs_ram = NULL;              /* modo disco: lecturas ATA */
    fs_ram_sectors = 0;
    return mefs_load();
}

int mefs_init_mem(const uint8_t *image, uint32_t size)
{
    fs_ram = image;
    fs_ram_sectors = size / 512;    /* size es multiplo de 512 */
    return mefs_load();
}

int mefs_file_count(void)
{
    return file_count;
}

int mefs_list(uint32_t i, char *name)
{
    if (i >= (uint32_t)file_count)
        return -1;
    for (int j = 0; j < MEFS_NAME_LEN; j++)
        name[j] = entries[i].name[j];
    return 0;
}

int mefs_size(const char *name)
{
    for (int i = 0; i < file_count; i++)
        if (strcmp(entries[i].name, name) == 0)
            return (int)entries[i].size;
    return -1;
}

int mefs_read(const char *name, uint8_t *buffer, uint32_t max)
{
    int idx = -1;
    for (int i = 0; i < file_count; i++)
        if (strcmp(entries[i].name, name) == 0) {
            idx = i;
            break;
        }
    if (idx < 0)
        return -1;

    uint32_t size = entries[idx].size;
    uint32_t lba  = entries[idx].lba;
    uint32_t left = size < max ? size : max;

    while (left > 0) {
        if (fs_read_sector(lba++, sector_buf) != 0)
            return -1;
        uint32_t chunk = left < 512 ? left : 512;
        memcpy(buffer, sector_buf, chunk);
        buffer += chunk;
        left -= chunk;
    }
    return size < max ? (int)size : (int)max;
}

int mefs_read_off(const char *name, uint8_t *buffer, uint32_t off,
                  uint32_t max)
{
    int idx = -1;
    for (int i = 0; i < file_count; i++)
        if (strcmp(entries[i].name, name) == 0) {
            idx = i;
            break;
        }
    if (idx < 0)
        return -1;

    uint32_t size = entries[idx].size;
    uint32_t lba  = entries[idx].lba;
    uint32_t left, skip;

    if (off >= size)
        return 0;               /* EOF */
    left = size - off < max ? size - off : max;

    /* salta los sectores anteriores a off y el byte dentro del primero */
    lba  += off / 512;
    skip  = off % 512;
    while (left > 0) {
        if (fs_read_sector(lba++, sector_buf) != 0)
            return -1;
        uint32_t chunk = 512 - skip < left ? 512 - skip : left;
        memcpy(buffer, sector_buf + skip, chunk);
        buffer += chunk;
        left  -= chunk;
        skip   = 0;
    }
    return size - off < max ? (int)(size - off) : (int)max;
}

int mefs_dir_get(uint32_t i, char *name, uint32_t *size)
{
    if (mefs_list(i, name) != 0)
        return -1;
    *size = (uint32_t)mefs_size(name);
    return 0;
}

int mefs_writable(void)
{
    return (fs_ram == NULL) ? 1 : 0;
}

int mefs_create(const char *name)
{
    int i, len;

    if (name == 0)
        return -1;
    len = 0;
    while (name[len])
        len++;
    if (len == 0 || len > MEFS_NAME_LEN - 1)
        return -1;
    if (mefs_size(name) >= 0)
        return -1;              /* ya existe */
    if (file_count >= MEFS_MAX_FILES)
        return -1;
    for (i = 0; i < file_count; i++)
        if (entries[i].name[0] == 0)
            break;
    if (i == file_count)
        i = file_count++;
    for (int j = 0; j <= len; j++)
        entries[i].name[j] = name[j];
    entries[i].size = 0;
    entries[i].lba  = next_free_lba;
    return 0;
}

int mefs_write(const char *name, const uint8_t *buf, uint32_t len)
{
    int idx = -1, i;
    uint32_t sectors, lba, left, off;

    for (i = 0; i < file_count; i++)
        if (strcmp(entries[i].name, name) == 0) {
            idx = i;
            break;
        }
    if (idx < 0)
        return -1;
    if (fs_ram != NULL)
        return -1;              /* CD: solo lectura */

    sectors = (len + 511) / 512;
    /* asigna sectores: siempre reubica al bloque libre (suficiente y
     * simple; evita fragmentacion). Libera los viejos implicitamente
     * (next_free_lba ya los salto). */
    lba = next_free_lba;
    next_free_lba += sectors;

    off = 0;
    left = len;
    while (left > 0) {
        uint32_t chunk = left < 512 ? left : 512;
        for (i = 0; i < 512; i++)
            sector_buf[i] = (off + i < len) ? buf[off + i] : 0;
        if (fs_write_sector(lba++, sector_buf) != 0)
            return -1;
        off += chunk;
        left -= chunk;
    }

    entries[idx].size = len;
    entries[idx].lba  = lba - sectors;
    return 0;
}

int mefs_delete(const char *name)
{
    int i;

    for (i = 0; i < file_count; i++)
        if (strcmp(entries[i].name, name) == 0) {
            entries[i].name[0] = 0;
            entries[i].size = 0;
            return 0;
        }
    return -1;
}

int mefs_flush(void)
{
    uint8_t sb[512], dir[MEFS_MAX_FILES * 32];
    uint32_t dir_lba, dir_size, s;

    if (fs_ram != NULL)
        return 0;               /* CD: no persistente, no falla */

    /* superbloque: magic + num_files + dir_lba + dir_size + next_free */
    dir_lba  = MEFS_FS_START + 1;
    dir_size = (uint32_t)file_count * 32;
    for (s = 0; s < 512; s++)
        sb[s] = 0;
    for (s = 0; s < 8; s++)
        sb[s] = (uint8_t)MEFS_MAGIC[s];
    *(uint32_t *)(sb + 8)  = (uint32_t)file_count;
    *(uint32_t *)(sb + 12) = dir_lba;
    *(uint32_t *)(sb + 16) = dir_size;
    *(uint32_t *)(sb + MEFS_SB_FREE) = next_free_lba;

    /* directorio en RAM -> sector(es) */
    for (s = 0; s < (uint32_t)(file_count * 32); s++)
        dir[s] = 0;
    for (int i = 0; i < file_count; i++) {
        uint32_t *e = (uint32_t *)(dir + i * 32);
        for (int j = 0; j < MEFS_NAME_LEN; j++)
            ((uint8_t *)e)[j] = (uint8_t)entries[i].name[j];
        e[4] = entries[i].size;
        e[5] = entries[i].lba;
    }

    if (fs_write_sector(MEFS_FS_START, sb) != 0)
        return -1;
    for (s = 0; s < (dir_size + 511) / 512; s++)
        if (fs_write_sector(dir_lba + s, dir + s * 512) != 0)
            return -1;
    return 0;
}
