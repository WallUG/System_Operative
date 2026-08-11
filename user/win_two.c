/* MyOS - user/win_two.c
 * Test de multitarea grafica (Fase 17): hace fork al inicio; padre e
 * hijo crean cada uno su par de ventanas (con su propio PD), el hijo
 * mueve las suyas a posiciones distintas. Verifica que el WM del
 * kernel enruta los eventos (clic en X, arrastre, teclado) solo a la
 * app duena de cada ventana: cada instancia imprime su pid. */

#include <stdint.h>

#define SYS_WRITE   7
#define SYS_MALLOC  10
#define SYS_EVENT   17
#define SYS_WINCREATE 18
#define SYS_WINCLOSE 19
#define SYS_WINMOVE 20
#define SYS_WINUPDATE 21
#define SYS_WININFO 22
#define SYS_FORK    3
#define SYS_GETPID  5

#define EV_MOVE         1
#define EV_BUTTON_DOWN  2
#define EV_BUTTON_UP    3
#define EV_KEY          4
#define EV_WINCLOSE     5

#define FRAME  2
#define TITLE  20

static void sys_write(const char *s, unsigned n)
{
    int r;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(SYS_WRITE), "b"(s), "c"(n)
                     : "memory");
    (void)r;
}

static void *sys_malloc(unsigned size)
{
    void *p;
    __asm__ volatile("int $0x80" : "=a"(p) : "a"(SYS_MALLOC), "b"(size));
    return p;
}

static int sys_event(uint32_t *ev)
{
    int r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(SYS_EVENT), "b"(ev) : "memory");
    return r;
}

static int sys_wincreate(uint32_t *a)
{
    int r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(SYS_WINCREATE), "b"(a) : "memory");
    return r;
}

static int sys_winclose(int id)
{
    int r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(SYS_WINCLOSE), "b"(id) : "memory");
    return r;
}

static int sys_winmove(int id, int dx, int dy)
{
    int r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(SYS_WINMOVE), "b"(id), "c"(dx), "d"(dy)
                     : "memory");
    return r;
}

static int sys_fork(void)
{
    int r;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(SYS_FORK));
    return r;
}

static int sys_getpid(void)
{
    int r;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(SYS_GETPID));
    return r;
}

static void put_dec(uint32_t n)
{
    char b[12];
    int k = 0;
    if (n == 0) {
        sys_write("0", 1);
        return;
    }
    while (n > 0) {
        b[k++] = (char)('0' + n % 10);
        n /= 10;
    }
    while (k > 0)
        sys_write(&b[--k], 1);
}

static void paint(uint32_t *buf, int w, int h, uint32_t c, uint32_t strip)
{
    int x, y;
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            int yy = (y / 16) % 2;
            buf[y * w + x] = (yy == 0) ? c : (c + strip);
        }
}

int _start(void)
{
    uint32_t *bufa, *bufb;
    uint32_t a[8], ev[8];
    int ida, idb, child, closed = 0;
    const char *tag;

    child = (sys_fork() == 0);
    tag = child ? "two" : "demo";

    bufa = sys_malloc((320 - 2 * FRAME) * (200 - TITLE - FRAME) * 4);
    if (bufa == (void *)0)
        return 1;
    paint(bufa, 320 - 2 * FRAME, 200 - TITLE - FRAME, 0x000060B0, 0x00FFFFFF);
    a[0] = (uint32_t)"Ventana A"; a[1] = 250; a[2] = 150;
    a[3] = 320; a[4] = 200; a[5] = (uint32_t)bufa;
    a[6] = (320 - 2 * FRAME) * (200 - TITLE - FRAME) * 4;
    a[7] = 0;
    ida = sys_wincreate(a);
    if (ida < 0)
        return 1;

    bufb = sys_malloc((280 - 2 * FRAME) * (180 - TITLE - FRAME) * 4);
    if (bufb == (void *)0)
        return 1;
    paint(bufb, 280 - 2 * FRAME, 180 - TITLE - FRAME, 0x00B04030, 0x00FFFF00);
    a[0] = (uint32_t)"Ventana B"; a[1] = 320; a[2] = 220;
    a[3] = 280; a[4] = 180; a[5] = (uint32_t)bufb;
    a[6] = (280 - 2 * FRAME) * (180 - TITLE - FRAME) * 4;
    a[7] = 0;
    idb = sys_wincreate(a);
    if (idb < 0)
        return 1;

    /* El hijo aleja sus ventanas de las del padre (rutas independientes
     * para que los logs digan quien cierra cada una). */
    if (child) {
        sys_winmove(ida, -190, -90);    /* A del hijo -> (60,60)    */
        sys_winmove(idb, -260, 80);     /* B del hijo -> (60,300)   */
    }

    sys_write(tag, 3);
    sys_write("[pid=", 5);
    put_dec(sys_getpid());
    sys_write("]: ventanas A=", 15);
    put_dec((uint32_t)ida);
    sys_write(" B=", 3);
    put_dec((uint32_t)idb);
    sys_write("\n", 1);

    for (;;) {
        if (sys_event(ev) != 0)
            continue;
        if (ev[0] == EV_KEY) {
            if (ev[4] == 'q' || ev[4] == '\n')
                break;
            continue;
        }
        if (ev[0] == EV_WINCLOSE) {
            sys_write(tag, 3);
            sys_write("[pid=", 5);
            put_dec(sys_getpid());
            sys_write("]: cerrando ventana id=", 23);
            put_dec((uint32_t)ev[4]);
            sys_write("\n", 1);
            sys_winclose((int)ev[4]);
            closed++;
            if (closed >= 2)
                break;
            continue;
        }
    }
    sys_write("fin\n", 4);
    return 0;
}
