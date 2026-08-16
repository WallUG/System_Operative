/* MyOS - kernel/drivers/ac97.c
 * Audio AC'97 (ICH de QEMU, Fase 24-P4): PCI scan de la clase
 * 0x0401 (audio), reset del codec, tasa 48 kHz, master/PCM al maximo
 * sin mute, y reproduccion por DMA del bus master (NABM) con un
 * buffer descriptor de 8 bytes (formato ICH: length = halfwords-1).
 * Sin IRQ: se sondea el SR (status). El sonido de arranque
 * (ac97_startup_beep) valida el driver en cada boot. */

#include <stdint.h>
#include "io.h"
#include "kprint.h"
#include "mem/heap.h"
#include "drivers/timer.h"

#define PCI_CONFIG_ADDR 0x0CF8u
#define PCI_CONFIG_DATA 0x0CFCu

static uint32_t nam;                /* Native Audio Mixer (BAR0)      */
static uint32_t nabm;               /* Native Audio Bus Master (BAR1) */

/* SR bits del bus master (NABM + 0x16) */
#define AC97_SR_DCH       0x0001u   /* DMA controller halted          */
#define AC97_SR_CELV      0x0002u   /* current equals last valid      */
#define AC97_SR_LVBCI     0x0004u   /* last valid buffer completed    */
#define AC97_SR_BCIS      0x0008u   /* buffer completion interrupt    */
#define AC97_SR_FIFOE     0x0010u   /* fifo error                     */

static uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t fn,
                           uint8_t reg)
{
    outl(PCI_CONFIG_ADDR, 0x80000000u | ((uint32_t)bus << 16)
                          | ((uint32_t)dev << 11) | ((uint32_t)fn << 8)
                          | (reg & 0xFC));
    return inl(PCI_CONFIG_DATA);
}

static void pci_write32(uint8_t bus, uint8_t dev, uint8_t fn,
                        uint8_t reg, uint32_t val)
{
    outl(PCI_CONFIG_ADDR, 0x80000000u | ((uint32_t)bus << 16)
                          | ((uint32_t)dev << 11) | ((uint32_t)fn << 8)
                          | (reg & 0xFC));
    outl(PCI_CONFIG_DATA, val);
}

/* Busca un dispositivo por clase/subclase y devuelve sus BARs. */
static int pci_find_audio(uint32_t *bar0, uint32_t *bar1)
{
    uint8_t bus, dev, fn;
    for (bus = 0; bus < 8; bus++)
        for (dev = 0; dev < 32; dev++) {
            uint32_t id = pci_read32(bus, dev, 0, 0);
            if ((id & 0xFFFF) == 0xFFFF)
                continue;
            for (fn = 0; fn < 8; fn++) {
                uint32_t cl = pci_read32(bus, dev, fn, 0x08);
                if ((cl >> 16) == 0x0401u) {   /* audio controller */
                    *bar0 = pci_read32(bus, dev, fn, 0x10)
                            & 0xFFFFFFF0u;
                    *bar1 = pci_read32(bus, dev, fn, 0x14)
                            & 0xFFFFFFF0u;
                    /* Fase 24-P4: bit BUS MASTER del PCI command
                     * register: sin el, el DMA del QEMU devuelve
                     * ceros silenciosamente. */
                    pci_write32(bus, dev, fn, 0x04,
                                pci_read32(bus, dev, fn, 0x04) | 0x0004u);
                    return 1;
                }
            }
        }
    return 0;
}

/* Buffer de DMA en el HEAP del kernel (kmalloc): el DMA de QEMU
 * devuelve ceros al leer los arrays estaticos de la BSS (0x3xxxx);
 * los bloques del heap (0x2000000+) se leen bien. */
#define AC97_FRAMES 7200
static volatile uint16_t *ac97_buf;
static volatile uint32_t *ac97_bd;      /* descriptor: ptr, cmd|len */

