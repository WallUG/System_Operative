/* MyOS - user/win32/user32.c
 * user32.dll: modulo Win32 fijo (ring 3) en 0xB0100000.
 * MessageBoxA minimo: imprime el texto por la consola usando la
 * syscall SYS_WRITE directamente (sin kernel32). */

#include <stdint.h>

#define SYS_WRITE 7

static int sys_write(const char *s, uint32_t len)
{
    int r;
    __asm__ volatile("int $0x80"
                     : "=a"(r)
                     : "a"(SYS_WRITE), "b"(s), "c"(len)
                     : "memory");
    return r;
}

static uint32_t strlen32(const char *s)
{
    uint32_t n = 0;
    while (s[n])
        n++;
    return n;
}

/* Botones: 0 = OK. Devuelve 0 (IDOK). */
int MessageBoxA(void *h, const char *text, const char *caption, uint32_t type)
{
    char buf[128];
    uint32_t i = 0;

    if (caption) {
        for (i = 0; i < sizeof(buf) - 2 && caption[i]; i++)
            buf[i] = caption[i];
        buf[i++] = ':';
        buf[i++] = ' ';
    }
    if (text) {
        for (; i < sizeof(buf) - 1 && text[i - (uint32_t)(caption ? i - 1
            : 0) - (uint32_t)(caption ? 0 : 0)]; i++)
            buf[i] = text[i - (uint32_t)(caption ? 2 : 0)];
    }
    (void)h;
    (void)type;
    buf[i] = '\n';
    buf[i + 1] = 0;
    sys_write(buf, i + 1);
    return 0;
}

typedef struct {
    char     name[16];
    uint32_t fn;
} win32_export_t;

win32_export_t __exports[] __attribute__((section(".exports"))) = {
    { "MessageBoxA", (uint32_t)&MessageBoxA },
    { "", 0 },
};