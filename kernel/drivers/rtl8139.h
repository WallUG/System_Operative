/* MyOS - kernel/drivers/rtl8139.h */
#ifndef RTL8139_H
#define RTL8139_H

int rtl8139_init(void);
int rtl8139_ready(void);
void net_ping(void);
void rtl8139_mac_get(uint8_t out[6]);
/* Fase 25-W2A: primitivas del stack (kernel/drivers/net.c) */
int rtl8139_tx(const uint8_t *pkt, int len);
int rtl8139_rx(uint8_t *dst, int max, int timeout_ms);

#endif