int ac97_init(void)
{
    uint32_t bar0, bar1, i, stat;

    ac97_buf = (volatile uint16_t *)kmalloc(AC97_FRAMES * 2 * 2);
    ac97_bd = (volatile uint32_t *)kmalloc(8);
    if (ac97_buf == 0 || ac97_bd == 0)
        return -1;

    if (pci_find_audio(&bar0, &bar1) == 0) {
        kprint("ac97: sin audio por PCI\n");
        return -1;
    }
    nam = bar0;
    nabm = bar1;

    /* Reset del codec: el registro RESET es el offset 0x00 del NAM
     * (escribir cualquier valor). El codec (Sigmatel 9700 de QEMU)
     * queda listo con el vendor id en 0x7C/0x7E. */
    outl(nam + 0x00, 0x00000001u);
    stat = inl(nam + 0x7C);
    if (stat == 0) {
        kprint("ac97: codec sin respuesta\n");
        return -1;
    }
    kprint("ac97: codec ");
    kprint_uint(stat);
    kprint(" nam=");
    kprint_uint(nam);
    kprint(" nabm=");
    kprint_uint(nabm);
    kprint("\n");

    /* Tasa de muestreo PCM-out a 48000 Hz y volumen al maximo sin
     * mute (bit 15). Master = 0x02, PCM = 0x18, rate = 0x2C. */
    outl(nam + 0x2C, 48000u);
    outl(nam + 0x02, 0x00000000u);
    outl(nam + 0x18, 0x00000000u);

    /* Reset del bus master + buffer descriptor + arranque. Los
     * registros del NABM son de 8/16 bits (SR/CIV/PICB = word;
     * CR/LVI = byte). */
    outb(nabm + 0x1B, 0x02);             /* CR: reset */
    for (i = 0; i < 10000u; i++)
        if (inw(nabm + 0x16) & AC97_SR_DCH)
            break;
    outl(nabm + 0x10, (uint32_t)(uintptr_t)ac97_bd);   /* BDBAR */
    outb(nabm + 0x15, 0);                /* LVI = 0 (un solo BD) */
    outb(nabm + 0x1B, 0x01);             /* CR: run */
    return 0;
}

/* Rellena el buffer con una onda cuadrada a 440 Hz y lo reproduce. */
void ac97_beep(uint32_t ms)
{
    uint32_t frames, i, v = 0, half;
    uint32_t sr0;

    if (nam == 0)
        return;
    frames = 48000u * ms / 1000u;
    if (frames > AC97_FRAMES)
        frames = AC97_FRAMES;
    half = 48000u / (2 * 440u);         /* media onda a 440 Hz */
    for (i = 0; i < frames; i++) {
        int16_t s = (int16_t)((i % (half * 2)) < half ? 8000 : -8000);
        ac97_buf[i * 2] = (uint16_t)s;
        ac97_buf[i * 2 + 1] = (uint16_t)s;
    }
    /* length del ICH = halfwords - 1; cmd = IOC (bit 0) */
    ac97_bd[0] = (uint32_t)(uintptr_t)ac97_buf;
    /* formato ICH/QEMU: len = halfwords (sin -1); cmd bit 0 = IOC */
    ac97_bd[1] = ((uint32_t)(frames * 2)) | 0x00000001u;

    sr0 = inw(nabm + 0x16);
    kprint("ac97: beep ");
    kprint_uint(ms);
    kprint(" ms sr0=");
    kprint_uint(sr0);
    kprint("\n");

    outb(nabm + 0x1B, 0x02);             /* reset del bus master */
    for (i = 0; i < 10000u; i++)
        if (inw(nabm + 0x16) & AC97_SR_DCH)
            break;
    outl(nabm + 0x10, (uint32_t)(uintptr_t)ac97_bd);
    outb(nabm + 0x15, 0);
    outb(nabm + 0x1B, 0x01);             /* run */

    /* esperar la reproduccion: poll de hasta ~1 s del SR (LVBCI =
     * ultimo buffer completado; tambien vale BCIS con el IOC). */
    for (i = 0; i < 20000000u; i++) {
        v = inw(nabm + 0x16);
        if (v & (AC97_SR_LVBCI | AC97_SR_BCIS))
            break;
    }
    kprint("ac97: beep fin sr=");
    kprint_uint(v);
    kprint(" picb=");
    kprint_uint(inw(nabm + 0x18));
    kprint("\n");
}