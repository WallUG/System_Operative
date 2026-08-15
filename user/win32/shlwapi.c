/* MyOS - user/win32/shlwapi.c
 * shlwapi.dll: modulo Win32 fijo (ring 3) en 0xB0A00000.
 * Fase 23-B8: helpers de strings y rutas: StrStrI/StrCmpI
 * (case-insensitive), PathFileExists (SYS_DLOOKUP), PathFindFileName
 * y PathRemoveFileSpec. */

#include <stdint.h>

#define SYS_DLOOKUP 35
#define MEFS_ROOT   0xFFFFFFFFu

static uint32_t sys_dlookup(uint32_t parent, const char *name)
{
    uint32_t r;
    __asm__ volatile("int $0x80"
                     : "=a"(r)
                     : "a"(SYS_DLOOKUP), "b"(parent), "c"(name)
                     : "memory");
    return r;
}

static char lower(char c)
{
    if (c >= 'A' && c <= 'Z')
        return (char)(c + 32);
    return c;
}

/* StrStrI: busca sub en str sin distinguir mayusculas; devuelve el
 * puntero a la primera coincidencia o NULL. */
const char *__attribute__((stdcall)) StrStrI(const char *str, const char *sub)
{
    uint32_t i, j;
    if (str == 0 || sub == 0)
        return 0;
    for (i = 0; str[i]; i++) {
        for (j = 0; sub[j]; j++)
            if (lower(str[i + j]) != lower(sub[j]))
                break;
        if (sub[j] == 0)
            return &str[i];
    }
    return 0;
}

/* StrCmpI: compara sin distinguir mayusculas; 0 si igual. */
int __attribute__((stdcall)) StrCmpI(const char *a, const char *b)
{
    uint32_t i;
    if (a == 0 || b == 0)
        return a == b ? 0 : (a ? 1 : -1);
    for (i = 0; a[i] || b[i]; i++) {
        char ca = lower(a[i]), cb = lower(b[i]);
        if (ca != cb)
            return ca < cb ? -1 : 1;
    }
    return 0;
}

/* PathFindFileName: puntero al nombre tras el ultimo '\', '/' o ':'. */
const char *__attribute__((stdcall)) PathFindFileName(const char *path)
{
    const char *p = path;
    const char *last = path;
    if (path == 0)
        return 0;
    while (*p) {
        if (*p == '\\' || *p == '/' || *p == ':')
            last = p + 1;
        p++;
    }
    return last;
}

/* PathFileExists: 1 si el archivo (componente final de la ruta)
 * existe en la raiz del MEFS. */
uint32_t __attribute__((stdcall)) PathFileExists(const char *path)
{
    const char *name;
    uint32_t idx;
    if (path == 0 || *path == 0)
        return 0;
    name = PathFindFileName(path);
    if (name == 0 || *name == 0)
        return 0;
    idx = sys_dlookup(MEFS_ROOT, name);
    return idx != 0xFFFFFFFFu ? 1 : 0;
}

/* PathRemoveFileSpec: corta la ruta en el ultimo separador (deja el
 * directorio); 1 si habia algo que cortar. */
uint32_t __attribute__((stdcall)) PathRemoveFileSpec(char *path)
{
    uint32_t i;
    if (path == 0 || *path == 0)
        return 0;
    for (i = 0; path[i]; i++) ;
    while (i > 0) {
        i--;
        if (path[i] == '\\' || path[i] == '/' || path[i] == ':') {
            path[i] = 0;
            return 1;
        }
    }
    return 0;
}

typedef struct {
    char     name[32];
    uint32_t fn;
} win32_export_t;

win32_export_t __exports[] __attribute__((section(".exports"))) = {
    { "StrStrI",            (uint32_t)&StrStrI },
    { "StrStrIA",           (uint32_t)&StrStrI },
    { "StrCmpI",            (uint32_t)&StrCmpI },
    { "StrCmpIA",           (uint32_t)&StrCmpI },
    { "PathFileExists",     (uint32_t)&PathFileExists },
    { "PathFileExistsA",    (uint32_t)&PathFileExists },
    { "PathFindFileName",   (uint32_t)&PathFindFileName },
    { "PathFindFileNameA",  (uint32_t)&PathFindFileName },
    { "PathRemoveFileSpec", (uint32_t)&PathRemoveFileSpec },
    { "PathRemoveFileSpecA",(uint32_t)&PathRemoveFileSpec },
    { "", 0 },
};