/* MyOS - user/win_demo.c
 * Demo del gestor de ventanas del kernel (Fase 16, opcion B).
 * Crea dos ventanas superpuestas con backing buffer propio, espera
 * eventos: el WM se encarga del arrastre (barra de titulo), del raise
 * y de avisar con EV_WINCLOSE al pulsar el boton X; una tecla cierra
 * todo. El contenido del cliente se pinta en el buffer de usuario y se
 * recompone con SYS_WINUPDATE. */

#include <stdint.h>

#define SYS_WRITE   7
#define SYS_MALLOC  10
#define SYS_EVENT   17
#define SYS_WINCREATE 18
#define SYS_WINCLOSE 19
#define SYS_WINUPDATE 21
#define SYS_WININFO 22

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

static int sys_winupdate(int id)
{
    int r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(SYS_WINUPDATE), "b"(id) : "memory");
    return r;
}

static int sys_wininfo(int id, uint32_t *info)
{
    int r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(SYS_WININFO), "b"(id), "c"(info) : "memory");
    return r;
}

static void put_dec(uint32_t v)
{
    char b[12];
    int p = 0;
    do {
        b[p++] = (char)('0' + v % 10);
        v /= 10;
    } while (v > 0);
    while (p > 0)
        sys_write(&b[--p], 1);
}

/* Pinta el area cliente: fondo y franjas diagonales para distinguir
 * las ventanas en los screendumps. */
static void paint(uint32_t *buf, int cw, int ch, uint32_t base, uint32_t line)
{
    int i, j;
    for (j = 0; j < ch; j++)
        for (i = 0; i < cw; i++) {
            uint32_t c = (((i - j) % 48) + 48) % 48 < 8 ? line : base;
            buf[j * cw + i] = c;
        }
}

int _start(void)
{
    uint32_t ev[5];
    uint32_t a[7], info[8];
    uint32_t *bufa, *bufb;
    int ida, idb;
    int closed = 0;

    sys_write("demo: creando ventanas...\n", 29);

    /* Ventana A (320x200, superpuesta a B) */
    bufa = sys_malloc((320 - 2 * FRAME) * (200 - TITLE - FRAME) * 4);
    if (bufa == (void *)0) {
        sys_write("demo: malloc A fallo\n", 21);
        return 1;
    }
    paint(bufa, 320 - 2 * FRAME, 200 - TITLE - FRAME, 0x000060B0, 0x00FFFFFF);
    a[0] = (uint32_t)"Ventana A"; a[1] = 250; a[2] = 150;
    a[3] = 320; a[4] = 200; a[5] = (uint32_t)bufa;
    a[6] = (320 - 2 * FRAME) * (200 - TITLE - FRAME) * 4;
    ida = sys_wincreate(a);
    if (ida < 0) {
        sys_write("demo: create A fallo\n", 21);
        return 1;
    }

    /* Ventana B (280x180), desplazada para superponerse */
    bufb = sys_malloc((280 - 2 * FRAME) * (180 - TITLE - FRAME) * 4);
    if (bufb == (void *)0) {
        sys_write("demo: malloc B fallo\n", 21);
        return 1;
    }
    paint(bufb, 280 - 2 * FRAME, 180 - TITLE - FRAME, 0x00B04030, 0x00FFFF00);
    a[0] = (uint32_t)"Ventana B"; a[1] = 320; a[2] = 220;
    a[3] = 280; a[4] = 180; a[5] = (uint32_t)bufb;
    a[6] = (280 - 2 * FRAME) * (180 - TITLE - FRAME) * 4;
    idb = sys_wincreate(a);
    if (idb < 0) {
        sys_write("demo: create B fallo\n", 21);
        return 1;
    }

    if (sys_wininfo(ida, info) == 0)
        sys_write("demo: cliente A = ", 18), put_dec(info[6]),
            sys_write("x", 1), put_dec(info[7]), sys_write("\n", 1);
    sys_winupdate(ida);         /* recompone (prueba de la syscall) */

    sys_write("demo: ventanas A=", 18);
    put_dec((uint32_t)ida);
    sys_write(" B=", 3);
    put_dec((uint32_t)idb);
    sys_write(" (arrastra por el titulo, X cierra, tecla sale)\n", 51);

    for (;;) {
        if (sys_event(ev) != 0)
            continue;
        if (ev[0] == EV_KEY) {
            if (ev[4] == 'q' || ev[4] == '\n')
                break;
            continue;
        }
        if (ev[0] == EV_WINCLOSE) {
            sys_write("demo: cerrando ventana id=", 26);
            put_dec((uint32_t)ev[4]);
            sys_write("\n", 1);
            sys_winclose((int)ev[4]);
            closed++;
            if (closed >= 2)
                break;
            continue;
        }
    }

    sys_write("demo: fin\n", 10);
    return 0;
}
