/* MyOS - user/mouseinfo.c
 * Probe de la Fase 14: SYS_MOUSEINFO y SYS_EVENT desde ring 3.
 * Imprime por serial los eventos de raton y teclado que recibe (limitado
 * para no inundar el log). No depende de user32 ni del framebuffer. */

#include <stdint.h>

#define SYS_WRITE   7
#define SYS_MOUSEINFO 16
#define SYS_EVENT   17

static void sys_write(const char *s, unsigned n)
{
    int r;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(SYS_WRITE), "b"(s), "c"(n)
                     : "memory");
    (void)r;
}

static int sys_mouseinfo(uint32_t *mi)
{
    int r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(SYS_MOUSEINFO), "b"(mi) : "memory");
    return r;
}

static int sys_event(uint32_t *ev)
{
    int r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(SYS_EVENT), "b"(ev) : "memory");
    return r;
}

static void put_dec(int v)
{
    char b[12];
    int p = 0;
    if (v < 0) {
        sys_write("-", 1);
        v = -v;
    }
    do {
        b[p++] = (char)('0' + v % 10);
        v /= 10;
    } while (v > 0);
    while (p > 0)
        sys_write(&b[--p], 1);
}

static void dump(uint32_t *ev)
{
    sys_write("ev t=", 5);
    put_dec((int)ev[0]);
    sys_write(" x=", 3);
    put_dec((int)ev[1]);
    sys_write(" y=", 3);
    put_dec((int)ev[2]);
    sys_write(" b=", 3);
    put_dec((int)ev[3]);
    sys_write(" k=", 3);
    put_dec((int)ev[4]);
    sys_write("\n", 1);
}

void _start(void)
{
    uint32_t mi[3];
    uint32_t ev[5];
    int shown = 0;

    if (sys_mouseinfo(mi) == 0) {
        sys_write("mouseinfo: x=", 14);
        put_dec((int)mi[0]);
        sys_write(" y=", 3);
        put_dec((int)mi[1]);
        sys_write(" b=", 3);
        put_dec((int)mi[2]);
        sys_write("\n", 1);
    } else {
        sys_write("mouseinfo: syscall fallo\n", 26);
    }

    sys_write("drenando eventos (primeros 12):\n", 34);
    for (;;) {
        if (sys_event(ev) != 0)
            continue;
        if (shown < 12) {
            dump(ev);
            shown++;
        }
    }
}
