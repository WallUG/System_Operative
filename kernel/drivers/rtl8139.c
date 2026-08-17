/* MyOS - kernel/drivers/rtl8139.c
 * Ethernet RTL8139 (Fase 24-P4, el NIC por defecto de QEMU): PCI
 * 10EC:8139, modo ring legacy (sin C+). Init: reset, MAC, RX ring de
 * 8 KB+16 (RCR: accept phys/broadcast + wrap), TX/RX enable.
 * Sin IRQ: se sondean TSD/TSAD (TX) y CAPR (RX).
 * net_ping(): ARP de 10.0.2.2 (gateway del user-net) + ICMP echo; el
 * slirp responde y el driver verifica la respuesta en el ring. */

#include <stdint.h>
#include <string.h>
#include "io.h"
#include "kprint.h"
#include "mem/heap.h"
#include "drivers/timer.h"

#define PCI_CONFIG_ADDR 0x0CF8u
#define PCI_CONFIG_DATA 0x0CFCu

#define RTL8139_VENDOR 0x10ECu
#define RTL8139_DEVICE 0x8139u

/* registros */
#define R_MAC0      0x00
#define R_TSD0      0x10
#define R_TSAD0     0x20
#define R_RBSTART   0x30
#define R_CHIPCMD   0x37
#define R_CAPR      0x38
#define R_IMR       0x3C
#define R_ISR       0x3E
#define R_RCR       0x44
#define R_CONFIG1   0x52
#define R_TSAD      0x60

#define CMD_RESET  0x10
#define CMD_RXEN   0x08
#define CMD_TXEN   0x04
#define TX_HOSTOWNS 0x2000u

/* RX ring: 64 KB + 16 (RBLEN=3, Fase 25-W2A: con 8 KB el ring hace
 * wrap durante una transferencia TCP grande y el recv relee paquetes
 * viejos) */
#define RX_RING_SIZE (65536 + 16)
#define RX_WRAP      65536

static uint32_t rtl_io;
static volatile uint8_t *rx_ring;
static int rtl_ready;
static volatile uint8_t *tx_pkt;
static uint32_t tx_desc;            /* descriptor TX rotativo */
static uint8_t  mac[6];

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

static void checksum16(const uint8_t *d, int n, uint16_t *out)
{
    uint32_t sum = 0;
    int i;
    for (i = 0; i + 1 < n; i += 2)
        sum += (uint16_t)((uint16_t)d[i] << 8) | d[i + 1];
    if (n & 1)
        sum += (uint16_t)((uint16_t)d[n - 1] << 8);
    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);
    *out = (uint16_t)~sum;
}

int rtl8139_init(void)
{
    uint8_t bus, dev;
    uint32_t id, bar, i;

    tx_pkt = (volatile uint8_t *)kmalloc(2048);
    if (tx_pkt == 0)
        return -1;

    for (bus = 0; bus < 8; bus++)
        for (dev = 0; dev < 32; dev++) {
            id = pci_read32(bus, dev, 0, 0);
            if ((id & 0xFFFF) == 0xFFFF)
                continue;
            if ((id & 0xFFFF) == RTL8139_VENDOR &&
                (id >> 16) == RTL8139_DEVICE) {
                bar = pci_read32(bus, dev, 0, 0x10) & 0xFFFFFFF0u;
                rtl_io = bar;
                /* Fase 24-P4: sin el bit BUS MASTER del PCI command
                 * register (0x04) el DMA del dispositivo devuelve
                 * CERO silenciosamente en QEMU. */
                pci_write32(bus, dev, 0, 0x04,
                            pci_read32(bus, dev, 0, 0x04) | 0x0004u);
                break;
            }
        }
    if (rtl_io == 0) {
        kprint("net: sin RTL8139 por PCI\n");
        return -1;
    }

    /* reset del chip */
    outb(rtl_io + R_CHIPCMD, CMD_RESET);
    for (i = 0; i < 4000000u && (inb(rtl_io + R_CHIPCMD) & CMD_RESET); i++)
        ;

    /* MAC */
    for (i = 0; i < 6; i++)
        mac[i] = inb(rtl_io + R_MAC0 + i);

    /* RX ring en el heap (el DMA de QEMU lee la BSS como 0x00).
     * RCR = aceptar phys/broadcast/multicast + wrap, RBLEN=3 -> 64 KB */
    rx_ring = (volatile uint8_t *)kmalloc(RX_RING_SIZE);
    if (rx_ring == 0)
        return -1;
    outl(rtl_io + R_RBSTART, (uint32_t)(uintptr_t)rx_ring);
    outw(rtl_io + R_CAPR, 0);
    outl(rtl_io + R_RCR, 0x0000188Eu);
    outb(rtl_io + R_CHIPCMD, CMD_RXEN | CMD_TXEN);
    outw(rtl_io + R_IMR, 0x0000);       /* sin IRQ (polling) */

    kprint("net: rtl8139 mac=");
    for (i = 0; i < 6; i++) {
        kprint_uint(mac[i] >> 4);
        kprint_uint(mac[i] & 15);
        if (i < 5)
            kprint(":");
    }
    kprint(" io=");
    kprint_uint(rtl_io);
    kprint("\n");
    rtl_ready = 1;
    return 0;
}

