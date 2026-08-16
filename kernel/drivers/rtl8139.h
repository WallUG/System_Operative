/* MyOS - kernel/drivers/rtl8139.h */
#ifndef RTL8139_H
#define RTL8139_H

int rtl8139_init(void);
int rtl8139_ready(void);
void net_ping(void);

#endif