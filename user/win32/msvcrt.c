/* MyOS - user/win32/msvcrt.c
 * msvcrt.dll: modulo Win32 fijo (ring 3) enlazado a 0xB3000000
 * (region de modulos "Modulos ring 3 fijos", ver kernel/win32.h).
 *
 * Fase 9: shim del runtime C de Windows que necesita un .exe de
 * mingw-w64. El CRT del .exe (mainCRTStartup) es ESTATICO, pero su
 * capa de stdio/heap/arranque tira de las funciones de msvcrt.dll
 * catalogadas con objdump -p (ver Makefile: win_hello, hello_win.exe).
 *
 * Semantica:
 *   - salida: putchar/puts/fputc/fputs/fprintf/vfprintf escriben a la
 *     consola via SYS_WRITE; FILE* es opaco (_iob estatico, nadie lo
 *     toca fuera de este modulo).
 *   - heap: malloc/calloc/realloc/free -> SYS_MALLOC/SYS_FREE (bump
 *     allocator del kernel; realloc/free son best-effort).
 *   - __getmainargs: el CRT lo llama una vez al arrancar; rellena
 *     argc/argv con un programa fijo "hello.exe" sin argumentos.
 *   - exit/atexit/_cexit/abort: los atexit se registran y corren en
 *     orden inverso (LIFO) al terminar; exit/_exit/abort -> SYS_EXIT.
 *   - _initterm: invoca los constructores globales del rango del .exe
 *     (__xc_a..__xc_z) que le pasa mainCRTStartup.
 *   - _lock/_unlock/__set_app_type/__setusermatherr/signal/setvbuf/
 *     fflush: no-ops plausibles para un solo proceso sin buffering.
 *   - __lc_codepage: EXPORT DE DATOS (uint32_t); el CRT lo lee por
 *     IAT como puntero (mov eax,[IAT]; mov eax,[eax]).
 *   - _errno: variable estatica propia del modulo.
 */

#include <stdint.h>

#define SYS_EXIT   2
#define SYS_WRITE  7
#define SYS_MALLOC 10
#define SYS_FREE   11
#define SYS_FSIZE  8
#define SYS_DREAD  12
#define SYS_FWRITE 27
#define SYS_FCREATE_IN 38
#define SYS_FLUSH  29

/* --- Fase 23-C9: _open/_read/_write/_close (CRT low-level) ---
 * Tabla local de archivos abiertos sobre las syscalls MEFS. _O_TEXT
 * traduce LF<->CRLF; _O_BINARY pasa crudo. El oflag del mingw-w64 se
 * codifica en dwFlagsAndAttributes (aqui se usa directo). */

#define O_WRONLY   0x0001
#define O_RDWR     0x0002
#define O_CREAT    0x0100
#define O_TRUNC    0x0200
#define O_APPEND   0x0800
#define O_TEXT     0x4000

struct crt_file_s {
    char     name[40];
    uint32_t pos;
    uint32_t wlen;
    uint32_t mode;          /* 0 binario, 1 texto */
    uint32_t pend_cr;
    uint32_t writable;      /* abierto para escritura: solo se
                             * persiste en _close si es de escritura */
};

static struct crt_file_s crt_files[16];
static uint8_t crt_wbuf[65536];

static struct crt_file_s *crt_file_of(int fd)
{
    int i = fd - 0x100;
    if (i < 0 || i >= 16 || crt_files[i].name[0] == 0)
        return 0;
    return &crt_files[i];
}

static int sys_fsize(const char *name)
{
    int r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(SYS_FSIZE), "b"(name)
                     : "memory");
    return r;
}

static int sys_write(const char *s, uint32_t len);

static int sys_dread(const char *name, void *buf, uint32_t off, uint32_t max)
{
    int r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(SYS_DREAD), "b"(name), "c"(buf), "d"(off),
                       "S"(max)
                     : "memory");
    return r;
}

static int sys_fcreate_in(uint32_t parent, const char *name)
{
    int r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(SYS_FCREATE_IN), "b"(parent), "c"(name)
                     : "memory");
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
                     : "a"(SYS_FLUSH)
                     : "memory");
    return r;
}

int __attribute__((cdecl)) _open(const char *name, int oflag, int pmode)
{
    int i, sz, writable;
    (void)pmode;
    if (name == 0)
        return -1;
    writable = (oflag & (O_WRONLY | O_RDWR)) != 0;
    sz = sys_fsize(name);
    if (sz < 0) {
        if (!writable || !(oflag & O_CREAT))
            return -1;
        sys_fcreate_in(0xFFFFFFFFu, name);
        sz = 0;
    }
    for (i = 0; i < 16; i++)
        if (crt_files[i].name[0] == 0)
            break;
    if (i == 16)
        return -1;
    {
        uint32_t k = 0;
        while (k < 39 && name[k]) {
            crt_files[i].name[k] = name[k];
            k++;
        }
        crt_files[i].name[k] = 0;
    }
    crt_files[i].pos = 0;
    crt_files[i].wlen = 0;
    crt_files[i].mode = (oflag & O_TEXT) ? 1u : 0u;
    crt_files[i].pend_cr = 0;
    crt_files[i].writable = (uint32_t)writable;
    if (oflag & O_TRUNC)
        sys_fwrite(name, (const void *)"", 0);
    return 0x100 + i;
}

