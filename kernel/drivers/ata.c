/* MyOS - kernel/drivers/ata.c
 * Driver ATA PIO (puertos 0x1F0-0x1F7, canal primario maestro).
 * Lee sectores de 512 bytes por PIO (sin DMA, sin interrupciones).
 * El filesystem de Fase 6 vive en el mismo disco que el kernel. */

#include <stdint.h>
#include "ata.h"
#include "io.h"

#define ATA_DATA      0x1F0
#define ATA_ERR       0x1F1
#define ATA_SECTOR_CNT 0x1F2
#define ATA_LBA_LO    0x1F3
#define ATA_LBA_MID   0x1F4
#define ATA_LBA_HI    0x1F5
#define ATA_DRIVE_SEL 0x1F6
#define ATA_CMD       0x1F7
#define ATA_STATUS    0x1F7

#define ATA_DRV_MASTER 0xE0
#define ATA_CMD_READ   0x20      /* READ SECTORS (LBA28) */

/* Espera hasta 4 s; devuelve 0 cuando hay datos listos (DRQ=1, sin ERR).
 * Registro de status: bit7 BSY, bit6 DRDY, bit5 DF, bit3 DRQ, bit0 ERR. */
static int ata_wait_drq(void)
{
    for (uint32_t tries = 0; tries < 4000000; tries++) {
        uint8_t st = inb(ATA_STATUS);
        if (st & 0x80)                  /* BSY: aun ocupado */
            continue;
        if (st & 0x01)                  /* ERR */
            return -1;
        if (st & 0x08)                  /* DRQ: datos listos */
            return 0;
        if (tries % 100000 == 0)
            io_wait();
    }
    return -1;
}

int ata_read_sector(uint32_t lba, uint8_t *buffer)
{
    outb(ATA_DRIVE_SEL, ATA_DRV_MASTER | ((lba >> 24) & 0x0F));
    outb(ATA_SECTOR_CNT, 1);
    outb(ATA_LBA_LO, (uint8_t)(lba & 0xFF));
    outb(ATA_LBA_MID, (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_LBA_HI, (uint8_t)((lba >> 16) & 0xFF));
    outb(ATA_CMD, ATA_CMD_READ);

    if (ata_wait_drq() != 0)
        return -1;

    for (int i = 0; i < 256; i++) {
        uint16_t w = inw(ATA_DATA);
        buffer[i * 2]     = (uint8_t)(w & 0xFF);
        buffer[i * 2 + 1] = (uint8_t)(w >> 8);
    }
    return 0;
}
