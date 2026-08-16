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
static uint32_t pit_divisor = 0;

void timer_init(uint32_t hz)
{
    uint32_t divisor = PIT_FREQ / hz;   /* division inline i386 */

    pit_divisor = divisor;
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

/* Lee el contador del PIT con latch (0x00 = canal 0): el valor es
 * estable entre la lectura y el siguiente reload. El contador baja de
 * pit_divisor a 0; "pit_divisor - cnt" es el tiempo transcurrido
 * dentro del periodo actual, asi que la serie total es continua:
 * en el borde (cnt=0) el valor es tick*d + d = (tick+1)*d, igual que
 * justo despues del reload (tick+1)*d + 0. */
uint64_t timer_qpc(void)
{
    uint8_t lo, hi;
    uint32_t cnt;

    outb(PIT_CMD, 0x00);
    lo = inb(PIT_CH0);
    hi = inb(PIT_CH0);
    cnt = ((uint32_t)hi << 8) | lo;
    return (uint64_t)tick_count * pit_divisor + (pit_divisor - cnt);
}
