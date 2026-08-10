/* MyOS - user/console.c
 * Demo de consola de usuario (Fase 8, hito consola):
 *  - SYS_WRITE: imprimir con longitud explicita.
 *  - SYS_READ:  leer una linea del teclado/serial.
 *  - SYS_FSIZE / SYS_FREAD: consultar y leer un archivo del FS.
 *  - SYS_MALLOC / SYS_FREE: heap de usuario (bump allocator).
 * Los numeros siguen kernel/syscall.h. */

#include <stdint.h>

#define SYS_PRINT   1
#define SYS_EXIT    2
#define SYS_FORK    3
#define SYS_EXEC    4
#define SYS_GETPID  5
#define SYS_READ    6
#define SYS_WRITE   7
#define SYS_FSIZE   8
#define SYS_FREAD   9
#define SYS_MALLOC  10
#define SYS_FREE    11

static void sys_exit(void)
{
    int r;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(SYS_EXIT));
    (void)r;
}

static void sys_write(const char *s, unsigned n)
{
    int r;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(SYS_WRITE), "b"(s), "c"(n)
                     : "memory");
    (void)r;
}

static int sys_read(char *buf, unsigned max)
{
    int r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(SYS_READ), "b"(buf), "c"(max) : "memory");
    return r;
}

static int sys_fsize(const char *name)
{
    int r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(SYS_FSIZE), "b"(name) : "memory");
    return r;
}

static int sys_fread(const char *name, char *dst, unsigned n)
{
    int r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(SYS_FREAD), "b"(name), "c"(dst), "d"(n)
                     : "memory");
    return r;
}

static void *sys_malloc(unsigned size)
{
    void *p;
    __asm__ volatile("int $0x80" : "=a"(p)
                     : "a"(SYS_MALLOC), "b"(size));
    return p;
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

static void put_hex32(uint32_t v)
{
    static const char *dig = "0123456789ABCDEF";
    char b[9];
    int i;
    for (i = 7; i >= 0; i--) {
        b[7 - i] = dig[(v >> (i * 4)) & 0xF];
    }
    b[8] = 0;
    sys_write(b, 8);
}

void _start(void)
{
    char buf[64];
    char *heap;
    int n;

    sys_write("console: SYS_WRITE ok\n", 22);

    /* Heap de usuario: escribir y releer directamente en ring 3. */
    heap = sys_malloc(32);
    if (heap == (void *)0) {
        sys_write("console: malloc fallo\n", 22);
        sys_exit();
    }
    heap[0] = 'H'; heap[1] = 'e'; heap[2] = 'a'; heap[3] = 'p';
    sys_write("heap: 32 bytes en 0x", 21);
    put_hex32((uint32_t)heap);
    sys_write("\n", 1);

    /* Tamano de hello.elf consultado por el FS desde ring 3. */
    n = sys_fsize("hello.elf");
    if (n < 0)
        n = 0;
    sys_write("hello.elf = ", 13);
    put_dec((uint32_t)n);
    sys_write(" bytes\n", 7);

    /* FREAD: primeras 12 bytes en hex. */
    n = sys_fread("hello.elf", buf, 12);
    if (n == 12) {
        sys_write("magic: ", 7);
        for (int i = 0; i < 4; i++) {
            char hb[3];
            hb[0] = "0123456789ABCDEF"[(buf[i] >> 4) & 0xF];
            hb[1] = "0123456789ABCDEF"[buf[i] & 0xF];
            hb[2] = 0;
            sys_write(hb, 2);
            sys_write(" ", 1);
        }
        sys_write("\n", 1);
    } else {
        sys_write("fread fallo\n", 12);
    }

    /* Linea interactiva: prueba SYS_READ (echo del kernel). */
sys_write("Escribe algo: ", 15);
    n = sys_read(buf, sizeof(buf));
    if (n < 0)
        n = 0;
    sys_write("Recibido (", 11);
    put_dec((uint32_t)n);
    sys_write("): ", 3);
    sys_write(buf, (unsigned)n);
    sys_write("\n", 1);

    sys_exit();
}