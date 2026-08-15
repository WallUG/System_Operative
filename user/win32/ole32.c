/* MyOS - user/win32/ole32.c
 * ole32.dll: modulo Win32 fijo (ring 3) en 0xB0900000.
 * Fase 23-B8: COM simplificado. CoInitialize/CoUninitialize son
 * no-op (no hay apartment); CoCreateInstance devuelve clase no
 * registrada (las apps de referencia muestran su error). */

#include <stdint.h>

#define SYS_MALLOC 10
#define SYS_FREE   11

static uint32_t sys_malloc2(uint32_t bytes)
{
    uint32_t r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(SYS_MALLOC), "b"(bytes)
                     : "memory");
    return r;
}

static void sys_free2(uint32_t p)
{
    uint32_t r;
    __asm__ volatile("int $0x80"
                     : "=a"(r)
                     : "a"(SYS_FREE)
                     : "memory");
    (void)p;
    (void)r;
}

uint32_t __attribute__((stdcall)) CoInitialize(uint32_t pv)
{
    (void)pv;
    return 0;                       /* S_OK */
}

void __attribute__((stdcall)) CoUninitialize(void)
{
}

uint32_t __attribute__((stdcall)) CoInitializeEx(uint32_t pv, uint32_t dw)
{
    (void)pv;
    (void)dw;
    return 0;                       /* S_OK */
}

uint32_t __attribute__((stdcall)) CoCreateInstance(uint32_t rclsid, uint32_t punk,
                           uint32_t ctx, uint32_t riid, uint32_t ppv)
{
    (void)rclsid; (void)punk; (void)ctx; (void)riid;
    if (ppv)
        *(uint32_t *)(uint32_t)ppv = 0;
    return 0x80040154u;             /* REGDB_E_CLASSNOTREG */
}

uint32_t __attribute__((stdcall)) CoTaskMemAlloc(uint32_t cb)
{
    if (cb == 0)
        cb = 1;
    return sys_malloc2(cb);
}

void __attribute__((stdcall)) CoTaskMemFree(uint32_t p)
{
    if (p)
        sys_free2(p);
}

uint32_t __attribute__((stdcall)) OleInitialize(uint32_t pv)
{
    (void)pv;
    return 0;
}

void __attribute__((stdcall)) OleUninitialize(void)
{
}

typedef struct {
    char     name[32];
    uint32_t fn;
} win32_export_t;

win32_export_t __exports[] __attribute__((section(".exports"))) = {
    { "CoInitialize",     (uint32_t)&CoInitialize },
    { "CoUninitialize",   (uint32_t)&CoUninitialize },
    { "CoInitializeEx",   (uint32_t)&CoInitializeEx },
    { "CoCreateInstance", (uint32_t)&CoCreateInstance },
    { "CoTaskMemAlloc",   (uint32_t)&CoTaskMemAlloc },
    { "CoTaskMemFree",    (uint32_t)&CoTaskMemFree },
    { "OleInitialize",    (uint32_t)&OleInitialize },
    { "OleUninitialize",  (uint32_t)&OleUninitialize },
    { "", 0 },
};