int __attribute__((cdecl)) _read(int fd, void *buf, unsigned int n)
{
    struct crt_file_s *f = crt_file_of(fd);
    uint8_t *dst = (uint8_t *)buf;
    uint32_t out = 0;
    if (fd == 0)
        return 0;
    if (f == 0)
        return -1;
    if (!f->mode) {
        int r = sys_dread(f->name, buf, f->pos, n);
        if (r <= 0)
            return 0;
        f->pos += (uint32_t)r;
        return r;
    }
    /* texto: CRLF -> LF (CR suelto se conserva) */
    {
        uint8_t raw[512];
        while (out < n) {
            int r = sys_dread(f->name, raw, f->pos, sizeof(raw));
            uint32_t k;
            if (r <= 0)
                break;
            f->pos += (uint32_t)r;
            for (k = 0; k < (uint32_t)r && out < n; k++) {
                uint8_t c = raw[k];
                if (f->pend_cr) {
                    f->pend_cr = 0;
                    if (c == '\n') {
                        dst[out++] = '\n';
                        continue;
                    }
                    dst[out++] = '\r';
                    if (c == '\r') {
                        f->pend_cr = 1;
                        continue;
                    }
                    dst[out++] = c;
                } else if (c == '\r') {
                    f->pend_cr = 1;
                } else {
                    dst[out++] = c;
                }
            }
        }
        if (f->pend_cr && out < n) {
            dst[out++] = '\r';
            f->pend_cr = 0;
        }
    }
    return (int)out;
}

int __attribute__((cdecl)) _write(int fd, const void *buf, unsigned int n)
{
    struct crt_file_s *f;
    if (fd == 1 || fd == 2)
        return sys_write((const char *)buf, n);
    f = crt_file_of(fd);
    if (f == 0)
        return -1;
    if (f->mode) {
        uint32_t k;
        for (k = 0; k < n && f->wlen < 65536; k++) {
            uint8_t c = ((const uint8_t *)buf)[k];
            if (c == '\n' && f->wlen + 1 < 65536) {
                crt_wbuf[f->wlen++] = '\r';
                crt_wbuf[f->wlen++] = '\n';
            } else {
                crt_wbuf[f->wlen++] = c;
            }
        }
        return (int)k;
    }
    if (f->wlen + n > 65536)
        n = 65536 - f->wlen;
    {
        uint32_t k;
        for (k = 0; k < n; k++)
            crt_wbuf[f->wlen + k] = ((const uint8_t *)buf)[k];
    }
    f->wlen += n;
    return (int)n;
}

int __attribute__((cdecl)) _close(int fd)
{
    struct crt_file_s *f;
    if (fd == 0 || fd == 1 || fd == 2)
        return 0;
    f = crt_file_of(fd);
    if (f == 0)
        return -1;
    if (f->writable) {
        if (f->wlen > 0)
            sys_fwrite(f->name, crt_wbuf, f->wlen);
        else
            sys_fwrite(f->name, (const void *)"", 0);
        sys_flush();
    }
    f->name[0] = 0;
    f->wlen = 0;
    return 0;
}

/* --- util --- */

static unsigned int u_strlen(const char *s)
{
    unsigned int n = 0;
    while (s[n]) n++;
    return n;
}

static void *u_memcpy(void *dst, const void *src, uint32_t n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    while (n--) *d++ = *s++;
    return dst;
}

static int u_strncmp(const char *a, const char *b, uint32_t n)
{
    while (n--) {
        unsigned char ca = (unsigned char)*a, cb = (unsigned char)*b;
        if (ca != cb)
            return (int)ca - (int)cb;
        if (ca == 0)
            return 0;
        a++;
        b++;
    }
    return 0;
}

/* --- syscalls --- */

static int sys_write(const char *s, uint32_t len)
{
    int r;
    __asm__ volatile("int $0x80"
                     : "=a"(r)
                     : "a"(SYS_WRITE), "b"(s), "c"(len)
                     : "memory");
    return r;
}

static void sys_exit(uint32_t code)
{
    int r;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(SYS_EXIT), "b"(code) : "memory");
    (void)r;
    for (;;) ;
}

static void *sys_malloc(uint32_t size)
{
    void *p;
    __asm__ volatile("int $0x80" : "=a"(p)
                     : "a"(SYS_MALLOC), "b"(size)
                     : "memory");
    return p;
}

/* --- FILE opaco (nadie inspecciona estos campos fuera del modulo) --- */

typedef struct {
    char  *_ptr;
    int    _cnt;
    char  *_base;
    int    _flag;          /* _IOREAD=1 _IOWRITE=2 */
    int    _fd;
    int    _charbuf;
    int    _bufsiz;
    char  *_tmpfname;
} FILE;

