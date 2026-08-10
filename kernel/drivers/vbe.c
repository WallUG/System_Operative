/* MyOS - kernel/drivers/vbe.c
 * Modo grafico VBE via la interfaz "dispi" de Bochs/QEMU. No hace falta
 * real mode: se programan los registros por puertos I/O (0x01CE/0x01CF)
 * y el LFB queda activo. La direccion fisica del LFB NO es fija: se
 * lee del BAR0 del controlador VGA (1234:1111) escaneando el bus PCI.
 *
 * Secuencia estandar (VBE_DISPI spec):
 *   index 0x1 XRES, 0x2 YRES, 0x3 BPP, 0x4 ENABLE (bit0 = display,
 *   bit6 = LFB enabled). */

#include <stdint.h>
#include "vbe.h"
#include "../io.h"
#include "../kprint.h"

#define VBE_DISPI_IOPORT_INDEX 0x01CE
#define VBE_DISPI_IOPORT_DATA  0x01CF

#define VBE_DISPI_INDEX_XRES   0x1
#define VBE_DISPI_INDEX_YRES   0x2
#define VBE_DISPI_INDEX_BPP    0x3
#define VBE_DISPI_INDEX_ENABLE 0x4

#define VBE_DISPI_ENABLED      0x1
#define VBE_DISPI_LFB_ENABLED  0x40

/* Direccion fisica del LFB (leida del BAR0 del VGA por PCI). */
uint32_t vbe_lfb_phys = 0;
int vbe_graphics_active = 0;

/* --- PCI config space (0xCF8/0xCFC) --- */

#define PCI_CONFIG_ADDR 0x0CF8u
#define PCI_CONFIG_DATA 0x0CFCu

static uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t fn,
                           uint8_t reg)
{
    outl(PCI_CONFIG_ADDR, 0x80000000u | ((uint32_t)bus << 16)
                          | ((uint32_t)dev << 11) | ((uint32_t)fn << 8)
                          | (reg & 0xFC));
    return inl(PCI_CONFIG_DATA);
}

static uint32_t vbe_find_lfb(void)
{
    uint8_t bus, dev, fn;

    for (bus = 0; bus < 8; bus++) {
        for (dev = 0; dev < 32; dev++) {
            for (fn = 0; fn < 8; fn++) {
                uint32_t id = pci_read32(bus, dev, fn, 0);
                if ((id & 0xFFFF) == 0xFFFF)
                    break;          /* sin dispositivo: salir del fn */
                if ((id & 0xFFFF) == 0x1234 && (id >> 16) == 0x1111)
                    return pci_read32(bus, dev, fn, 0x10) & 0xFFFFFFF0u;
            }
        }
    }
    return 0;
}

void vbe_init(void)
{
    vbe_lfb_phys = vbe_find_lfb();
    if (vbe_lfb_phys == 0) {
        kprint("VBE: no se encontro el VGA (1234:1111) por PCI\n");
        return;
    }

    outw(VBE_DISPI_IOPORT_INDEX, VBE_DISPI_INDEX_XRES);
    outw(VBE_DISPI_IOPORT_DATA, VBE_SCREEN_W);
    outw(VBE_DISPI_IOPORT_INDEX, VBE_DISPI_INDEX_YRES);
    outw(VBE_DISPI_IOPORT_DATA, VBE_SCREEN_H);
    outw(VBE_DISPI_IOPORT_INDEX, VBE_DISPI_INDEX_BPP);
    outw(VBE_DISPI_IOPORT_DATA, VBE_SCREEN_BPP);
    outw(VBE_DISPI_IOPORT_INDEX, VBE_DISPI_INDEX_ENABLE);
    outw(VBE_DISPI_IOPORT_DATA, VBE_DISPI_ENABLED | VBE_DISPI_LFB_ENABLED);
    vbe_graphics_active = 1;
}
