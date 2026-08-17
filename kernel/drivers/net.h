/* MyOS - kernel/drivers/net.h */
#ifndef MYOS_NET_H
#define MYOS_NET_H

int net_init_stack(void);
int net_arp_resolve(const uint8_t ip[4], uint8_t mac[6]);
int net_udp_send(const uint8_t dst[4], uint16_t src_port,
                 uint16_t dst_port, const uint8_t *data, int len);
int net_udp_recv(uint16_t src_port, uint8_t *dst, int max,
                 int timeout_ms, uint8_t srcip_out[4],
                 uint16_t *udp_src_out);
int net_tcp_connect(const uint8_t dst[4], uint16_t port);
int net_tcp_send(const uint8_t *data, int len);
int net_tcp_recv(uint8_t *dst, int max, int timeout_ms);
int net_tcp_close(void);

#endif