/* MyOS - user/win32/advapi32.c
 * advapi32.dll: modulo Win32 fijo (ring 3) en 0xB7000000.
 * Fase 24-P1.3: registro persistente. MyOS no tiene registro de Windows;
 * las claves se guardan en memoria y se persisten a "registry.ini" en el
 * MEFS (SYS_FCREATE/FWRITE/FLUSH). Formato INI:
 *     [Software\App]
 *     name=sz:valor
 *     num=dword:123
 * El registro es por proceso: la tabla en memoria se siembra leyendo el
 * .ini al primer uso, y cada RegSetValueExA vuelca el archivo completo.
 */

#include <stdint.h>

#define KERNEL_BASE 0xB0000000u
#define SYS_FSIZE  8
#define SYS_FREAD  9
#define SYS_FCREATE 26
#define SYS_FWRITE 27
#define SYS_FLUSH  29

#define REG_FILE  "registry.ini"

#define ERROR_SUCCESS          0
#define ERROR_FILE_NOT_FOUND   2
#define ERROR_MORE_DATA        234

#define HKEY_CLASSES_ROOT      0x80000000u
#define HKEY_CURRENT_USER      0x80000001u
#define HKEY_LOCAL_MACHINE     0x80000002u
#define HKEY_USERS             0x80000003u

#define REG_SZ           1
#define REG_BINARY       3
#define REG_DWORD        4

#define MAX_KEYS   16
#define MAX_VALS   48
#define KEY_MAX    96
#define NAME_MAX   48
#define VAL_MAX    128

static char     rkeys[MAX_KEYS][KEY_MAX];
static int      rkey_n;

typedef struct {
    char     path[KEY_MAX];
    char     name[NAME_MAX];
    uint32_t type;
    uint32_t size;
    uint8_t  data[VAL_MAX];
} rval_t;
static rval_t  rvals[MAX_VALS];
static int     rval_n;
static int     rreg_inited;

/* --- syscalls MEFS --- */
static int sys_fsize(const char *name)
{
    int r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(SYS_FSIZE), "b"(name) : "memory");
    return r;
}
static int sys_fread(const char *name, void *buf, uint32_t len)
{
    int r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(SYS_FREAD), "b"(name), "c"(buf), "d"(len)
                     : "memory");
    return r;
}
static int sys_fcreate(const char *name)
{
    int r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(SYS_FCREATE), "b"(name) : "memory");
    return r;
}
static int sys_fwrite(const char *name, const void *buf, uint32_t len)
{
    int r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(SYS_FWRITE), "b"(name), "c"(buf), "d"(len)
                     : "memory");
    return r;
}
static int sys_flush(void)
{
    int r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(SYS_FLUSH) : "memory");
    return r;
}

/* --- helpers minimos (sin libc: build -nostdlib) --- */
static int mlen(const char *s)
{
    int n = 0;
    while (s[n]) n++;
    return n;
}
static void mcpy(char *d, const char *s)
{
    while ((*d++ = *s++)) ;
}
static int mcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}
static void mi2a(char *b, uint32_t v)
{
    char t[12]; int n = 0, i;
    do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    for (i = 0; i < n; i++) b[i] = t[n - 1 - i];
    b[n] = 0;
}
static uint32_t ma2i(const char *s)
{
    uint32_t v = 0;
    while (*s >= '0' && *s <= '9') v = v * 10 + (uint32_t)(*s++ - '0');
    return v;
}

/* --- tabla de claves/valores --- */
static int rkey_find(const char *path)
{
    int i;
    for (i = 0; i < rkey_n; i++)
        if (mcmp(rkeys[i], path) == 0)
            return i;
    return -1;
}
/* Devuelve handle (indice+1) de la clave, creandola si hace falta. */
static int rkey_handle(const char *path)
{
    int i = rkey_find(path);
    if (i < 0 && rkey_n < MAX_KEYS) {
        mcpy(rkeys[rkey_n], path);
        i = rkey_n++;
    }
    return i + 1;
}
static int rval_find(const char *path, const char *name)
{
    int i;
    for (i = 0; i < rval_n; i++)
        if (mcmp(rvals[i].path, path) == 0 &&
            mcmp(rvals[i].name, name) == 0)
            return i;
    return -1;
}
/* Construye la ruta de una clave hija. Raiz (hkey>=0x80000000) -> solo
 * el subkey; si no, hkey es un handle y se concatenan. */
static void rkey_path(uint32_t hkey, const char *subkey, char *out)
{
    if (subkey == 0) {
        out[0] = 0;
        return;
    }
    if (hkey >= 0x80000000u) {
        mcpy(out, subkey);
    } else {
        uint32_t h = hkey;
        if (h >= 1 && h <= (uint32_t)rkey_n) {
            mcpy(out, rkeys[h - 1]);
            out[mlen(out)] = '\\';
            mcpy(out + mlen(out), subkey);
        } else {
            mcpy(out, subkey);
        }
    }
}

