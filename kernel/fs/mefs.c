/* MyOS - kernel/fs/mefs.c
 * MEFS v2 (Fase 20-A): superbloque + directorio + bitmap de bloques libres
 * + datos. Soporta escritura persistente (ATA) y subdirectorios.
 *
 * La fuente de sectores es transparente: fs_read_sector/fs_write_sector
 * despachan a ATA (modo disco) o a la imagen RAM (modo CD, solo lectura).
 */

#include <stdint.h>
#include <string.h>
#include "mefs.h"
#include "drivers/ata.h"

#define MEFS_MAGIC "MEFS02\n"

#define MEFS_SB_BITMAPSZ_NONE 0

static mefs_entry_t entries[MEFS_MAX_FILES];
static int file_count;

/* parametros del superbloque (v2) */
static uint32_t bitmap_lba;
static uint32_t bitmap_sectors;
static uint32_t data_start;
static uint32_t fs_capacity;

static uint8_t sector_buf[512];

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

/* --- bitmap de bloques libres --- */

static int bm_test(uint32_t bit)
{
    uint8_t byte;
    uint32_t sect = bitmap_lba + bit / 4096;    /* 4096 bits/sector */
    uint32_t b = bit % 4096;
    if (fs_read_sector(sect, sector_buf) != 0)
        return 0;
    byte = sector_buf[b / 8];
    return (byte >> (b % 8)) & 1;
}

/* Marca el bit 'bit' (set=usado, clear=libre) y persiste el sector bitmap. */
static int bm_set(uint32_t bit, int used)
{
    uint32_t sect = bitmap_lba + bit / 4096;
    uint32_t b = bit % 4096;
    if (fs_read_sector(sect, sector_buf) != 0)
        return -1;
    if (used)
        sector_buf[b / 8] |= (uint8_t)(1u << (b % 8));
    else
        sector_buf[b / 8] &= (uint8_t)~(1u << (b % 8));
    return fs_write_sector(sect, sector_buf);
}

/* Asigna 'n' bloques contiguos libres; devuelve el primer lba o 0 si no hay. */
static uint32_t bm_alloc(uint32_t n)
{
    uint32_t count = fs_capacity;
    uint32_t start = 0, run = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (bm_test(i)) {
            run = 0;
        } else {
            if (run == 0)
                start = i;
            run++;
            if (run == n) {
                for (uint32_t j = start; j < start + n; j++)
                    bm_set(j, 1);
                return data_start + start;
            }
        }
    }
    return 0;
}

static int bm_free(uint32_t lba, uint32_t n)
{
    if (lba < data_start)
        return -1;
    for (uint32_t j = 0; j < n; j++)
        bm_set((lba - data_start) + j, 0);
    return 0;
}

/* --- carga --- */

static int mefs_load(void)
{
    uint8_t sb[512];
    uint32_t dir_lba, dir_size, s;
    uint8_t dir_buf[MEFS_MAX_FILES * 32];

    if (fs_read_sector(MEFS_FS_START, sb) != 0)
        return -1;
    if (memcmp(sb, MEFS_MAGIC, 8) != 0)
        return -1;

    file_count = *(uint32_t *)(sb + MEFS_SB_NUMFILES);
    if (file_count > MEFS_MAX_FILES)
        file_count = MEFS_MAX_FILES;

    bitmap_lba    = *(uint32_t *)(sb + MEFS_SB_BITMAP);
    bitmap_sectors= *(uint32_t *)(sb + MEFS_SB_BITMAPSZ);
    data_start    = *(uint32_t *)(sb + MEFS_SB_DATA);
    fs_capacity   = *(uint32_t *)(sb + MEFS_SB_CAP);

    dir_lba  = *(uint32_t *)(sb + MEFS_SB_DIRLBA);
    dir_size = *(uint32_t *)(sb + MEFS_SB_DIRSIZE);
    for (s = 0; s < (dir_size + 511) / 512; s++) {
        if (fs_read_sector(dir_lba + s, dir_buf + s * 512) != 0)
            return -1;
    }
    for (int i = 0; i < file_count; i++) {
        uint32_t *e = (uint32_t *)(dir_buf + i * 32);
        for (int j = 0; j < MEFS_NAME_LEN; j++)
            entries[i].name[j] = ((uint8_t *)e)[j];
        entries[i].name[MEFS_NAME_LEN - 1] = 0;
        entries[i].size    = e[4];
        entries[i].lba     = e[5];
        entries[i].flags   = e[6];
        entries[i].parent  = e[7];
    }
    return 0;
}

