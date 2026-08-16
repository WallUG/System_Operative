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

int ac97_play_kernel(const uint8_t *data, uint32_t bytes, uint32_t rate);

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
#define AC97_FRAMES 48000       /* 1 s a 48 kHz stereo 16-bit (192 KB) */
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

    /* Reset del codec: el registro RESET es el offset 0x00 del NAM. El
     * NAM de QEMU/ICH es de 16 bits: los accesos de 32 bits NO hacen
     * nada (y las lecturas devuelven 0xFFFFFFFF por diseno), asi que
     * TODO es con inw/outw. El codec (Sigmatel 9700) deja el vendor
     * id en 0x7C/0x7E y la tasa 48000 con VRA activo. */
    outw(nam + 0x00, 0x0001u);
    stat = inw(nam + 0x7C);
    if (stat == 0 || stat == 0xFFFFFFFFu) {
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

    /* Tasa PCM-out a 48000 Hz y volumen al maximo sin mute: el reset
     * deja el master en 0x8000 (mute); hay que escribir los volumenes
     * con WORD (0x02 master, 0x18 PCM). La tasa (0x2C) solo se acepta
     * con VRA (ya activo tras el reset: 0x28 = 0x0009). */
    outw(nam + 0x2C, 48000u);
    outw(nam + 0x02, 0x0000u);
    outw(nam + 0x18, 0x0000u);

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
    uint32_t frames, i, half;

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
    kprint("ac97: beep ");
    kprint_uint(ms);
    kprint(" ms\n");
    ac97_play_kernel((const uint8_t *)ac97_buf, frames * 4, 48000u);
}

/* Fase 25-W2A: reproduce PCM (16-bit stereo, tasa `rate`) que ya vive
 * en RAM del kernel. Corta en trozos de AC97_FRAMES y espera a que
 * cada uno termine (poll del SR, sin IRQ). Devuelve 0 si OK. */
int ac97_play_kernel(const uint8_t *data, uint32_t bytes, uint32_t rate)
{
    uint32_t off = 0, i;

    if (nam == 0 || data == 0)
        return -1;
    if (bytes == 0)
        return 0;
    /* Tasa del codec: la cambia por reproduccion (QEMU acepta
     * 8000-48000; escritura WORD, VRA ya activo). */
    outw(nam + 0x2C, (uint16_t)rate);

    while (off < bytes) {
        uint32_t n = bytes - off;
        uint32_t frames, sr0, v = 0;
        if (n > AC97_FRAMES * 4)
            n = AC97_FRAMES * 4;
        for (i = 0; i < n; i++)
            ac97_buf[i] = (uint16_t)((uint8_t)data[off + i] |
                                     ((uint8_t)data[off + i + 1] << 8));
        off += n;
        frames = n / 4;                 /* frames stereo 16-bit */
        if (frames == 0)
            break;
        /* length del ICH = halfwords; cmd = IOC (bit 31, no el 0: el bit 0
         * forma parte del length) */
        ac97_bd[0] = (uint32_t)(uintptr_t)ac97_buf;
        ac97_bd[1] = ((uint32_t)(frames * 2)) | 0x80000000u;

        sr0 = inw(nabm + 0x16);
        (void)sr0;
        outb(nabm + 0x1B, 0x02);             /* reset del bus master */
        for (i = 0; i < 10000u; i++)
            if (inw(nabm + 0x16) & AC97_SR_DCH)
                break;
        outl(nabm + 0x10, (uint32_t)(uintptr_t)ac97_bd);
        outb(nabm + 0x15, 0);
        outb(nabm + 0x1B, 0x01);             /* run */

        /* esperar la reproduccion: poll del SR (LVBCI = ultimo buffer
         * completado; tambien vale BCIS con el IOC). */
        for (i = 0; i < 20000000u; i++) {
            v = inw(nabm + 0x16);
            if (v & (AC97_SR_LVBCI | AC97_SR_BCIS))
                break;
        }
        if (!(v & (AC97_SR_LVBCI | AC97_SR_BCIS)))
            return -2;
    }
    return 0;
}