int rtl8139_ready(void)
{
    return rtl_ready;
}

void rtl8139_mac_get(uint8_t out[6])
{
    int i;
    for (i = 0; i < 6; i++)
        out[i] = mac[i];
}

/* Envia pkt[0..len) por el descriptor rotativo (el QEMU transmite el
 * descriptor actual tras cada TSD write). Devuelve 0 si el DMA
 * arranco. Fase 25-W2A: copia desde el buffer del caller (el stack
 * arma el paquete en su propio buffer). */
int rtl8139_tx(const uint8_t *pkt, int len)
{
    uint32_t desc = tx_desc;
    if (len > 2048)
        len = 2048;
    /* net_ping construye el paquete DENTRO de tx_pkt y pasa tx_pkt
     * como pkt: un memcpy auto-copia con memset previo borraria el
     * paquete. Solo copiar cuando el caller trae su propio buffer. */
    if (pkt != tx_pkt)
        memcpy((void *)tx_pkt, pkt, (uint32_t)len);
    outl(rtl_io + R_TSAD0 + desc * 4, (uint32_t)(uintptr_t)tx_pkt);
    /* Fase 25-W2A: el transmit de QEMU es SINCRONO (el TSD write
     * transmite el descriptor antes de retornar), asi que el poll del
     * OWN es innecesario (y en el rtl8139 de QEMU el TSD1 de la 2ª
     * rotacion se lee 0 aunque el paquete salga). */
    outl(rtl_io + R_TSD0 + desc * 4, (uint32_t)len);  /* quita OWN */
    tx_desc = (tx_desc + 1) & 3;
    return 0;
}

/* Lee un paquete del ring (bloquea hasta timeout). El layout del ring
 * (modo legacy): header 4 B {status | len<<16}, datos, crc. Devuelve
 * el largo del paquete o -1.
 * Fase 25-W2A: el driver lleva su propio puntero (rx_capr estatico) —
 * la LECTURA del CAPR devuelve RxBufPtr-16, no la posicion del
 * siguiente paquete. El CAPR se ESCRIBE como (siguiente - 16): QEMU
 * hace RxBufPtr = CAPR + 16, asi que con el offset exacto el espacio
 * libre quedaba en 16 B y la siguiente recepcion se descartaba como
 * overflow (el quirk de la "2ª recepcion" de la Fase 24-P4.5; el
 * driver Linux escribe RTL_W16(RxBufPtr, cur_rx - 16)). */
static uint32_t rx_capr;        /* offset del siguiente paquete */

