/* MyOS - user/win32/advapi32.c
 * advapi32.dll: modulo Win32 fijo (ring 3) en 0xB7000000.
 * Registro y miscelanea: stubs para metapad.exe. MyOS no tiene registro:
 *  - RegCreateKeyExA -> ERROR_SUCCESS con un hKey ficticio (1): metapad
 *    cree la clave y siga su flujo normal.
 *  - RegQueryValueExA -> ERROR_FILE_NOT_FOUND: la config "no existe" y
 *    metapad usa sus valores por defecto (igual que en una instalacion
 *    limpia de Windows).
 *  - RegSetValueExA -> ERROR_SUCCESS: guardar config no hace nada.
 *  - RegOpenKeyExA -> ERROR_FILE_NOT_FOUND.
 */

#include <stdint.h>

#define ERROR_SUCCESS          0
#define ERROR_FILE_NOT_FOUND   2

#define KEY_QUERY_VALUE        0x0001
#define KEY_SET_VALUE          0x0002
#define KEY_CREATE_SUB_KEY     0x0004
#define KEY_ENUMERATE_SUB_KEYS 0x0008
#define KEY_READ               0x20019
#define KEY_WRITE              0x20006
#define KEY_ALL_ACCESS         0xF003F

#define REG_SZ           1
#define REG_DWORD        4
#define REG_BINARY       3
#define REG_NONE         0

/* hKey ficticio que "abre" RegCreateKeyExA. */
#define FAKE_KEY         1

uint32_t __attribute__((stdcall)) RegCreateKeyExA(uint32_t hkey, const char *subkey, uint32_t res,
                         const char *cls, uint32_t opt, uint32_t access,
                         const void *sa, uint32_t *out, uint32_t *disp)
{
    (void)hkey; (void)subkey; (void)res; (void)cls; (void)opt;
    (void)access; (void)sa;
    if (out)
        *out = FAKE_KEY;
    if (disp)
        *disp = 1;              /* REG_CREATED_NEW_KEY */
    return ERROR_SUCCESS;
}

uint32_t __attribute__((stdcall)) RegOpenKeyExA(uint32_t hkey, const char *subkey, uint32_t opt,
                       uint32_t access, uint32_t *out)
{
    (void)hkey; (void)subkey; (void)opt; (void)access; (void)out;
    return ERROR_FILE_NOT_FOUND;
}

/* La clave nunca tiene valores: "no configurado" = defaults. */
uint32_t __attribute__((stdcall)) RegQueryValueExA(uint32_t hkey, const char *name, uint32_t *res,
                          uint32_t *type, void *data, uint32_t *size)
{
    (void)hkey; (void)name; (void)res; (void)type; (void)data; (void)size;
    return ERROR_FILE_NOT_FOUND;
}

uint32_t __attribute__((stdcall)) RegSetValueExA(uint32_t hkey, const char *name, uint32_t res,
                        uint32_t type, const void *data, uint32_t size)
{
    (void)hkey; (void)name; (void)res; (void)type; (void)data; (void)size;
    return ERROR_SUCCESS;
}

uint32_t __attribute__((stdcall)) RegCloseKey(uint32_t hkey)
{
    (void)hkey;
    return ERROR_SUCCESS;
}

/* IsTextUnicode: heuristica de deteccion de UTF-16; siempre "no". */
uint32_t __attribute__((stdcall)) IsTextUnicode(const void *buf, int len, uint32_t *result)
{
    (void)buf; (void)len;
    if (result)
        *result = 0;
    return 0;
}

typedef struct {
    char     name[32];
    uint32_t fn;
} win32_export_t;

win32_export_t __exports[] __attribute__((section(".exports"))) = {
    { "RegCreateKeyExA",  (uint32_t)&RegCreateKeyExA },
    { "RegOpenKeyExA",    (uint32_t)&RegOpenKeyExA },
    { "RegQueryValueExA", (uint32_t)&RegQueryValueExA },
    { "RegSetValueExA",   (uint32_t)&RegSetValueExA },
    { "RegCloseKey",      (uint32_t)&RegCloseKey },
    { "IsTextUnicode",    (uint32_t)&IsTextUnicode },
    { "", 0 },
};