/* --- persistencia a registry.ini --- */
/* Serializa un valor a "name=tt:valor". 'end' limita el buffer de
 * salida (line[256] en reg_save): los valores largos se cortan en vez
 * de desbordar la pila (Fase 24-P3.1: un REG_SZ sin NUL o un REG_BINARY
 * de 128 B escribian hex/'0's sobre el ret addr -> eip=0x30303030). */
static void val_enc(rval_t *v, char *out, char *end)
{
    int i, n;
    mcpy(out, v->name);
    out += mlen(out);
    *out++ = '=';
    if (v->type == REG_DWORD) {
        if (out + 10 < end) {
            *out++ = 'd'; *out++ = ':';
            mi2a(out, *(uint32_t *)v->data);
        }
    } else if (v->type == REG_SZ) {
        n = (int)v->size;
        if (n > VAL_MAX)
            n = VAL_MAX;
        if (out + 2 + n + 1 <= end) {
            *out++ = 's'; *out++ = ':';
            for (i = 0; i < n; i++)
                out[i] = ((const char *)v->data)[i];
            out[n] = 0;
        }
    } else {                       /* binary: hex */
        n = (int)v->size;
        if (n > VAL_MAX)
            n = VAL_MAX;
        if (out + 2 + n * 2 + 1 > end)
            n = (int)((end - out - 3) / 2);     /* cortar al caber */
        if (n > 0) {
            static const char *hx = "0123456789abcdef";
            *out++ = 'b'; *out++ = ':';
            for (i = 0; i < n; i++) {
                out[0] = hx[v->data[i] >> 4];
                out[1] = hx[v->data[i] & 0xF];
                out += 2;
            }
            *out = 0;
        }
    }
}

static void reg_save(void)
{
    static char buf[4096];
    char *p = buf, *end = buf + sizeof(buf);
    char line[256];
    int k, v, wrote;

    for (k = 0; k < rkey_n && p < end; k++) {
        p[0] = '['; p++;
        for (int i = 0; rkeys[k][i] && p < end - 2; i++) *p++ = rkeys[k][i];
        *p++ = ']'; *p++ = '\n';
        for (v = 0; v < rval_n; v++) {
            if (mcmp(rvals[v].path, rkeys[k]) != 0)
                continue;
            val_enc(&rvals[v], line, line + sizeof(line) - 1);
            for (int i = 0; line[i] && p < end - 1; i++) *p++ = line[i];
            *p++ = '\n';
        }
    }
    wrote = (int)(p - buf);
    if (wrote == 0)
        return;
    if (sys_fsize(REG_FILE) < 0)
        sys_fcreate(REG_FILE);
    sys_fwrite(REG_FILE, buf, (uint32_t)wrote);
    sys_flush();
}