static FILE _iob[3] = {
    { 0, 0, 0, 1, 0, 0, 0, 0 },   /* stdin  */
    { 0, 0, 0, 2, 1, 0, 0, 0 },   /* stdout */
    { 0, 0, 0, 2, 2, 0, 0, 0 },   /* stderr */
};

/* __p__iob: devuelve puntero al array _iob (FILE*[3] del CRT) */
FILE **__p__iob(void) { return (FILE **)&_iob[0]; }

/* __p__acmdln: el CRT de mingw hace *__p__acmdln() para obtener la
 * linea de comandos del proceso al construir __argv/__argc. */
static const char *cmdline_from_tib(void);
static char *s_acmdln = 0;
char ***__p__acmdln(void)
{
    s_acmdln = (char *)cmdline_from_tib();
    return (char ***)&s_acmdln;
}

/* entradas "de ambiente" que pide el CRT en el arranque */
static int   s_commode    = 0;
static int   s_fmode      = 0;
static int   s_mb_cur_max = 1;
static char *s_initenv    = 0;

int   *__p__commode(void)     { return &s_commode; }
int   *__p__fmode(void)       { return &s_fmode; }
int   *__p___mb_cur_max(void) { return &s_mb_cur_max; }
char ***__p___initenv(void)  { return (char ***)&s_initenv; }

/* --- estado del CRT --- */

/* EXPORT DE DATOS: __lc_codepage = &g_lc_codepage (uint32_t); el CRT
 * hace mov eax,[IAT]; mov eax,[eax]. */
static uint32_t g_lc_codepage = 1252;   /* CP_ACP */

#define LC_CODEPAGE_ADDR ((uint32_t)&g_lc_codepage)

void __set_app_type(int t)         { (void)t; }
void __setusermatherr(uint32_t f)  { (void)f; }
void _lock(int fd)                 { (void)fd; }
void _unlock(int fd)               { (void)fd; }

/* --- errno --- */

static int s_errno;
int *_errno(void) { return &s_errno; }

/* --- runtime / salida --- */

void _amsg_exit(int n)
{
    char tmp[16];
    int  i = 0;
    tmp[i++] = 'e';
    tmp[i++] = 'r';
    tmp[i++] = 'r';
    tmp[i++] = '=';
    if (n < 0) { tmp[i++] = '-'; n = -n; }
    {
        char rev[12];
        int  k = 0;
        do { rev[k++] = (char)('0' + n % 10); n /= 10; } while (n && k < 12);
        while (k > 0) tmp[i++] = rev[--k];
    }
    tmp[i++] = '\n';
    sys_write(tmp, (int)i);
    sys_exit(3);
}

typedef void (*atexit_fn_t)(void);

#define ATEXIT_MAX 32
static atexit_fn_t atexit_tab[ATEXIT_MAX];
static int         atexit_n;

int atexit(atexit_fn_t fn)
{
    if (atexit_n >= ATEXIT_MAX)
        return -1;
    atexit_tab[atexit_n++] = fn;
    return 0;
}

void _cexit(void)
{
    while (atexit_n > 0)
        atexit_tab[--atexit_n]();
}

void _exit(int code)
{
    _cexit();
    sys_exit((uint32_t)code);
}

void exit(int code) { _exit(code); }

void abort(void)
{
    sys_write("abort\n", 6);
    sys_exit(3);
}

/* Retorno "de thread" del CRT: mainCRTStartup termina guardando el
 * puntero de pila inicial y haciendo `ret` a el (en Windows apunta al
 * stub de la runtime que termina el proceso). Como aqui el entry se
 * llama como una funcion, el kernel escribe [USER_ESP+0] con esta
 * direccion: al llegar aqui, eax = resultado de main(). */
void _crt_ret(void)
{
    uint32_t code;
    __asm__ volatile("movl %%eax, %0" : "=r"(code));
    sys_exit(code);
    for (;;) ;
}

/* --- _initterm --- */

void _initterm(void **start, void **end)
{
    void **p;
    for (p = start; p < end; p++)
        if (*p != 0)
            ((void (*)(void))*p)();
}

/* --- memoria --- */

void *malloc(unsigned int size)
{
    if (size == 0)
        size = 1;
    return sys_malloc(size);
}

void *calloc(unsigned int n, unsigned int size)
{
    unsigned char *p = (unsigned char *)sys_malloc(n * size);
    unsigned int i;
    if (p)
        for (i = 0; i < n * size; i++)
            p[i] = 0;
    return p;
}

void free(void *p)
{
    if (p) {
        int r;
        __asm__ volatile("int $0x80" : "=a"(r)
                         : "a"(SYS_FREE), "b"(p)
                         : "memory");
        (void)r;
    }
}

void *realloc(void *p, unsigned int size)
{
    void *n;
    unsigned int old, i;
    if (p == 0)
        return malloc(size);
    n = malloc(size);
    if (n == 0)
        return 0;
    /* tamano viejo: header de 16 B del heap de usuario (magic, size, ...) */
    old = ((unsigned int *)p)[-3];
    if (old > size)
        old = size;
    for (i = 0; i < old; i++)
        ((unsigned char *)n)[i] = ((unsigned char *)p)[i];
    free(p);
    return n;
}

