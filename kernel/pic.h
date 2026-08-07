/* MyOS - kernel/pic.h */
#ifndef MYOS_PIC_H
#define MYOS_PIC_H

void pic_remap(void);
void pic_send_eoi(uint8_t irq);

#endif