static int hexv(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void reg_load(void)
{
    static char buf[4096];
    char path[KEY_MAX];
    char name[NAME_MAX];
    int sz, n, i, line;
    rval_t *v;

    if (rreg_inited)
        return;
    rreg_inited = 1;
    path[0] = 0;
    sz = sys_fsize(REG_FILE);
    if (sz <= 0 || sz > (int)sizeof(buf))
        return;
    n = sys_fread(REG_FILE, buf, (uint32_t)sz);
    if (n <= 0)
        return;
    buf[n] = 0;
    line = 0;
    while (line < n && rval_n < MAX_VALS) {
        char *ln = &buf[line];
        int e = 0;
        while (ln[e] != '\n' && ln[e] != 0) e++;
        ln[e] = 0;
        if (ln[0] == '[') {
            int j = 1;
            while (ln[j] != ']' && ln[j]) { path[j - 1] = ln[j]; j++; }
            path[j - 1] = 0;
        } else if (ln[0] != 0 && ln[0] != ';') {
            int eq = 0;
            while (ln[eq] != '=' && ln[eq]) eq++;
            if (ln[eq] == '=') {
                for (i = 0; i < eq && i < NAME_MAX - 1; i++) name[i] = ln[i];
                name[i] = 0;
                const char *vp = &ln[eq + 1];
                v = &rvals[rval_n];
                mcpy(v->path, path);
                mcpy(v->name, name);
                v->size = 0;
                if (vp[0] == 'd' && vp[1] == ':') {
                    v->type = REG_DWORD;
                    uint32_t d = ma2i(&vp[2]);
                    v->data[0] = d; v->data[1] = d >> 8;
                    v->data[2] = d >> 16; v->data[3] = d >> 24;
                    v->size = 4;
                    rval_n++;
                } else if (vp[0] == 's' && vp[1] == ':') {
                    v->type = REG_SZ;
                    mcpy((char *)v->data, &vp[2]);
                    v->size = (uint32_t)mlen((char *)v->data) + 1;
                    rval_n++;
                } else if (vp[0] == 'b' && vp[1] == ':') {
                    v->type = REG_BINARY;
                    const char *q = &vp[2];
                    while (q[0] && q[1] && v->size < VAL_MAX) {
                        int a = hexv(q[0]), b = hexv(q[1]);
                        if (a < 0 || b < 0) break;
                        v->data[v->size++] = (uint8_t)((a << 4) | b);
                        q += 2;
                    }
                    rval_n++;
                }
            }
        }
        line += e + 1;
    }
}

/* --- API Win32 --- */
uint32_t __attribute__((stdcall)) RegCreateKeyExA(uint32_t hkey, const char *subkey, uint32_t res,
                         const char *cls, uint32_t opt, uint32_t access,
                         const void *sa, uint32_t *out, uint32_t *disp)
{
    char path[KEY_MAX];
    int existed;
    (void)res; (void)cls; (void)opt; (void)access; (void)sa;
    reg_load();
    rkey_path(hkey, subkey, path);
    existed = rkey_find(path) >= 0;
    if (out)
        *out = (uint32_t)rkey_handle(path);
    if (disp)
        *disp = existed ? 2 /* REG_OPENED_EXISTING_KEY */
                        : 1 /* REG_CREATED_NEW_KEY */;
    return ERROR_SUCCESS;
}

uint32_t __attribute__((stdcall)) RegOpenKeyExA(uint32_t hkey, const char *subkey, uint32_t opt,
                       uint32_t access, uint32_t *out)
{
    char path[KEY_MAX];
    int i;
    (void)opt; (void)access;
    reg_load();
    rkey_path(hkey, subkey, path);
    i = rkey_find(path);
    if (i < 0)
        return ERROR_FILE_NOT_FOUND;
    if (out)
        *out = (uint32_t)(i + 1);
    return ERROR_SUCCESS;
}

uint32_t __attribute__((stdcall)) RegSetValueExA(uint32_t hkey, const char *name, uint32_t res,
                        uint32_t type, const void *data, uint32_t size)
{
    char path[KEY_MAX];
    int k, i, v;
    rval_t *rv;
    uint32_t n;
    (void)res;
    if (hkey == 0 || name == 0 || data == 0)
        return ERROR_FILE_NOT_FOUND;
    reg_load();
    k = (int)hkey - 1;
    if (k < 0 || k >= rkey_n)
        return ERROR_FILE_NOT_FOUND;
    mcpy(path, rkeys[k]);
    i = rval_find(path, name);
    if (i < 0) {
        if (rval_n >= MAX_VALS)
            return ERROR_FILE_NOT_FOUND;
        rv = &rvals[rval_n];
        mcpy(rv->path, path);
        mcpy(rv->name, name);
        rv->size = 0;
        rval_n++;
    } else {
        rv = &rvals[i];
    }
    rv->type = type;
    if (size > VAL_MAX)
        size = VAL_MAX;
    rv->size = size;
    for (v = 0; v < (int)size; v++)
        rv->data[v] = ((const uint8_t *)data)[v];
    if (type == REG_SZ && size == 0) {   /* SZ sin size: usar strlen+1 */
        n = 0;
        while (((const char *)data)[n]) n++;
        rv->size = n + 1;
    }
    reg_save();
    return ERROR_SUCCESS;
}

uint32_t __attribute__((stdcall)) RegQueryValueExA(uint32_t hkey, const char *name, uint32_t *res,
                          uint32_t *type, void *data, uint32_t *size)
{
    char path[KEY_MAX];
    int k, i, cp;
    (void)res;
    if (hkey == 0 || name == 0)
        return ERROR_FILE_NOT_FOUND;
    reg_load();
    k = (int)hkey - 1;
    if (k < 0 || k >= rkey_n)
        return ERROR_FILE_NOT_FOUND;
    mcpy(path, rkeys[k]);
    i = rval_find(path, name);
    if (i < 0)
        return ERROR_FILE_NOT_FOUND;
    if (type)
        *type = rvals[i].type;
    if (data) {
        cp = (rvals[i].size < VAL_MAX) ? (int)rvals[i].size : VAL_MAX;
        if (size && *size < (uint32_t)cp)
            return ERROR_MORE_DATA;
        for (int v = 0; v < cp; v++)
            ((uint8_t *)data)[v] = rvals[i].data[v];
    }
    if (size)
        *size = rvals[i].size;
    return ERROR_SUCCESS;
}

uint32_t __attribute__((stdcall)) RegDeleteValueA(uint32_t hkey, const char *name)
{
    char path[KEY_MAX];
    int k, i;
    if (hkey == 0 || name == 0)
        return ERROR_FILE_NOT_FOUND;
    reg_load();
    k = (int)hkey - 1;
    if (k < 0 || k >= rkey_n)
        return ERROR_FILE_NOT_FOUND;
    mcpy(path, rkeys[k]);
    i = rval_find(path, name);
    if (i < 0)
        return ERROR_FILE_NOT_FOUND;
    for (int v = i; v < rval_n - 1; v++)
        rvals[v] = rvals[v + 1];
    rval_n--;
    reg_save();
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
    { "RegDeleteValueA",  (uint32_t)&RegDeleteValueA },
    { "RegCloseKey",      (uint32_t)&RegCloseKey },
    { "IsTextUnicode",    (uint32_t)&IsTextUnicode },
    { "", 0 },
};