/* MyOS - user/fork.c
 * Demo de fork (SYS_FORK): el proceso se clona; el hijo reanuda en el
 * mismo punto con retorno 0 y el padre con el pid del hijo. Ambos
 * tienen su propio espacio de direcciones (copiado por el kernel). */

#include <stdint.h>

#define SYS_PRINT  1
#define SYS_EXIT   2
#define SYS_FORK   3
#define SYS_GETPID 5

static void sys_print(const char *s)
{
    __asm__ volatile("int $0x80" : : "a"(SYS_PRINT), "b"(s) : "memory");
}

static void sys_exit(void)
{
    __asm__ volatile("int $0x80" : : "a"(SYS_EXIT));
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

static void delay(void)
{
    for (volatile uint32_t i = 0; i < 500000; i++)
        ;
}

/* Imprime "tag pid n" en un buffer local. */
static void report(const char *tag, int n)
{
    char buf[32];
    int pos = 0;
    int v;

    for (v = 0; tag[v]; v++)
        buf[pos++] = tag[v];
    buf[pos++] = ' ';
    v = sys_getpid();
    {
        char t[8];
        int p = 0;
        do {
            t[p++] = (char)('0' + v % 10);
            v /= 10;
        } while (v > 0);
        while (p > 0)
            buf[pos++] = t[--p];
    }
    buf[pos++] = ' ';
    v = n;
    {
        char t[8];
        int p = 0;
        do {
            t[p++] = (char)('0' + v % 10);
            v /= 10;
        } while (v > 0);
        while (p > 0)
            buf[pos++] = t[--p];
    }
    buf[pos++] = '\n';
    buf[pos] = 0;
    sys_print(buf);
}

void _start(void)
{
    int pid = sys_fork();

    if (pid < 0) {
        sys_print("FORK: error al clonar\n");
        sys_exit();
    }
    if (pid == 0) {
        for (int i = 0; i < 5; i++) {
            report("CHILD", i);
            delay();
        }
        sys_print("FORK: hijo termina\n");
        sys_exit();
    }
    for (int i = 0; i < 5; i++) {
        report("PARENT", i);
        delay();
    }
    sys_print("FORK: padre termina\n");
    sys_exit();
}