/* --- cadenas que exporta el CRT (wide incluido) --- */

char *strerror(int e)
{
    (void)e;
    return (char *)"Unknown error";
}

/* --- cadenas/memoria que importa metapad.exe directamente del CRT --- */

static const char *u_strchr(const char *s, int c)
{
    while (*s) {
        if ((unsigned char)*s == (unsigned char)c)
            return s;
        s++;
    }
    return ((unsigned char)c == 0) ? s : 0;
}

static const char *u_strrchr(const char *s, int c)
{
    const char *last = 0;
    do {
        if ((unsigned char)*s == (unsigned char)c)
            last = s;
    } while (*s++);
    return last;
}

static void *u_memchr(const void *p, int c, unsigned int n)
{
    const unsigned char *b = (const unsigned char *)p;
    while (n--) {
        if (*b == (unsigned char)c)
            return (void *)b;
        b++;
    }
    return 0;
}

int  isdigit(int c)  { return c >= '0' && c <= '9'; }
int  isprint(int c)  { return c >= 32 && c <= 126; }
int  isalnum(int c)  { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z')
                            || (c >= 'A' && c <= 'Z'); }
int  isspace(int c)  { return c == ' ' || c == '\t' || c == '\n'
                            || c == '\r' || c == '\v' || c == '\f'; }
static void trace(const char *s) { sys_write(s, (uint32_t)u_strlen(s)); }

int  atoi(const char *s) { trace("[msv] atoi\n"); int v = 0, neg = 0; while (*s == ' ') s++;
                            if (*s == '-') { neg = 1; s++; }
                            while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; }
                            return neg ? -v : v; }
long atol(const char *s) { trace("[msv] atol\n"); return (long)atoi(s); }
char *strchr(const char *s, int c)  { trace("[msv] strchr\n"); return (char *)u_strchr(s, c); }
char *strrchr(const char *s, int c) { trace("[msv] strrchr\n"); return (char *)u_strrchr(s, c); }
void *memchr(const void *p, int c, unsigned int n) { trace("[msv] memchr\n"); return u_memchr(p, c, n); }

char *strncpy(char *d, const char *s, unsigned int n)
{
    unsigned int i;
    for (i = 0; i < n && s[i]; i++)
        d[i] = s[i];
    for (; i < n; i++)
        d[i] = 0;
    return d;
}

void *memset(void *d, int c, unsigned int n)
{
    unsigned char *b = (unsigned char *)d;
    while (n--)
        *b++ = (unsigned char)c;
    return d;
}

char *strerror_r(int e)          { (void)e; return (char *)"Unknown error"; }
int    strncmp(const char *a, const char *b, unsigned int n) { return u_strncmp(a, b, n); }
unsigned int strlen(const char *s) { return u_strlen(s); }
void *memcpy(void *d, const void *s, unsigned int n) { return u_memcpy(d, s, n); }

/* Fase 24-P2.3: strcpy/strcmp (blitclip usa ambos; antes crasheaban). */
char *strcpy(char *d, const char *s)
{
    char *r = d;
    while ((*d++ = *s++)) ;
    return r;
}
int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

unsigned int wcslen(const unsigned short *s)
{
    unsigned int n = 0;
    while (s[n]) n++;
    return n;
}

/* --- signal: almacenamos pero nunca se entrega --- */

typedef void (*sighandler)(int);

/* ==================================================================
 * Fase 25-W2A paso 3: entrada W del CRT (MSVC) + cadenas wchar_t.
 * El CRT de MSVC arranca con _wgetmainargs (VC7+: "_wgetmainargs",
 * VC6: "__wgetmainargs") y usa wcscpy/wcslen/_wcsicmp/_wtoi...
 * ================================================================== */

int __getmainargs(int *argc, char ***argv, char ***envp,
                  int dowildcard, void *startup);

/* Parsea la linea de comandos del TIB a tokens wchar_t. */
static int wsplit_cmdline(const char *cmd, unsigned short *out,
                          unsigned short **argv, int max)
{
    int n = 0;
    while (*cmd) {
        while (*cmd == ' ' || *cmd == '\t')
            cmd++;
        if (*cmd == 0)
            break;
        if (n >= max - 1)
            break;
        argv[n++] = out;
        if (*cmd == '"') {
            cmd++;
            while (*cmd && *cmd != '"')
                *out++ = (unsigned short)(unsigned char)*cmd++;
            if (*cmd == '"')
                cmd++;
        } else {
            while (*cmd && *cmd != ' ' && *cmd != '\t')
                *out++ = (unsigned short)(unsigned char)*cmd++;
        }
        *out++ = 0;
    }
    return n;
}

static unsigned short *w_argv_list[33];
static unsigned short w_buf[256];

