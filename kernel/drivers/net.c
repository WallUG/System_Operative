/* MyOS - kernel/drivers/net.c
 * Stack IP cliente (Fase 25-W2A paso 6): ARP (cache), UDP y TCP
 * cliente sobre el RTL8139. Sin IRQ: todo sincrono con timeouts
 * (el driver sondea el ring).
 *
 * Una sola conexion TCP a la vez, suficiente para HTTP GET
 * (wget/netget). El estado: SYN -> SYN+ACK -> EST -> datos -> FIN.
 * Los numeros de secuencia usan comparacion con wrap de 32 bits
 * ((int32_t)(a - b) > 0). El checksum de IP/TCP/UDP se calcula
 * siempre (el slirp descarta los mal calculados). */

#include <stdint.h>
#include <string.h>
#include "kprint.h"
#include "mem/heap.h"
#include "drivers/rtl8139.h"
#include "drivers/timer.h"

#define MY_IP 10
#define MY_IP2 0
#define MY_IP3 2
#define MY_IP4 15

static const uint8_t my_ip[4] = { MY_IP, MY_IP2, MY_IP3, MY_IP4 };
static uint8_t my_mac[6];

/* --- checksum (IPv4: suma de words big-endian, complemento a 1) --- */

static uint16_t net_csum(const uint8_t *d, int n)
{
    uint32_t sum = 0;
    int i;
    for (i = 0; i + 1 < n; i += 2)
        sum += (uint16_t)(((uint16_t)d[i] << 8) | d[i + 1]);
    if (n & 1)
        sum += (uint16_t)((uint16_t)d[n - 1] << 8);
    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

static uint16_t csum_with_pseudo(const uint8_t *pseudo, const uint8_t *d,
                                 int n)
{
    uint32_t sum = 0;
    int i;
    for (i = 0; i + 1 < 12; i += 2)
        sum += (uint16_t)(((uint16_t)pseudo[i] << 8) | pseudo[i + 1]);
    for (i = 0; i + 1 < n; i += 2)
        sum += (uint16_t)(((uint16_t)d[i] << 8) | d[i + 1]);
    if (n & 1)
        sum += (uint16_t)((uint16_t)d[n - 1] << 8);
    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

/* --- ARP: cache + resolucion bloqueante --- */

#define ARP_CACHE 4
static uint8_t arp_ip[ARP_CACHE][4];
static uint8_t arp_mac[ARP_CACHE][6];
static int arp_ok[ARP_CACHE];
static uint32_t arp_age[ARP_CACHE];

static int arp_lookup(const uint8_t ip[4], uint8_t mac[6])
{
    int i;
    for (i = 0; i < ARP_CACHE; i++)
        if (arp_ok[i] && memcmp(arp_ip[i], ip, 4) == 0) {
            memcpy(mac, arp_mac[i], 6);
            return 0;
        }
    return -1;
}

/* Espera la respuesta ARP para `ip` (2 s). 0 = ok. */
static int arp_wait(const uint8_t ip[4], uint8_t mac[6])
{
    uint8_t r[96];
    uint32_t t0 = timer_get_ticks();
    while (timer_get_ticks() - t0 < 200) {          /* 2 s */
        int n = rtl8139_rx(r, sizeof(r), 500);
        if (n < 42)
            continue;
        if (r[12] == 0x08 && r[13] == 0x06 && r[21] == 0x02 &&
            r[28] == ip[0] && r[29] == ip[1] &&
            r[30] == ip[2] && r[31] == ip[3]) {
            memcpy(mac, r + 22, 6);
            return 0;
        }
    }
    return -1;
}

/* Resuelve ip -> mac (cache o ARP request broadcast). 0 = ok. */
int net_arp_resolve(const uint8_t ip[4], uint8_t mac[6])
{
    uint8_t p[60];
    int i;

    if (arp_lookup(ip, mac) == 0)
        return 0;
    if (memcmp(ip, my_ip, 4) == 0) {
        rtl8139_mac_get(mac);
        return 0;
    }
    memset(p, 0, sizeof(p));
    for (i = 0; i < 6; i++)
        p[i] = 0xFF;
    for (i = 0; i < 6; i++)
        p[6 + i] = my_mac[i];
    p[12] = 0x08;
    p[13] = 0x06;
    p[14] = 0x00;
    p[15] = 0x01;               /* ethernet */
    p[16] = 0x08;
    p[17] = 0x00;               /* IPv4 */
    p[18] = 0x06;
    p[19] = 0x04;
    p[20] = 0x00;
    p[21] = 0x01;               /* request */
    for (i = 0; i < 6; i++)
        p[22 + i] = my_mac[i];
    p[28] = MY_IP;
    p[29] = MY_IP2;
    p[30] = MY_IP3;
    p[31] = MY_IP4;
    for (i = 0; i < 6; i++)
        p[32 + i] = 0xFF;
    p[38] = ip[0];
    p[39] = ip[1];
    p[40] = ip[2];
    p[41] = ip[3];
    if (rtl8139_tx(p, 42) != 0)
        return -1;
    if (arp_wait(ip, mac) != 0)
        return -1;
    for (i = 0; i < ARP_CACHE; i++)
        if (!arp_ok[i] ||
            (uint32_t)(timer_get_ticks() - arp_age[i]) > 1000) {
            memcpy(arp_ip[i], ip, 4);
            memcpy(arp_mac[i], mac, 6);
            arp_ok[i] = 1;
            arp_age[i] = timer_get_ticks();
            break;
        }
    return 0;
}

/* --- IP: arma el header (proto, payload -> pkt eth+ip+payload) --- */

static int ip_build(uint8_t *p, const uint8_t dst[4], uint8_t proto,
                    uint16_t totlen)
{
    uint16_t csum;
    int i;
    for (i = 0; i < 6; i++)
        p[i] = 0xFF;                    /* relleno: mac del arp */
    for (i = 0; i < 6; i++)
        p[6 + i] = my_mac[i];
    p[12] = 0x08;
    p[13] = 0x00;
    p[14] = 0x45;
    p[15] = 0;
    p[16] = (uint8_t)(totlen >> 8);
    p[17] = (uint8_t)totlen;
    p[18] = 0;
    p[19] = 0;
    p[20] = 0x40;                       /* DF */
    p[21] = 0;
    p[22] = 64;
    p[23] = proto;
    p[24] = 0;
    p[25] = 0;
    p[26] = MY_IP;
    p[27] = MY_IP2;
    p[28] = MY_IP3;
    p[29] = MY_IP4;
    p[30] = dst[0];
    p[31] = dst[1];
    p[32] = dst[2];
    p[33] = dst[3];
    csum = net_csum(p + 14, 20);
    p[24] = (uint8_t)(csum >> 8);
    p[25] = (uint8_t)csum;
    return 34;                          /* offset del payload */
}

/* Parsea eth+ip: devuelve proto, src ip, payload. 0 si no es IPv4. */
static int ip_parse(const uint8_t *r, int n, uint8_t *proto,
                    const uint8_t **srcip_out, const uint8_t **payload,
                    int *payload_len)
{
    int ihlen;
    if (n < 34)
        return 0;
    if (r[12] != 0x08 || r[13] != 0x00)
        return 0;
    if ((r[14] >> 4) != 4)
        return 0;
    ihlen = (r[14] & 0x0F) * 4;
    if (n < 14 + ihlen + 8)
        return 0;
    *proto = r[23];
    *srcip_out = r + 26;
    *payload = r + 14 + ihlen;
    *payload_len = n - 14 - ihlen;
    return 1;
}

/* --- UDP --- */

int net_udp_send(const uint8_t dst[4], uint16_t src_port,
                 uint16_t dst_port, const uint8_t *data, int len)
{
    uint8_t *p = (uint8_t *)kmalloc(2048);

    uint8_t mac[6];
    uint8_t pseudo[12];
    uint16_t csum;
    int off, tot;

    if (p == 0)
        return -1;
    if (net_arp_resolve(dst, mac) != 0) {
        kfree(p);
        return -1;
    }
    tot = 20 + 8 + len;                 /* ip + udp + datos */
    off = ip_build(p, dst, 17, (uint16_t)tot);
    p[off] = (uint8_t)(src_port >> 8);
    p[off + 1] = (uint8_t)src_port;
    p[off + 2] = (uint8_t)(dst_port >> 8);
    p[off + 3] = (uint8_t)dst_port;
    p[off + 4] = (uint8_t)((8 + len) >> 8);
    p[off + 5] = (uint8_t)(8 + len);
    p[off + 6] = 0;
    p[off + 7] = 0;
    memcpy(p + off + 8, data, (uint32_t)len);
    memcpy(pseudo, my_ip, 4);
    memcpy(pseudo + 4, dst, 4);
    pseudo[8] = 0;
    pseudo[9] = 17;
    pseudo[10] = (uint8_t)((8 + len) >> 8);
    pseudo[11] = (uint8_t)(8 + len);
    csum = csum_with_pseudo(pseudo, p + off, 8 + len);
    p[off + 6] = (uint8_t)(csum >> 8);
    p[off + 7] = (uint8_t)csum;
    memcpy(p, mac, 6);
    rtl8139_tx(p, 14 + tot);
    kfree(p);
    return 0;
}

/* Recibe un datagrama UDP destinado a src_port. Devuelve el payload
 * en dst (o -1 en timeout). srcip/udp_src opcionales (pueden ser 0). */
int net_udp_recv(uint16_t src_port, uint8_t *dst, int max,
                 int timeout_ms, uint8_t srcip_out[4],
                 uint16_t *udp_src_out)
{
    uint8_t r[1600];
    uint32_t t0 = timer_get_ticks();
    while (timer_get_ticks() - t0 < (uint32_t)(timeout_ms / 10)) {
        int n = rtl8139_rx(r, sizeof(r), 500);
        uint8_t proto;
        const uint8_t *srcip = 0;
        const uint8_t *pay;
        int plen;
        int udplen;
        if (!ip_parse(r, n, &proto, &srcip, &pay, &plen))
            continue;
        if (proto != 17 || plen < 8)
            continue;
        if ((pay[2] != (uint8_t)(src_port >> 8)) ||
            (pay[3] != (uint8_t)src_port))
            continue;
        udplen = ((int)pay[4] << 8) | pay[5];
        if (udplen < 8 || udplen - 8 > max)
            continue;
        memcpy(dst, pay + 8, (uint32_t)(udplen - 8));
        if (srcip_out)
            memcpy(srcip_out, srcip, 4);
        if (udp_src_out)
            *udp_src_out = (uint16_t)(((uint16_t)pay[0] << 8) | pay[1]);
        return udplen - 8;
    }
    return -1;
}

/* --- TCP cliente --- */

/* Banderas del header TCP (Fase 25-W2A: antes se usaban los valores
 * de ESTADO como banderas y el SYN salia con FIN) */
#define TCP_FIN 0x01
#define TCP_SYN 0x02
#define TCP_RST 0x04
#define TCP_PSH 0x08
#define TCP_ACK 0x10

/* Estados de la maquina (una conexion a la vez) */
#define TCP_ST_FREE 0
#define TCP_ST_SYN  1
#define TCP_ST_EST  2
#define TCP_ST_FIN  3

static int tcp_state;               /* TCP_ST_* */
static uint8_t tcp_peer[4];
static uint16_t tcp_peer_port;
static uint16_t tcp_local_port;
static uint32_t tcp_seq;            /* nuestro proximo seq */
static uint32_t tcp_ack;            /* proximo seq esperado del peer */
static uint8_t tcp_last_acked;      /* 1 = nuestro ultimo envio acked */

static uint32_t tcp_syn_isn;        /* ISN del peer */

static int tcp_build(uint8_t *p, const uint8_t dst[4], uint16_t sport,
                     uint16_t dport, uint32_t seq, uint32_t ack,
                     uint8_t flags, const uint8_t *data, int len)
{
    uint8_t pseudo[12];
    uint16_t csum;
    int off = ip_build(p, dst, 6, (uint16_t)(20 + 20 + len));
    p[off] = (uint8_t)(sport >> 8);
    p[off + 1] = (uint8_t)sport;
    p[off + 2] = (uint8_t)(dport >> 8);
    p[off + 3] = (uint8_t)dport;
    p[off + 4] = (uint8_t)(seq >> 24);
    p[off + 5] = (uint8_t)(seq >> 16);
    p[off + 6] = (uint8_t)(seq >> 8);
    p[off + 7] = (uint8_t)seq;
    p[off + 8] = (uint8_t)(ack >> 24);
    p[off + 9] = (uint8_t)(ack >> 16);
    p[off + 10] = (uint8_t)(ack >> 8);
    p[off + 11] = (uint8_t)ack;
    p[off + 12] = 0x50;             /* data offset 5 (sin opciones) */
    p[off + 13] = flags;
    p[off + 14] = 0xFF;             /* window 65535 */
    p[off + 15] = 0xFF;
    p[off + 16] = 0;
    p[off + 17] = 0;
    p[off + 18] = 0;
    p[off + 19] = 0;
    if (len > 0)
        memcpy(p + off + 20, data, (uint32_t)len);
    memcpy(pseudo, my_ip, 4);
    memcpy(pseudo + 4, dst, 4);
    pseudo[8] = 0;
    pseudo[9] = 6;
    pseudo[10] = (uint8_t)((20 + len) >> 8);
    pseudo[11] = (uint8_t)(20 + len);
    csum = csum_with_pseudo(pseudo, p + off, 20 + len);
    p[off + 16] = (uint8_t)(csum >> 8);
    p[off + 17] = (uint8_t)csum;
    return off;
}

/* Envia el segmento ya construido en tx[] (eth+ip+tcp). */
static int tcp_tx_pkt(uint8_t *tx, int off, int len, const uint8_t dst[4])
{
    uint8_t mac[6];
    if (net_arp_resolve(dst, mac) != 0)
        return -1;
    memcpy(tx, mac, 6);
    /* off ya incluye el eth (ip_build -> 34): el total es off + len */
    return rtl8139_tx(tx, off + len);
}

static uint8_t tcp_seg_buf[2048];   /* paquete en construccion */

/* Envia SYN. Devuelve 0 si llego el SYN+ACK (estado EST). */
static int tcp_send_syn(const uint8_t dst[4], uint16_t sport,
                        uint16_t dport, uint32_t isn)
{
    int off, tries;
    uint8_t r[1600];

    tcp_state = TCP_ST_SYN;
    off = tcp_build(tcp_seg_buf, dst, sport, dport, isn, 0, TCP_SYN, 0, 0);
    for (tries = 0; tries < 3; tries++) {
        uint32_t t0 = timer_get_ticks();
        if (tcp_tx_pkt(tcp_seg_buf, off, 20, dst) != 0)
            return -1;
        /* esperar el SYN+ACK (hasta 1 s, reenviando a los 500 ms) */
        while (timer_get_ticks() - t0 < 100) {  /* 1 s */
            int n = rtl8139_rx(r, sizeof(r), 250);
            uint8_t proto;
            const uint8_t *srcip;
            const uint8_t *pay;
            int plen;
            if (!ip_parse(r, n, &proto, &srcip, &pay, &plen))
                continue;
            if (proto != 6 || plen < 20)
                continue;
            /* el SYN+ACK: src = dport (8080), dst = sport (nuestro) */
            if ((pay[0] != (uint8_t)(dport >> 8)) ||
                (pay[1] != (uint8_t)dport) ||
                (pay[2] != (uint8_t)(sport >> 8)) ||
                (pay[3] != (uint8_t)sport))
                continue;
            if (!(pay[13] & (TCP_SYN | TCP_ACK)))
                continue;
            tcp_syn_isn = ((uint32_t)pay[4] << 24) | ((uint32_t)pay[5] << 16)
                          | ((uint32_t)pay[6] << 8) | pay[7];
            tcp_ack = tcp_syn_isn + 1;
            tcp_seq = isn + 1;
            tcp_state = TCP_ST_EST;
            /* ACK del SYN+ACK */
            off = tcp_build(tcp_seg_buf, dst, sport, dport, tcp_seq,
                            tcp_ack, TCP_ACK, 0, 0);
            tcp_tx_pkt(tcp_seg_buf, off, 20, dst);
            return 0;
        }
    }
    tcp_state = 0;
    return -1;
}

/* Envia el payload (ACK piggyback). 0 = el peer lo ack'ed. */
static int tcp_send_data(const uint8_t *data, int len)
{
    int off, tries;
    uint8_t r[1600];

    tcp_last_acked = 0;
    off = tcp_build(tcp_seg_buf, tcp_peer, tcp_local_port, tcp_peer_port,
                    tcp_seq, tcp_ack, TCP_PSH | TCP_ACK, data, len);
    for (tries = 0; tries < 3; tries++) {
        uint32_t t0 = timer_get_ticks();
        if (tcp_tx_pkt(tcp_seg_buf, off, 20 + len, tcp_peer) != 0)
            return -1;
        while (timer_get_ticks() - t0 < 100) {  /* 1 s */
            int n = rtl8139_rx(r, sizeof(r), 250);
            uint8_t proto;
            const uint8_t *srcip;
            const uint8_t *pay;
            int plen;
            if (!ip_parse(r, n, &proto, &srcip, &pay, &plen))
                continue;
            if (proto != 6 || plen < 20)
                continue;
            /* el segmento del peer: src = peer_port, dst = local */
            if ((pay[0] != (uint8_t)(tcp_peer_port >> 8)) ||
                (pay[1] != (uint8_t)tcp_peer_port) ||
                (pay[2] != (uint8_t)(tcp_local_port >> 8)) ||
                (pay[3] != (uint8_t)tcp_local_port))
                continue;
            {
                uint32_t ackn = ((uint32_t)pay[8] << 24)
                                | ((uint32_t)pay[9] << 16)
                                | ((uint32_t)pay[10] << 8) | pay[11];
                /* nuestro envio acked (seq + len). Los datos que
                 * lleguen con el ACK se dejan para net_tcp_recv
                 * (si no los ACK ahora, el peer los retransmite). */
                if ((int32_t)(ackn - (tcp_seq + len)) >= 0) {
                    tcp_seq += len;
                    tcp_last_acked = 1;
                    return 0;
                }
                /* ACK de un segmento anterior (dup ACK): ignorar */
            }
        }
    }
    return -1;
}

/* Recibe datos en orden. Devuelve >0 (bytes), 0 (FIN), -1 (timeout). */
int net_tcp_recv(uint8_t *dst, int max, int timeout_ms)
{
    uint8_t r[1600];
    uint32_t t0 = timer_get_ticks();

    if (tcp_state == TCP_ST_FREE)
        return -1;
    if (tcp_state == TCP_ST_FIN)        /* peer cerro: EOF */
        return 0;
    while (timer_get_ticks() - t0 < (uint32_t)(timeout_ms / 10)) {
        int n = rtl8139_rx(r, sizeof(r), 500);
        uint8_t proto;
        const uint8_t *srcip;
        const uint8_t *pay;
        int plen;
        int off;
        if (!ip_parse(r, n, &proto, &srcip, &pay, &plen))
            continue;
        if (proto != 6 || plen < 20)
            continue;
        /* el segmento del peer: src = peer_port, dst = local */
        if ((pay[0] != (uint8_t)(tcp_peer_port >> 8)) ||
            (pay[1] != (uint8_t)tcp_peer_port) ||
            (pay[2] != (uint8_t)(tcp_local_port >> 8)) ||
            (pay[3] != (uint8_t)tcp_local_port))
            continue;
        off = (pay[12] >> 4) * 4;
        if (off < 20 || off > plen)
            continue;
        {
            uint32_t segseq = ((uint32_t)pay[4] << 24)
                              | ((uint32_t)pay[5] << 16)
                              | ((uint32_t)pay[6] << 8) | pay[7];
            int dlen = plen - off;
            if (pay[13] & TCP_FIN) {
                /* Fase 25-W2A: el segmento con FIN puede llevar la
                 * COLA de datos (slirp cierra con data+FIN en el
                 * ultimo segmento); entregarla ANTES de marcar EOF. */
                if (dlen > 0 && segseq == tcp_ack) {
                    int take = dlen;
                    if (take > max)
                        take = max;
                    memcpy(dst, pay + off, (uint32_t)take);
                    tcp_ack = segseq + take;
                    off = tcp_build(tcp_seg_buf, tcp_peer, tcp_local_port,
                                    tcp_peer_port, tcp_seq, tcp_ack,
                                    TCP_ACK, 0, 0);
                    tcp_tx_pkt(tcp_seg_buf, off, 20, tcp_peer);
                    if (take < dlen)
                        return take;    /* sobra: la lee el proximo */
                    tcp_state = TCP_ST_FIN;
                    return take;
                }
                tcp_ack = segseq + 1 + dlen;
                tcp_state = TCP_ST_FIN;
                off = tcp_build(tcp_seg_buf, tcp_peer, tcp_local_port,
                                tcp_peer_port, tcp_seq, tcp_ack,
                                TCP_ACK, 0, 0);
                tcp_tx_pkt(tcp_seg_buf, off, 20, tcp_peer);
                return 0;
            }
            if (dlen == 0)
                continue;               /* ACK puro */
            if (segseq != tcp_ack) {
                /* fuera de orden / duplicado: re-ACK para re-sincronizar */
                off = tcp_build(tcp_seg_buf, tcp_peer, tcp_local_port,
                                tcp_peer_port, tcp_seq, tcp_ack,
                                TCP_ACK, 0, 0);
                tcp_tx_pkt(tcp_seg_buf, off, 20, tcp_peer);
                continue;
            }
            if (dlen > max)
                dlen = max;
            memcpy(dst, pay + off, (uint32_t)dlen);
            tcp_ack += dlen;
            off = tcp_build(tcp_seg_buf, tcp_peer, tcp_local_port,
                            tcp_peer_port, tcp_seq, tcp_ack, TCP_ACK, 0, 0);
            tcp_tx_pkt(tcp_seg_buf, off, 20, tcp_peer);
            return dlen;
        }
    }
    return -1;
}

/* Conecta (bloquea). 0 = ok. */
int net_tcp_connect(const uint8_t dst[4], uint16_t port)
{
    uint32_t isn;
    if (tcp_state != TCP_ST_FREE)
        return -1;
    memcpy(tcp_peer, dst, 4);
    tcp_peer_port = port;
    tcp_local_port = (uint16_t)(1024 + (timer_get_ticks() & 0x3FFF));
    isn = (uint32_t)timer_get_ticks() * 2654435761u + 0x9E3779B9u;
    tcp_seq = isn;
    return tcp_send_syn(dst, tcp_local_port, port, isn);
}

/* Envia datos. 0 = ok (acked). */
int net_tcp_send(const uint8_t *data, int len)
{
    if (tcp_state != TCP_ST_EST || len > 1400 || len <= 0)
        return -1;
    return tcp_send_data(data, len);
}

/* Cierra (FIN). 0 = ok. */
int net_tcp_close(void)
{
    int off;
    if (tcp_state == TCP_ST_FREE)
        return 0;
    if (tcp_state == TCP_ST_EST) {
        off = tcp_build(tcp_seg_buf, tcp_peer, tcp_local_port,
                        tcp_peer_port, tcp_seq, tcp_ack, TCP_FIN | TCP_ACK,
                        0, 0);
        tcp_tx_pkt(tcp_seg_buf, off, 20, tcp_peer);
        tcp_seq++;
    }
    tcp_state = TCP_ST_FREE;
    return 0;
}

int net_init_stack(void)
{
    rtl8139_mac_get(my_mac);
    return 0;
}