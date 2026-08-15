/* MyOS - user/win32/winspool.c
 * winspool.dll: modulo Win32 fijo (ring 3) en 0xB0B00000.
 * Fase 23-B8: impresion "a archivo": OpenPrinter/StartDocPrinter/
 * WritePrinter acumulan el trabajo y EndDoc/ClosePrinter lo vuelcan a
 * print.txt en el MEFS (SYS_FWRITE). */

#include <stdint.h>

#define SYS_FWRITE   27
#define SYS_FCREATE_IN 38
#define MEFS_ROOT   0xFFFFFFFFu

static uint32_t sys_fcreate_in(uint32_t parent, const char *name)
{
    uint32_t r;
    __asm__ volatile("int $0x80"
                     : "=a"(r)
                     : "a"(SYS_FCREATE_IN), "b"(parent), "c"(name)
                     : "memory");
    return r;
}

static uint32_t sys_fwrite(const char *name, const void *buf, uint32_t len)
{
    uint32_t r;
    __asm__ volatile("int $0x80"
                     : "=a"(r)
                     : "a"(SYS_FWRITE), "b"(name), "c"(buf), "d"(len)
                     : "memory");
    return r;
}

#define PRINT_BUF_MAX 32768

static char print_buf[PRINT_BUF_MAX];
static uint32_t print_len = 0;

static void print_flush(void)
{
    if (print_len > 0) {
        sys_fcreate_in(MEFS_ROOT, "print.txt");
        sys_fwrite("print.txt", print_buf, print_len);
        print_len = 0;
    }
}

uint32_t __attribute__((stdcall)) OpenPrinterA(const char *name, uint32_t *ph,
                       uint32_t pdefault)
{
    (void)name;
    (void)pdefault;
    if (ph)
        *ph = 1;
    return 1;
}

uint32_t __attribute__((stdcall)) StartDocPrinterA(uint32_t h, uint32_t level,
                           uint32_t docinfo)
{
    (void)h;
    (void)level;
    (void)docinfo;
    return 1;                       /* job id */
}

uint32_t __attribute__((stdcall)) StartPagePrinter(uint32_t h)
{
    (void)h;
    return 1;
}

uint32_t __attribute__((stdcall)) WritePrinter(uint32_t h, const void *buf,
                       uint32_t cb, uint32_t *written)
{
    uint32_t i;
    const char *s = (const char *)buf;
    (void)h;
    for (i = 0; i < cb && print_len < PRINT_BUF_MAX; i++)
        print_buf[print_len++] = s[i];
    if (written)
        *written = i;
    return 1;
}

uint32_t __attribute__((stdcall)) EndPagePrinter(uint32_t h)
{
    (void)h;
    return 1;
}

uint32_t __attribute__((stdcall)) EndDocPrinter(uint32_t h)
{
    (void)h;
    print_flush();
    return 1;
}

uint32_t __attribute__((stdcall)) ClosePrinter(uint32_t h)
{
    (void)h;
    print_flush();
    return 1;
}

typedef struct {
    char     name[32];
    uint32_t fn;
} win32_export_t;

win32_export_t __exports[] __attribute__((section(".exports"))) = {
    { "OpenPrinterA",     (uint32_t)&OpenPrinterA },
    { "StartDocPrinterA", (uint32_t)&StartDocPrinterA },
    { "StartPagePrinter", (uint32_t)&StartPagePrinter },
    { "WritePrinter",     (uint32_t)&WritePrinter },
    { "EndPagePrinter",   (uint32_t)&EndPagePrinter },
    { "EndDocPrinter",    (uint32_t)&EndDocPrinter },
    { "ClosePrinter",     (uint32_t)&ClosePrinter },
    { "", 0 },
};