int __attribute__((cdecl)) _wgetmainargs(int *argc, unsigned short ***argv,
                                         unsigned short ***envp,
                                         int *dontfree, int *mode)
{
    static unsigned short *w_envp_list[1];
    (void)dontfree; (void)mode;
    *argc = wsplit_cmdline(cmdline_from_tib(), w_buf, w_argv_list, 33);
    if (*argc < 1) {
        w_argv_list[0] = (unsigned short *)L"program.exe";
        *argc = 1;
    }
    w_argv_list[*argc] = 0;
    w_envp_list[0] = 0;
    *argv = w_argv_list;
    if (envp)
        *envp = w_envp_list;
    return 0;
}

int __attribute__((cdecl)) _getmainargs(int *argc, char ***argv, char ***envp,
                                        int dowildcard, void *startup)
{
    return __getmainargs(argc, argv, envp, dowildcard, startup);
}

/* --- cadenas wchar_t (ANSI C: sin _; msvcrt: wcslen ya esta) --- */

unsigned short *__attribute__((cdecl)) wcscpy(unsigned short *d, const unsigned short *s)
{
    unsigned int i = 0;
    do { d[i] = s[i]; } while (s[i++]);
    return d;
}

unsigned short *__attribute__((cdecl)) wcscat(unsigned short *d, const unsigned short *s)
{
    unsigned int i = 0, k = 0;
    while (d[i]) i++;
    while (s[k]) d[i++] = s[k++];
    d[i] = 0;
    return d;
}

int __attribute__((cdecl)) wcscmp(const unsigned short *a, const unsigned short *b)
{
    while (*a && *b && *a == *b) { a++; b++; }
    return (int)*a - (int)*b;
}

int __attribute__((cdecl)) wcsncmp(const unsigned short *a, const unsigned short *b,
                                   unsigned int n)
{
    unsigned int i;
    for (i = 0; i < n; i++) {
        if (a[i] != b[i])
            return (int)a[i] - (int)b[i];
        if (a[i] == 0)
            return 0;
    }
    return 0;
}

unsigned short *__attribute__((cdecl)) wcsncpy(unsigned short *d,
                        const unsigned short *s, unsigned int n)
{
    unsigned int i;
    for (i = 0; i < n && s[i]; i++)
        d[i] = s[i];
    for (; i < n; i++)
        d[i] = 0;
    return d;
}

unsigned short *__attribute__((cdecl)) wcschr(const unsigned short *s, unsigned short c)
{
    while (*s && *s != c) s++;
    return (*s == c) ? (unsigned short *)s : 0;
}

unsigned short *__attribute__((cdecl)) wcsrchr(const unsigned short *s, unsigned short c)
{
    const unsigned short *last = 0;
    while (*s) {
        if (*s == c)
            last = s;
        s++;
    }
    if (c == 0)
        return (unsigned short *)s;
    return (unsigned short *)last;
}

unsigned short *__attribute__((cdecl)) wcsstr(const unsigned short *h,
                        const unsigned short *n)
{
    unsigned int i, k;
    if (*n == 0)
        return (unsigned short *)h;
    for (i = 0; h[i]; i++) {
        for (k = 0; n[k] && h[i + k] == n[k]; k++) ;
        if (n[k] == 0)
            return (unsigned short *)&h[i];
    }
    return 0;
}

int __attribute__((cdecl)) _wcsicmp(const unsigned short *a, const unsigned short *b)
{
    for (;;) {
        unsigned short ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb)
            return (int)ca - (int)cb;
        if (ca == 0)
            return 0;
        a++; b++;
    }
}

int __attribute__((cdecl)) _wcsnicmp(const unsigned short *a, const unsigned short *b,
                                     unsigned int n)
{
    unsigned int i;
    for (i = 0; i < n; i++) {
        unsigned short ca = a[i], cb = b[i];
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb)
            return (int)ca - (int)cb;
        if (ca == 0)
            return 0;
    }
    return 0;
}

unsigned short *__attribute__((cdecl)) _wcslwr(unsigned short *s)
{
    unsigned int i;
    for (i = 0; s[i]; i++)
        if (s[i] >= 'A' && s[i] <= 'Z')
            s[i] += 32;
    return s;
}

unsigned short *__attribute__((cdecl)) _wcsupr(unsigned short *s)
{
    unsigned int i;
    for (i = 0; s[i]; i++)
        if (s[i] >= 'a' && s[i] <= 'z')
            s[i] -= 32;
    return s;
}

int __attribute__((cdecl)) _wtoi(const unsigned short *s)
{
    int v = 0, neg = 0;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (*s - '0');
        s++;
    }
    return neg ? -v : v;
}

long __attribute__((cdecl)) _wtol(const unsigned short *s)
{
    return (long)_wtoi(s);
}

unsigned short *__attribute__((cdecl)) _itow(int v, unsigned short *buf, int radix)
{
    char tmp[16];
    int p = 0, u = (v < 0 && radix == 10) ? -v : v;
    int i;
    (void)radix;
    do { tmp[p++] = (char)('0' + u % 10); u /= 10; } while (u);
    if (v < 0 && radix == 10)
        tmp[p++] = '-';
    for (i = 0; i < p; i++)
        buf[i] = (unsigned short)tmp[p - 1 - i];
    buf[i] = 0;
    return buf;
}

