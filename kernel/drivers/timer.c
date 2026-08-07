/* MyOS - kernel/drivers/timer.c
 * PIT (Intel 8253/8254), IRQ0, modo 3 (rate generator).
 * Divisor = 1193182 / hz. Base del futuro scheduler (Fase 5). */

#include <stdint.h>
#include "timer.h"
#include "io.h"

#define PIT_CMD   0x43
#define PIT_CH0   0x40
#define PIT_FREQ  1193182u

static volatile uint32_t tick_count = 0;

void timer_init(uint32_t hz)
{
    uint32_t divisor = PIT_FREQ / hz;   /* division inline i386 */

    outb(PIT_CMD, 0x36);                /* canal 0, lobyte/hibyte, modo 3 */
    outb(PIT_CH0, (uint8_t)(divisor & 0xFF));
    outb(PIT_CH0, (uint8_t)((divisor >> 8) & 0xFF));
}

void timer_tick(void)
{
    tick_count++;
}

uint32_t timer_get_ticks(void)
{
    return tick_count;
}
