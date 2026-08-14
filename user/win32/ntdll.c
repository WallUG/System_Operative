/* MyOS - user/win32/ntdll.c
 * ntdll.dll: modulo Win32 fijo (ring 3) en 0xB0200000.
 * Rutinas basicas de bajo nivel (Rtl*) que kernel32/user32 podrian
 * usar; por ahora solo RtlZeroMemory/RtlCopyMemory/GetModuleHandleA
 * (todas vacias/implementaciones triviales). */

#include <stdint.h>

void __attribute__((stdcall)) RtlZeroMemory(void *p, uint32_t n)
{
    uint8_t *b = (uint8_t *)p;
    while (n--)
        *b++ = 0;
}

void __attribute__((stdcall)) RtlCopyMemory(void *dst, const void *src, uint32_t n)
{
    const uint8_t *s = (const uint8_t *)src;
    uint8_t *d = (uint8_t *)dst;
    while (n-- > 0)
        *d++ = *s++;
}

uint32_t __attribute__((stdcall)) RtlWow64GetProcessMachines(void)
{
    return 0;
}

/* GetModuleHandle: devuelve la base del modulo llamador (0 = self). */
uint32_t __attribute__((stdcall)) GetModuleHandle(const char *name)
{
    (void)name;
    return 0xB0200000u;   /* self = ntdll (fijo) */
}

typedef struct {
    char     name[16];
    uint32_t fn;
} win32_export_t;

win32_export_t __exports[] __attribute__((section(".exports"))) = {
    { "RtlZeroMemory",      (uint32_t)&RtlZeroMemory },
    { "RtlCopyMemory",      (uint32_t)&RtlCopyMemory },
    { "RtlWow64Mach",   (uint32_t)&RtlWow64GetProcessMachines },
    { "GetModuleHandle",    (uint32_t)&GetModuleHandle },
    { "", 0 },
};