/* _wfopen: sin fopen real en el CRT; log + NULL (el binario objetivo
 * dara el diagnostico si lo necesita). */
void *__attribute__((cdecl)) _wfopen(const unsigned short *path,
                                     const unsigned short *mode)
{
    char p[128], m[16];
    int i;
    if (path != 0) {
        for (i = 0; i < 127 && path[i]; i++)
            p[i] = (char)path[i];
        p[i] = 0;
        sys_write(p, (uint32_t)i);
    }
    if (mode != 0) {
        for (i = 0; i < 15 && mode[i]; i++)
            m[i] = (char)mode[i];
        m[i] = 0;
        sys_write(" mode=", 6);
        sys_write(m, (uint32_t)i);
    }
    sys_write("\n", 1);
    return 0;
}

/* --- punteros del CRT W (mingw -municode) --- */

#define WIN32_TIB_CMDLINE_LEN 128u

static unsigned short w_cmdln[WIN32_TIB_CMDLINE_LEN];
static unsigned short *w_cmdln_ptr;
static unsigned short *w_initenv_arr[1];
static unsigned short **w_initenv_p;

/* __p__wcmdln: puntero a la linea de comandos W (la del TIB
 * convertida). El CRT de mingw -municode lo lee al arrancar. */
unsigned short **__attribute__((cdecl)) __p__wcmdln(void)
{
    const char *a = cmdline_from_tib();
    uint32_t i;
    for (i = 0; i < WIN32_TIB_CMDLINE_LEN - 1 && a[i]; i++)
        w_cmdln[i] = (unsigned short)(unsigned char)a[i];
    w_cmdln[i] = 0;
    w_cmdln_ptr = w_cmdln;
    return &w_cmdln_ptr;
}

/* __p___winitenv: entorno W (vacio). */
unsigned short ***__attribute__((cdecl)) __p___winitenv(void)
{
    w_initenv_arr[0] = 0;
    w_initenv_p = w_initenv_arr;
    return &w_initenv_p;
}

static sighandler sigtab[32];

void *signal(int sig, void *h)
{
    void *old;
    if (sig < 0 || sig > 31)
        return (void *)(intptr_t)-1;    /* SIG_ERR */
    old = (void *)sigtab[sig];
    sigtab[sig] = (sighandler)h;
    return old;
}

/* --- localeconv --- */

typedef struct {
    char *decimal_point, *thousands_sep, *grouping;
} lconv_shim;

static lconv_shim lc = { ".", "", "" };

void *localeconv(void) { return &lc; }

/* --- __getmainargs --- */

/* Linea de comandos real del proceso: vive en el TIB (el kernel la
 * escribe al lanzar/exec), %fs:0x18 da la base del TIB actual. */
#define WIN32_TIB_VA          0x84000000u
#define WIN32_TIB_CMDLINE_OFF 0x100u
#define WIN32_TIB_CMDLINE_LEN 128u

static const char *cmdline_from_tib(void)
{
    uint32_t tib = 0;
    __asm__ volatile("mov %%fs:0x18, %0" : "=r"(tib));
    return (const char *)(tib + WIN32_TIB_CMDLINE_OFF);
}

/* Parseo estilo Windows de la linea de comandos: espacios separan
 * argumentos, comillas dobles agrupan (sin escapes anidados, sufiente
 * para la shell y las pruebas). Devuelve el numero de argumentos. */
static int split_cmdline(const char *cmd, char *out, char **argv, int max)
{
    int n = 0;
    while (*cmd) {
        while (*cmd == ' ' || *cmd == '\t')
            cmd++;
        if (*cmd == 0)
            break;
        if (n >= max - 1)
            break;
        argv[n++] = out;
        if (*cmd == '"') {
            cmd++;
            while (*cmd && *cmd != '"')
                *out++ = *cmd++;
            if (*cmd == '"')
                cmd++;
        } else {
            while (*cmd && *cmd != ' ' && *cmd != '\t')
                *out++ = *cmd++;
        }
        *out++ = 0;
    }
    return n;
}

int __getmainargs(int *argc, char ***argv, char ***envp,
                  int dowildcard, void *startup)
{
    static char *argv_list[33];
    static char buf[256];
    static char *envp_list[1];
    (void)dowildcard;
    (void)startup;
    *argc = split_cmdline(cmdline_from_tib(), buf, argv_list, 33);
    if (*argc < 1) {
        argv_list[0] = (char *)"program.exe";   /* nunca deberia pasar */
        *argc = 1;
    }
    argv_list[*argc] = 0;
    envp_list[0] = 0;
    *argv = argv_list;
    if (envp)
        *envp = envp_list;
    return 0;
}

/* --- salida por consola: va_list de MSVCRT = char* --- */

typedef char *va_list_crt;   /* MSVCRT va_list es char* (i386 cdecl) */

static void write_str(const char *s)
{
    sys_write(s, u_strlen(s));
}