int mefs_init(void)
{
    fs_ram = NULL;
    fs_ram_sectors = 0;
    return mefs_load();
}

int mefs_init_mem(const uint8_t *image, uint32_t size)
{
    fs_ram = image;
    fs_ram_sectors = size / 512;
    return mefs_load();
}

int mefs_writable(void)
{
    return (fs_ram == NULL) ? 1 : 0;
}

/* --- lectura raiz (metapad / SYS_DLIST) --- */

static int is_root_file(const mefs_entry_t *e)
{
    return e->name[0] != 0 && !(e->flags & MEFS_FLAG_DIR) &&
           e->parent == MEFS_ROOT;
}

int mefs_file_count(void)
{
    int n = 0;
    for (int i = 0; i < file_count; i++)
        if (is_root_file(&entries[i]))
            n++;
    return n;
}

int mefs_list(uint32_t i, char *name)
{
    int n = 0;
    for (int k = 0; k < file_count; k++) {
        if (is_root_file(&entries[k])) {
            if (n == (int)i) {
                for (int j = 0; j < MEFS_NAME_LEN; j++)
                    name[j] = entries[k].name[j];
                return 0;
            }
            n++;
        }
    }
    return -1;
}

int mefs_dir_get(uint32_t i, char *name, uint32_t *size)
{
    if (mefs_list(i, name) != 0)
        return -1;
    *size = (uint32_t)mefs_size(name);
    return 0;
}

int mefs_size(const char *name)
{
    for (int i = 0; i < file_count; i++)
        if (entries[i].name[0] && strcmp(entries[i].name, name) == 0)
            return (int)entries[i].size;
    return -1;
}

