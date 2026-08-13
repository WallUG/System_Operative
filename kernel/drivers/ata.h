/* MyOS - kernel/drivers/ata.h */
#ifndef MYOS_ATA_H
#define MYOS_ATA_H

#include <stdint.h>

/* Lee 1 sector (512 B) del canal primario (maestro) a buffer.
 * Devuelve 0 en exito, -1 en error/timeout. */
int ata_read_sector(uint32_t lba, uint8_t *buffer);

/* Escribe 1 sector (512 B) al canal primario (maestro).
 * Devuelve 0 en exito, -1 en error/timeout. */
int ata_write_sector(uint32_t lba, const uint8_t *buffer);

#endif