int putchar(int c)
{
    char ch = (char)c;
    sys_write(&ch, 1);
    return c;
}

int fputc(int c, FILE *f)
{
    (void)f;
    return putchar(c);
}

int puts(const char *s)
{
    write_str(s);
    sys_write("\n", 1);
    return 0;
}

int fputs(const char *s, FILE *f)
{
    (void)f;
    write_str(s);
    return 0;
}

int fflush(FILE *f) { (void)f; return 0; }

int setvbuf(FILE *f, char *buf, int mode, unsigned int size)
{
    (void)f; (void)buf; (void)mode; (void)size;
    return 0;
}

/* --- mini printf para _fprintf/_vfprintf (formato plano) --- */

static void emit_uint(unsigned long u, int hex)
{
    char tmp[16];
    int  i = 16;
    if (u == 0)
        tmp[--i] = '0';
    else
        while (u != 0) {
            if (hex)
                tmp[--i] = "0123456789abcdef"[u & 0xF];
            else
                tmp[--i] = (char)('0' + u % 10);
            u /= hex ? 16u : 10u;
        }
    write_str(&tmp[i]);
}

static void emit_number(unsigned int v, int neg, int hex)
{
    if (neg) {
        putchar('-');
        emit_uint((unsigned long)(0u - v), hex);
    } else {
        emit_uint((unsigned long)v, hex);
    }
}

static void vfprintf_stdout(const char *fmt, va_list_crt ap)
{
    for (;;) {
        char c = *fmt++;
        if (c == 0)
            break;
        if (c == '%') {
            int left = 0, width = 0;
            char spec;
            c = *fmt++;                 /* flags / ancho */
            for (;;) {
                if (c == '-') { left = 1; c = *fmt++; }
                else if (c == '0')      { c = *fmt++; }
                else if (c == '+')      { c = *fmt++; }
                else if (c == '.')      { c = *fmt++; }
                else if (c >= '0' && c <= '9') {
                    width = width * 10 + (c - '0');
                    c = *fmt++;
                } else {
                    break;
                }
            }
            if (c == 'l' || c == 'z' || c == 'j' || c == 'h' || c == 't') {
                c = *fmt++;             /* longitud: siempre leemos 32 bit */
                if (c == 'l')    c = *fmt++;
            }
            spec = c;
            if (spec == '%') { putchar('%'); continue; }
            if (spec == 's') {
                const char *s = *(const char **)ap;
                int n = 0;
                ap += 4;
                if (s != 0)
                    n = (int)u_strlen(s);
                if (!left)
                    for (; n < width; n++) putchar(' ');
                if (s != 0)
                    write_str(s);
                if (left)
                    for (n = (int)u_strlen(s); n < width; n++) putchar(' ');
                continue;
            }
            if (spec == 'c') {
                putchar(*(int *)ap);
                ap += 4;
                for (; width > 1; width--) putchar(' ');
                continue;
            }
            if (spec == 'd' || spec == 'i') {
                unsigned int v = *(unsigned int *)ap;
                int neg = 0, n = 0, t, i;
                ap += 4;
                if ((int)v < 0) { neg = 1; v = 0u - v; }
                t = (int)v;
                do { n++; t /= 10; } while (t);
                if (neg) n++;
                if (!left)
                    for (; n < width; n++) putchar(' ');
                if (neg) putchar('-');
                emit_uint((unsigned long)v, 0);
                if (left)
                    for (; n < width; n++) putchar(' ');
                (void)i;
                continue;
            }
            if (spec == 'u' || spec == 'x' || spec == 'X') {
                unsigned int v = *(unsigned int *)ap;
                int hex = (spec != 'u');
                int n = 0, t = (int)v, i;
                ap += 4;
                if (v == 0) n = 1;
                else {
                    while (t) { n++; t = hex ? t / 16 : t / 10; }
                    if (hex && v == 0) n = 1;
                }
                if (!left)
                    for (; n < width; n++) putchar(' ');
                emit_uint((unsigned long)v, hex);
                if (left)
                    for (; n < width; n++) putchar(' ');
                (void)i;
                continue;
            }
            if (spec == 'p') {
                unsigned int v = *(unsigned int *)ap;
                ap += 4;
                if (!left)
                    for (; 10 < width; width--) putchar(' ');
                putchar('0');
                putchar('x');
                emit_number(v, 0, 1);
                if (left)
                    for (; 10 < width; width--) putchar(' ');
                continue;
            }
            putchar(spec);
        } else {
            putchar(c);
        }
    }
}

int vfprintf(FILE *f, const char *fmt, va_list_crt ap)
{
    (void)f;
    vfprintf_stdout(fmt, ap);
    return 0;
}

int fprintf(FILE *f, const char *fmt, ...)
{
    char *ap = (char *)(&fmt + 1);     /* i386 cdecl: args siguen a fmt */
    (void)f;
    vfprintf_stdout(fmt, ap);
    return 0;
}

int printf(const char *fmt, ...)
{
    char *ap = (char *)(&fmt + 1);
    vfprintf_stdout(fmt, ap);
    return 0;
}