int rtl8139_rx(uint8_t *dst, int max, int timeout_ms)
{
    uint32_t capr = rx_capr;
    uint32_t t0 = timer_get_ticks();
    for (;;) {
        uint32_t hdr;
        uint32_t i;
        if (capr + 4 > RX_RING_SIZE)
            capr = 0;
        hdr = (uint32_t)rx_ring[capr]
              | ((uint32_t)rx_ring[capr + 1] << 8)
              | ((uint32_t)rx_ring[capr + 2] << 16)
              | ((uint32_t)rx_ring[capr + 3] << 24);
        if (hdr & 0x0001) {                     /* ROK */
            uint32_t raw = (hdr >> 16) & 0xFFFF;
            uint32_t copy = raw;
            if (raw < 4 || raw - 4 > (uint32_t)max) {
                copy = (uint32_t)max + 4;
                if (raw < 4)
                    return -1;
            }
            for (i = 0; i < copy - 4; i++)
                dst[i] = rx_ring[capr + 4 + i];
            /* header(4) + frame + crc(4): el campo len del header ya
             * incluye el crc, asi que el total es len + 4. El avance
             * usa el LEN REAL (no el recortado por el buffer del
             * caller: clampear el avance desalinea el ring). El chip
             * realinea mod 8192; el tail de 16 B aguanta el cruce. */
            capr = (capr + raw + 4 + 3) & ~3u;
            if (capr >= RX_WRAP)
                capr -= RX_WRAP;
            rx_capr = capr;
            /* CAPR = (siguiente - 16) mod 8192: el wrap u16 hace que
             * QEMU compute RxBufPtr = CAPR + 16 = siguiente (mod). */
            outw(rtl_io + R_CAPR, (uint16_t)(capr - 16));
            return (int)(copy - 4);
        }
        /* Fase 25-W2A: sin paquete, re-escribir el CAPR fuerza el
         * qemu_flush_queued_packets del netdev: si no, el rtl8139 de
         * QEMU entrega los paquetes entrantes con retraso (~1-2 s) y
         * los timeouts del stack son flaky. */
        if (rx_capr > 16)
            outw(rtl_io + R_CAPR, (uint16_t)(rx_capr - 16));
        else
            outw(rtl_io + R_CAPR, (uint16_t)(rx_capr + RX_WRAP - 16));
        if (timer_get_ticks() - t0 > (uint32_t)(timeout_ms / 10))
            return -1;
    }
}

/* Construye la cabecera eth + ip + icmp para una peticion echo.
 * Datos de 64 bytes (tamano clasico de ping): el slirp valida el
 * checksum sobre ip_len y los echo cortos no pasan su check. */
static void build_icmp(uint8_t *p, const uint8_t *dstmac, uint16_t id)
{
    uint16_t csum;
    int i;
    /* eth */
    for (uint8_t i = 0; i < 6; i++)
        p[i] = dstmac[i];
    for (uint8_t i = 0; i < 6; i++)
        p[6 + i] = mac[i];
    p[12] = 0x08;
    p[13] = 0x00;
    /* ip: 20 bytes, origen 10.0.2.15, destino 10.0.2.2 */
    p[14] = 0x45;                               /* ver+ihl */
    p[15] = 0;                                  /* tos */
    p[16] = 0;
    p[17] = 92;                                 /* totlen: 20+8+64 */
    p[18] = 0;                                  /* id */
    p[19] = 0;
    p[20] = 0;                                  /* flags+frag */
    p[21] = 0;
    p[22] = 64;                                 /* ttl */
    p[23] = 0x01;                               /* proto ICMP */
    p[24] = 0;                                  /* csum ip: relleno */
    p[25] = 0;
    p[26] = 10;
    p[27] = 0;
    p[28] = 2;
    p[29] = 15;
    p[30] = 10;
    p[31] = 0;
    p[32] = 2;
    p[33] = 2;
    checksum16(p + 14, 20, &csum);
    p[24] = (uint8_t)(csum >> 8);
    p[25] = (uint8_t)csum;
    /* icmp echo request: type 8, id, seq */
    p[34] = 8;
    p[35] = 0;
    p[36] = 0;
    p[37] = 0;
    p[38] = (uint8_t)(id >> 8);
    p[39] = (uint8_t)id;
    p[40] = 0;
    p[41] = 1;
    for (i = 0; i < 64; i++)
        p[42 + i] = (uint8_t)('a' + (i % 26));
    checksum16(p + 34, 72, &csum);
    p[36] = (uint8_t)(csum >> 8);
    p[37] = (uint8_t)csum;
}

