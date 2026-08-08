/* MyOS - user/win32/kernel32.c
 * kernel32.dll: modulo Win32 fijo (ring 3) enlazado a 0xB0000000
 * (alternativa "Modulos ring 3 fijos", ver kernel/win32.c).
 * Funciones minimas de la capa Win32 implementadas sobre las syscalls
 * de MyOS (int 0x80): GetStdHandle y WriteFile, mas la tabla de
 * exports .exports que el kernel usa para resolver los imports de los
 * .exe (win32_resolve). */

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

/* --- API Win32 (consola) --- */

/* Handles: 1 = salida estandar (consola). */
#define STD_OUTPUT_HANDLE 1

uint32_t GetStdHandle(uint32_t which)
{
    (void)which;
    return STD_OUTPUT_HANDLE;
}

uint32_t WriteFile(uint32_t h, const void *buf, uint32_t n,
                   uint32_t *written)
{
    int r;
    if (h != STD_OUTPUT_HANDLE)
        return 0;
    r = sys_write((const char *)buf, n);
    if (written)
        *written = (uint32_t)(r > 0 ? r : 0);
    return (uint32_t)(r > 0 ? 1 : 0);
}

/* Tabla de exports: name + VA de la funcion (el ELF esta enlazado a la
 * base fija, asi que son las direcciones finales en el PD de usuario). */
typedef struct {
    char     name[16];
    uint32_t fn;
} win32_export_t;

win32_export_t __exports[] __attribute__((section(".exports"))) = {
    { "GetStdHandle", (uint32_t)&GetStdHandle },
    { "WriteFile",    (uint32_t)&WriteFile },
    { "kernel32_version", (uint32_t)0x00010000 },
    { "", 0 },
};