int fwrite(const void *buf, unsigned int size, unsigned int count, FILE *f)
{
    (void)f;
    sys_write((const char *)buf, size * count);
    return (int)count;
}

/* --- tabla de exports (formato kernel/win32.h) --- */

typedef struct {
    char     name[32];
    uint32_t fn;
} win32_export_t;

win32_export_t __exports[] __attribute__((section(".exports"))) = {
    { "_open",              (uint32_t)&_open },
    { "_read",              (uint32_t)&_read },
    { "_write",             (uint32_t)&_write },
    { "_close",             (uint32_t)&_close },
    { "__p__iob",           (uint32_t)__p__iob },
    { "__p__acmdln",        (uint32_t)__p__acmdln },
    { "__p__wcmdln",        (uint32_t)__p__wcmdln },
    { "__p___winitenv",     (uint32_t)__p___winitenv },
    { "__lc_codepage",      LC_CODEPAGE_ADDR },
    { "__p___initenv",      (uint32_t)__p___initenv },
    { "__p___mb_cur_max",   (uint32_t)__p___mb_cur_max },
    { "__p__commode",       (uint32_t)__p__commode },
    { "__p__fmode",         (uint32_t)__p__fmode },
    { "__set_app_type",     (uint32_t)__set_app_type },
    { "__setusermatherr",   (uint32_t)__setusermatherr },
    { "_amsg_exit",         (uint32_t)_amsg_exit },
    { "_cexit",             (uint32_t)_cexit },
    { "_errno",             (uint32_t)_errno },
    { "_initterm",          (uint32_t)_initterm },
    { "_lock",              (uint32_t)_lock },
    { "_unlock",            (uint32_t)_unlock },
    { "_exit",              (uint32_t)_exit },
    { "atexit",             (uint32_t)atexit },
    { "abort",              (uint32_t)abort },
    { "calloc",             (uint32_t)calloc },
    { "exit",               (uint32_t)exit },
    { "fflush",             (uint32_t)fflush },
    { "fwrite",             (uint32_t)fwrite },
    { "fprintf",            (uint32_t)fprintf },
    { "printf",             (uint32_t)printf },
    { "fputc",              (uint32_t)fputc },
    { "free",               (uint32_t)free },
    { "fputs",              (uint32_t)fputs },
    { "localeconv",         (uint32_t)localeconv },
    { "malloc",             (uint32_t)malloc },
    { "memcpy",             (uint32_t)memcpy },
    { "putchar",            (uint32_t)putchar },
    { "puts",               (uint32_t)puts },
    { "realloc",            (uint32_t)realloc },
    { "setvbuf",            (uint32_t)setvbuf },
    { "signal",             (uint32_t)signal },
    { "strerror",           (uint32_t)strerror },
    { "strlen",             (uint32_t)strlen },
    { "strcpy",              (uint32_t)strcpy },
    { "strcmp",              (uint32_t)strcmp },
    { "strncmp",            (uint32_t)strncmp },
    { "memset",             (uint32_t)memset },
    { "memchr",             (uint32_t)memchr },
    { "strchr",             (uint32_t)strchr },
    { "strrchr",            (uint32_t)strrchr },
    { "strncpy",            (uint32_t)strncpy },
    { "atoi",               (uint32_t)atoi },
    { "atol",               (uint32_t)atol },
    { "isdigit",            (uint32_t)isdigit },
    { "isprint",            (uint32_t)isprint },
    { "isalnum",            (uint32_t)isalnum },
    { "isspace",            (uint32_t)isspace },
    { "vfprintf",           (uint32_t)vfprintf },
    { "__getmainargs",      (uint32_t)__getmainargs },
    { "_getmainargs",        (uint32_t)_getmainargs },
    { "_wgetmainargs",       (uint32_t)_wgetmainargs },
    { "__wgetmainargs",      (uint32_t)_wgetmainargs },
    { "wcslen",             (uint32_t)wcslen },
    { "wcscpy",             (uint32_t)wcscpy },
    { "wcscat",             (uint32_t)wcscat },
    { "wcscmp",             (uint32_t)wcscmp },
    { "wcsncmp",            (uint32_t)wcsncmp },
    { "wcsncpy",            (uint32_t)wcsncpy },
    { "wcschr",             (uint32_t)wcschr },
    { "wcsrchr",            (uint32_t)wcsrchr },
    { "wcsstr",             (uint32_t)wcsstr },
    { "_wcsicmp",           (uint32_t)_wcsicmp },
    { "_wcsnicmp",          (uint32_t)_wcsnicmp },
    { "_wcslwr",            (uint32_t)_wcslwr },
    { "_wcsupr",            (uint32_t)_wcsupr },
    { "_wtoi",              (uint32_t)_wtoi },
    { "_wtol",              (uint32_t)_wtol },
    { "_itow",              (uint32_t)_itow },
    { "_wfopen",            (uint32_t)_wfopen },
    { "_crt_ret",           (uint32_t)_crt_ret },
    { "", 0 },
};