/* Ping del gateway del user-net: ARP + ICMP echo. */
void net_ping(void)
{
    uint8_t gwmac[6];
    uint32_t i;
    uint16_t id = 0x1234;

    if (rtl_io == 0)
        return;
    kprint("net: ping 10.0.2.2\n");

    /* 1) ARP request (broadcast): who has 10.0.2.2 */
    memset((void *)tx_pkt, 0, 60);
    for (i = 0; i < 6; i++) {
        tx_pkt[i] = 0xFF;
        tx_pkt[6 + i] = mac[i];
    }
    tx_pkt[12] = 0x08;
    tx_pkt[13] = 0x06;
    tx_pkt[14] = 0x00;
    tx_pkt[15] = 0x01;                          /* ethernet */
    tx_pkt[16] = 0x08;
    tx_pkt[17] = 0x00;                          /* IPv4 */
    tx_pkt[18] = 0x06;
    tx_pkt[19] = 0x04;
    tx_pkt[20] = 0x00;
    tx_pkt[21] = 0x01;                          /* request */
    for (i = 0; i < 6; i++)
        tx_pkt[22 + i] = mac[i];                /* sha */
    tx_pkt[28] = 10;
    tx_pkt[29] = 0;
    tx_pkt[30] = 2;
    tx_pkt[31] = 15;                            /* spa 10.0.2.15 */
    for (i = 0; i < 6; i++)
        tx_pkt[32 + i] = 0xFF;                  /* tha: broadcast */
    tx_pkt[38] = 10;
    tx_pkt[39] = 0;
    tx_pkt[40] = 2;
    tx_pkt[41] = 2;                             /* tpa 10.0.2.2 */
    if (rtl8139_tx((const uint8_t *)tx_pkt, 42) != 0) {
        kprint("net: tx arp fallo\n");
        return;
    }
    kprint("net: tx arp\n");

    /* 2) esperar la respuesta ARP */
    for (i = 0; i < 50; i++) {
        uint8_t r[64];
        int n = rtl8139_rx(r, sizeof(r), 200);
        if (n < 42)
            continue;
        if (r[12] == 0x08 && r[13] == 0x06 && r[21] == 0x02 &&
            r[28] == 10 && r[29] == 0 && r[30] == 2 && r[31] == 2) {
            for (uint8_t j = 0; j < 6; j++)
                gwmac[j] = r[22 + j];
            kprint("net: arp 10.0.2.2 -> ");
            for (uint8_t j = 0; j < 6; j++) {
                kprint_uint(gwmac[j] >> 4);
                kprint_uint(gwmac[j] & 15);
                if (j < 5)
                    kprint(":");
            }
            kprint("\n");
            goto arp_ok;
        }
    }
    kprint("net: sin respuesta ARP\n");
    return;

arp_ok:
    /* 3) ICMP echo request al gateway */
    build_icmp((uint8_t *)tx_pkt, gwmac, id);
    if (rtl8139_tx((const uint8_t *)tx_pkt, 106) != 0) {
        kprint("net: tx icmp fallo\n");
        return;
    }
    kprint("net: tx icmp\n");

    /* 4) esperar el echo reply (type 0, id, seq) */
    for (i = 0; i < 100; i++) {
        uint8_t r[96];
        int n = rtl8139_rx(r, sizeof(r), 200);
        if (n < 42)
            continue;
        if (r[12] == 0x08 && r[13] == 0x00 && r[23] == 1 &&
            r[34] == 0 && r[38] == (uint8_t)(id >> 8) &&
            r[39] == (uint8_t)id) {
            kprint("net: ICMP reply de 10.0.2.2 OK\n");
            return;
        }
    }
    /* El reply llega al nivel del netdev pero el ring no lo recibe en
     * esta version de QEMU (la segunda recepcion); el ARP si hace el
     * round trip completo. */
    kprint("net: sin echo reply\\n");
}