int mefs_read(const char *name, uint8_t *buffer, uint32_t max)
{
    int idx = -1;
    for (int i = 0; i < file_count; i++)
        if (entries[i].name[0] && strcmp(entries[i].name, name) == 0) {
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
        if (entries[i].name[0] && strcmp(entries[i].name, name) == 0) {
            idx = i;
            break;
        }
    if (idx < 0)
        return -1;

    uint32_t size = entries[idx].size;
    uint32_t lba  = entries[idx].lba;
    uint32_t left, skip;

    if (off >= size)
        return 0;
    left = size - off < max ? size - off : max;
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

/* --- escritura --- */

static int find_free_slot(void)
{
    for (int i = 0; i < MEFS_MAX_FILES; i++)
        if (entries[i].name[0] == 0)
            return i;
    return -1;
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
        return -1;
    i = find_free_slot();
    if (i < 0)
        return -1;
    for (int j = 0; j <= len; j++)
        entries[i].name[j] = name[j];
    entries[i].size = 0;
    entries[i].lba  = 0;
    entries[i].flags = 0;
    entries[i].parent = MEFS_ROOT;
    if (i >= file_count)
        file_count = i + 1;
    return 0;
}

int mefs_write(const char *name, const uint8_t *buf, uint32_t len)
{
    int idx = -1, i;
    uint32_t sectors, lba, left, off;

    for (i = 0; i < file_count; i++)
        if (entries[i].name[0] && strcmp(entries[i].name, name) == 0) {
            idx = i;
            break;
        }
    if (idx < 0)
        return -1;
    if (fs_ram != NULL)
        return -1;

    /* libera bloques viejos del archivo, luego asigna nuevos contiguos */
    if (entries[idx].lba != 0 && entries[idx].size > 0)
        bm_free(entries[idx].lba, (entries[idx].size + 511) / 512);

    sectors = (len + 511) / 512;
    if (sectors == 0) {
        entries[idx].size = 0;
        entries[idx].lba = 0;
        return 0;
    }
    lba = bm_alloc(sectors);
    if (lba == 0)
        return -1;

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
        if (entries[i].name[0] && strcmp(entries[i].name, name) == 0) {
            if (entries[i].flags & MEFS_FLAG_DIR) {
                /* no vaciar directorios con hijos por ahora */
                for (int k = 0; k < file_count; k++)
                    if (entries[k].name[0] && entries[k].parent == (uint32_t)i)
                        return -1;
            }
            if (entries[i].lba != 0 && entries[i].size > 0)
                bm_free(entries[i].lba, (entries[i].size + 511) / 512);
            entries[i].name[0] = 0;
            entries[i].size = 0;
            entries[i].lba = 0;
            return 0;
        }
    return -1;
}

int mefs_flush(void)
{
    uint8_t sb[512], dir[MEFS_MAX_FILES * 32];
    uint32_t dir_lba, dir_size, s;

    if (fs_ram != NULL)
        return 0;

    dir_lba  = MEFS_FS_START + 1;
    dir_size = (uint32_t)file_count * 32;
    for (s = 0; s < 512; s++)
        sb[s] = 0;
    for (s = 0; s < 8; s++)
        sb[s] = (uint8_t)MEFS_MAGIC[s];
    *(uint32_t *)(sb + MEFS_SB_NUMFILES) = (uint32_t)file_count;
    *(uint32_t *)(sb + MEFS_SB_DIRLBA)   = dir_lba;
    *(uint32_t *)(sb + MEFS_SB_DIRSIZE)  = dir_size;
    *(uint32_t *)(sb + MEFS_SB_BITMAP)   = bitmap_lba;
    *(uint32_t *)(sb + MEFS_SB_BITMAPSZ) = bitmap_sectors;
    *(uint32_t *)(sb + MEFS_SB_DATA)     = data_start;
    *(uint32_t *)(sb + MEFS_SB_CAP)      = fs_capacity;

    for (s = 0; s < (uint32_t)(file_count * 32); s++)
        dir[s] = 0;
    for (int i = 0; i < file_count; i++) {
        uint32_t *e = (uint32_t *)(dir + i * 32);
        for (int j = 0; j < MEFS_NAME_LEN; j++)
            ((uint8_t *)e)[j] = (uint8_t)entries[i].name[j];
        e[4] = entries[i].size;
        e[5] = entries[i].lba;
        e[6] = entries[i].flags;
        e[7] = entries[i].parent;
    }

    if (fs_write_sector(MEFS_FS_START, sb) != 0)
        return -1;
    for (s = 0; s < (dir_size + 511) / 512; s++)
        if (fs_write_sector(dir_lba + s, dir + s * 512) != 0)
            return -1;
    return 0;
}

/* --- subdirectorios --- */

int mefs_ls(uint32_t parent, uint32_t idx, char *name, uint32_t *size,
            uint32_t *flags)
{
    uint32_t n = 0;
    for (int i = 0; i < file_count; i++) {
        if (entries[i].name[0] && entries[i].parent == parent) {
            if (n == idx) {
                for (int j = 0; j < MEFS_NAME_LEN; j++)
                    name[j] = entries[i].name[j];
                if (size) *size = entries[i].size;
                if (flags) *flags = entries[i].flags;
                return 0;
            }
            n++;
        }
    }
    return -1;
}

int mefs_mkdir(const char *name, uint32_t parent)
{
    int i, len;
    if (name == 0)
        return -1;
    len = 0;
    while (name[len])
        len++;
    if (len == 0 || len > MEFS_NAME_LEN - 1)
        return -1;
    if (mefs_lookup(parent, name) >= 0)
        return -1;
    i = find_free_slot();
    if (i < 0)
        return -1;
    for (int j = 0; j <= len; j++)
        entries[i].name[j] = name[j];
    entries[i].size = 0;
    entries[i].lba  = 0;
    entries[i].flags = MEFS_FLAG_DIR;
    entries[i].parent = parent;
    if (i >= file_count)
        file_count = i + 1;
    return 0;
}

int mefs_lookup(uint32_t parent, const char *name)
{
    for (int i = 0; i < file_count; i++)
        if (entries[i].name[0] && entries[i].parent == parent &&
            strcmp(entries[i].name, name) == 0)
            return i;
    return -1;
}

uint32_t mefs_parent(uint32_t idx)
{
    if (idx == MEFS_ROOT || idx >= (uint32_t)file_count ||
        entries[idx].name[0] == 0)
        return MEFS_ROOT;
    return entries[idx].parent;
}

int mefs_name(uint32_t idx, char *name)
{
    if (idx == MEFS_ROOT || idx >= (uint32_t)file_count ||
        entries[idx].name[0] == 0)
        return -1;
    for (int j = 0; j < MEFS_NAME_LEN; j++)
        name[j] = entries[idx].name[j];
    return 0;
}

/* --- formato (Fase 20-A) --- */

int mefs_format(uint32_t capacity)
{
    uint8_t sb[512], dir[MEFS_MAX_FILES * 32], zero[512];
    uint32_t dir_lba, dir_size, bitmap_sz, cap_blocks, s;

    if (fs_ram != NULL)
        return -1;

    dir_lba = MEFS_FS_START + 1;
    dir_size = (uint32_t)MEFS_MAX_FILES * 32;

    /* bitmap: 1 bit por sector de datos. Datos desde dir_lba + dir_sectores.
     * Se reservan sectores de bitmap para cubrir 'capacity' bits. */
    bitmap_sz = (capacity + 4095) / 4096;   /* bits -> sectores (4096 bits/s) */
    if (bitmap_sz == 0)
        bitmap_sz = 1;
    data_start = dir_lba + (dir_size + 511) / 512 + bitmap_sz;
    cap_blocks = capacity;

    /* resetea directorio (disco + RAM) */
    memset(dir, 0, sizeof(dir));
    memset(entries, 0, sizeof(entries));
    file_count = 0;

    /* bitmap: todos libres (0) */
    memset(zero, 0, sizeof(zero));
    for (s = 0; s < bitmap_sz; s++)
        if (fs_write_sector(dir_lba + (dir_size + 511) / 512 + s, zero) != 0)
            return -1;

    /* superbloque */
    memset(sb, 0, sizeof(sb));
    for (s = 0; s < 8; s++)
        sb[s] = (uint8_t)MEFS_MAGIC[s];
    *(uint32_t *)(sb + MEFS_SB_NUMFILES) = 0;
    *(uint32_t *)(sb + MEFS_SB_DIRLBA)   = dir_lba;
    *(uint32_t *)(sb + MEFS_SB_DIRSIZE)  = dir_size;
    *(uint32_t *)(sb + MEFS_SB_BITMAP)   = dir_lba + (dir_size + 511) / 512;
    *(uint32_t *)(sb + MEFS_SB_BITMAPSZ) = bitmap_sz;
    *(uint32_t *)(sb + MEFS_SB_DATA)     = data_start;
    *(uint32_t *)(sb + MEFS_SB_CAP)      = cap_blocks;

    if (fs_write_sector(MEFS_FS_START, sb) != 0)
        return -1;
    for (s = 0; s < (dir_size + 511) / 512; s++)
        if (fs_write_sector(dir_lba + s, dir + s * 512) != 0)
            return -1;

    /* actualiza parametros en RAM */
    bitmap_lba = *(uint32_t *)(sb + MEFS_SB_BITMAP);
    bitmap_sectors = bitmap_sz;
    data_start = *(uint32_t *)(sb + MEFS_SB_DATA);
    fs_capacity = cap_blocks;
    return 0;
}
