/* MyOS - kernel/drivers/timer.h */
#ifndef MYOS_TIMER_H
#define MYOS_TIMER_H

#include <stdint.h>

/* Configura el PIT (IRQ0) a hz ticks por segundo (max ~1193182). */
void timer_init(uint32_t hz);
/* Llamado desde el IRQ0 handler; incrementa el contador de ticks. */
void timer_tick(void);
/* Contador global de ticks desde timer_init. */
uint32_t timer_get_ticks(void);

#endif
