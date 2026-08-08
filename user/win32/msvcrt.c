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
    __asm__ volatile("int $0x80" : : "a"(SYS_EXIT), "b"(code) : "memory");
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
    if (p)
        __asm__ volatile("int $0x80" : : "a"(SYS_FREE) : "memory");
}

void *realloc(void *p, unsigned int size)
{
    (void)p;
    return malloc(size);
}

/* --- cadenas que exporta el CRT (wide incluido) --- */

char *strerror(int e)
{
    (void)e;
    return (char *)"Unknown error";
}

char *strerror_r(int e)          { (void)e; return (char *)"Unknown error"; }
int    strncmp(const char *a, const char *b, unsigned int n) { return u_strncmp(a, b, n); }
unsigned int strlen(const char *s) { return u_strlen(s); }
void *memcpy(void *d, const void *s, unsigned int n) { return u_memcpy(d, s, n); }

unsigned int wcslen(const unsigned short *s)
{
    unsigned int n = 0;
    while (s[n]) n++;
    return n;
}

/* --- signal: almacenamos pero nunca se entrega --- */

typedef void (*sighandler)(int);

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

static char *argv0 = "hello.exe";

int __getmainargs(int *argc, char ***argv, char ***envp,
                  int dowildcard, void *startup)
{
    static char *argv_list[2];
    static char *envp_list[1];
    (void)dowildcard;
    (void)startup;
    argv_list[0] = argv0;
    envp_list[0] = 0;
    *argc = 1;
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
    { "__p__iob",           (uint32_t)__p__iob },
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
    { "strncmp",            (uint32_t)strncmp },
    { "vfprintf",           (uint32_t)vfprintf },
    { "__getmainargs",      (uint32_t)__getmainargs },
    { "wcslen",             (uint32_t)wcslen },
    { "_crt_ret",           (uint32_t)_crt_ret },
    { "", 0 },
};