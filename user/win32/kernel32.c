/* MyOS - user/win32/kernel32.c
 * kernel32.dll: modulo Win32 fijo (ring 3) enlazado a 0xB0000000
 * (alternativa "Modulos ring 3 fijos", ver kernel/win32.c).
 *
 * Fase 9: kernel32 minimo que necesita el CRT de mingw-w64 (continuara
 * en msvcrt.c, con la mayor parte del trabajo). Funciones implementadas
 * sobre las syscalls int 0x80 de MyOS: consola (WriteFile/GetStdHandle),
 * exit (ExitProcess/TerminateProcess), modulos (GetModuleHandle/...
 * GetProcAddress resolve la propia tabla .exports), secciones criticas
 * (lock real de un bit), codepages ASCII y stubs de "sentido comun"
 * para lo que el CRT llama solo en caso de error.
 */

#include <stdint.h>

#define KERNEL_BASE 0xB0000000u

#define SYS_EXIT   2
#define SYS_GETPID 5
#define SYS_WRITE  7
#define SYS_MALLOC 10
#define SYS_FREE   11
#define SYS_FSIZE  8
#define SYS_DREAD  12
#define SYS_DLIST  13
#define SYS_SELFNAME 14
#define SYS_FCREATE 26
#define SYS_FWRITE  27
#define SYS_FDELETE 28
#define SYS_FLUSH   29
#define SYS_MKDIR   37
#define SYS_THREADCREATE 39 /* Fase 24-P2.2: ebx=fn, ecx=param, edx=&tid */
#define SYS_THREADEXIT 40   /* Fase 24-P2.2: ebx=code */
#define SYS_TICKS   41      /* ticks del timer (100 Hz) */
#define SYS_QPC     45      /* edx:eax = contador PIT alta resolucion */

/* Fase 25 (W2A): la frecuencia del PIT como QPC (1193182 Hz nominal;
 * el divisor cargado es 1193182/100 = 11931 unidades por tick). */
#define QPC_FREQ 1193182ull

#define INVALID_HANDLE_VALUE ((uint32_t)-1)

/* --- tabla de archivos abiertos (Fase E: escritura) --- */

struct win32_file_s {
    char     name[40];
    uint32_t size;
    uint32_t pos;
    uint32_t writable;      /* abierto para GENERIC_WRITE (Fase E) */
    uint32_t wlen;          /* bytes acumulados en wbuf               */
    uint8_t *wbuf;          /* buffer de escritura (apunta a wbuf_global) */
    uint32_t mode;          /* Fase 23-C9: 0=binario, 1=texto (_O_TEXT) */
    uint32_t pend_cr;       /* CR pendiente de la traduccion CRLF->LF  */
};

static struct win32_file_s open_files[16];
static uint8_t wbuf_global[65536];   /* buffer compartido de escritura */

/* handle -> puntero a la entrada abierta (o NULL) */
static struct win32_file_s *win32_file_of(uint32_t h)
{
    int i = (int)h - 0x100;
    if (i < 0 || i >= 16 || open_files[i].name[0] == 0)
        return 0;
    return &open_files[i];
}

/* --- util --- */

static int ci_eq(const char *a, const char *b)
{
    for (;;) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb)
            return 0;
        if (ca == 0)
            return 1;
        a++;
        b++;
    }
}

static unsigned int strlen_u(const char *s)
{
    unsigned int n = 0;
    while (s[n]) n++;
    return n;
}

static uint32_t copy_str_n(char *dst, uint32_t n, const char *src);

/* --- Fase 25 (W2A): conversion UTF-16 <-> cp437 ---
 * cp437 = la codepage de la consola/MEFS de MyOS. Los puntos Unicode
 * comunes (Latin-1 + simbolos) mapean a su byte cp437; lo no mapeado
 * se aplana a '?' (W->A) o U+FFFD (A->W). ASCII pasa directo. */

typedef struct { uint16_t u; uint8_t c; } cp437_map_t;

static const cp437_map_t cp437_map[] = {
    { 0x00C7,0x80 },{ 0x00FC,0x81 },{ 0x00E9,0x82 },{ 0x00E2,0x83 },
    { 0x00E4,0x84 },{ 0x00E0,0x85 },{ 0x00E5,0x86 },{ 0x00E7,0x87 },
    { 0x00EA,0x88 },{ 0x00EB,0x89 },{ 0x00E8,0x8A },{ 0x00EF,0x8B },
    { 0x00EE,0x8C },{ 0x00EC,0x8D },{ 0x00C4,0x8E },{ 0x00C5,0x8F },
    { 0x00C9,0x90 },{ 0x00E6,0x91 },{ 0x00C6,0x92 },{ 0x00F4,0x93 },
    { 0x00F6,0x94 },{ 0x00F2,0x95 },{ 0x00FB,0x96 },{ 0x00F9,0x97 },
    { 0x00FF,0x98 },{ 0x00D6,0x99 },{ 0x00DC,0x9A },{ 0x00A2,0x9B },
    { 0x00A3,0x9C },{ 0x00A5,0x9D },{ 0x20A7,0x9E },{ 0x0192,0x9F },
    { 0x00E1,0xA0 },{ 0x00ED,0xA1 },{ 0x00F3,0xA2 },{ 0x00FA,0xA3 },
    { 0x00F1,0xA4 },{ 0x00D1,0xA5 },{ 0x00AA,0xA6 },{ 0x00BA,0xA7 },
    { 0x00BF,0xA8 },{ 0x2310,0xA9 },{ 0x00AC,0xAA },{ 0x00BD,0xAB },
    { 0x00BC,0xAC },{ 0x00A1,0xAD },{ 0x00AB,0xAE },{ 0x00BB,0xAF },
    { 0x03B1,0xE0 },{ 0x00DF,0xE1 },{ 0x0393,0xE2 },{ 0x03C0,0xE3 },
    { 0x03A3,0xE4 },{ 0x03C3,0xE5 },{ 0x00B5,0xE6 },{ 0x03C4,0xE7 },
    { 0x03A6,0xE8 },{ 0x0398,0xE9 },{ 0x03A9,0xEA },{ 0x03B4,0xEB },
    { 0x221E,0xEC },{ 0x03C6,0xED },{ 0x03B5,0xEE },{ 0x2229,0xEF },
    { 0x2261,0xF0 },{ 0x00B1,0xF1 },{ 0x2265,0xF2 },{ 0x2264,0xF3 },
    { 0x2320,0xF4 },{ 0x2321,0xF5 },{ 0x00F7,0xF6 },{ 0x2248,0xF7 },
    { 0x00B0,0xF8 },{ 0x00B9,0xF9 },{ 0x00B7,0xFA },{ 0x221A,0xFB },
    { 0x00B3,0xFC },{ 0x00B2,0xFD },{ 0x25A0,0xFE },{ 0x00A0,0xFF },
};

/* UTF-16 (terminada en 0) -> cadena cp437 en out (max = bytes, NUL
 * incluido). */
static void utf16_to_cp437(char *out, const uint16_t *in, int max)
{
    int k = 0;
    if (max <= 0)
        return;
    while (k < max - 1 && in[k]) {
        uint16_t u = in[k];
        if (u < 0x80) {
            out[k] = (char)u;
        } else {
            unsigned i;
            char c = '?';
            for (i = 0; i < sizeof(cp437_map) / sizeof(cp437_map[0]); i++)
                if (cp437_map[i].u == u) {
                    c = (char)cp437_map[i].c;
                    break;
                }
            out[k] = c;
        }
        k++;
    }
    out[k] = 0;
}

/* cp437 (terminada en 0) -> UTF-16 en out (max = wchar_t, NUL
 * incluido). Devuelve el numero de wchar_t escritos (sin el NUL). */
static uint32_t cp437_to_utf16(uint16_t *out, uint32_t max, const char *in)
{
    uint32_t k = 0, i;
    if (max == 0)
        return 0;
    for (i = 0; in[i] && k < max - 1; i++) {
        uint8_t b = (uint8_t)in[i];
        uint16_t u = (b < 0x80) ? (uint16_t)b : 0xFFFD;
        if (b >= 0x80) {
            unsigned j;
            for (j = 0; j < sizeof(cp437_map) / sizeof(cp437_map[0]); j++)
                if (cp437_map[j].c == b) {
                    u = cp437_map[j].u;
                    break;
                }
        }
        out[k++] = u;
    }
    out[k] = 0;
    return k;
}

/* Longitud UTF-16 (sin el NUL). */
static uint32_t wstrlen(const uint16_t *s)
{
    uint32_t n = 0;
    while (s[n]) n++;
    return n;
}

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

static void trace(const char *s)
{
    sys_write(s, strlen_u(s));
}

static void trace_hex(uint32_t v)
{
    static const char hx[] = "0123456789abcdef";
    char tmp[10];
    int i;
    tmp[0] = '0';
    tmp[1] = 'x';
    for (i = 0; i < 8; i++)
        tmp[2 + i] = hx[(v >> (28 - i * 4)) & 0xF];
    sys_write(tmp, 10);
}

static void *win_malloc(uint32_t size)
{
    void *p;
    __asm__ volatile("int $0x80" : "=a"(p)
                     : "a"(SYS_MALLOC), "b"(size)
                     : "memory");
    return p;
}

/* --- consola / procesos --- */

#define STD_OUTPUT_HANDLE 1

uint32_t __attribute__((stdcall)) GetStdHandle(uint32_t which)
{
    (void)which;
    return STD_OUTPUT_HANDLE;
}

uint32_t __attribute__((stdcall)) WriteFile(uint32_t h, const void *buf,
                   uint32_t n, uint32_t *written, void *overlapped)
{
    int r;
    (void)overlapped;
    /* Fase E: handle de archivo abierto para escritura -> acumular en
     * wbuf; el contenido se escribe al FS en CloseHandle (o SetEndOfFile). */
    if (h != STD_OUTPUT_HANDLE) {
        struct win32_file_s *f = win32_file_of((uint32_t)h);
        if (f == 0 || !f->writable)
            return 0;
        if (f->mode) {
            /* Fase 23-C9: modo texto: LF -> CRLF al acumular */
            uint32_t k;
            for (k = 0; k < n && f->wlen < 65536; k++) {
                uint8_t c = ((const uint8_t *)buf)[k];
                if (c == '\n' && f->wlen + 1 < 65536) {
                    f->wbuf[f->wlen++] = '\r';
                    f->wbuf[f->wlen++] = '\n';
                } else {
                    f->wbuf[f->wlen++] = c;
                }
            }
            if (written)
                *written = k;
            return 1;
        }
        if (f->wlen + n > 65536)
            n = 65536 - f->wlen;
        {
            uint32_t k;
            for (k = 0; k < n; k++)
                f->wbuf[f->wlen + k] = ((const uint8_t *)buf)[k];
        }
        f->wlen += n;
        if (written)
            *written = n;
        return 1;
    }
    r = sys_write((const char *)buf, n);
    if (written)
        *written = (uint32_t)(r > 0 ? r : 0);
    return (uint32_t)(r > 0 ? 1 : 0);
}

void __attribute__((stdcall)) ExitProcess(uint32_t code)     { trace("[k32] ExitProcess\n"); sys_exit(code); }
void __attribute__((stdcall)) TerminateProcess(uint32_t h, uint32_t code) { (void)h; sys_exit(code); }
uint32_t __attribute__((stdcall)) GetCurrentProcess(void)    { return (uint32_t)-1; }

static uint32_t sys_getpid(void)
{
    uint32_t r;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(5));
    return r;
}

uint32_t __attribute__((stdcall)) GetCurrentProcessId(void)  { return sys_getpid(); }

/* Fase 25: GetCurrentThreadId — cada hilo del kernel tiene su propio
 * pid (task_create_thread los numeran), asi que el pid de la tarea
 * actual ES el id del hilo. La semantica Windows (pid != tid, tid del
 * hilo principal == pid del proceso) queda aproximada. */
uint32_t __attribute__((stdcall)) GetCurrentThreadId(void) { return sys_getpid(); }

static uint32_t sys_ticks(void)
{
    uint32_t r;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(SYS_TICKS));
    return r;
}

/* Contador PIT de alta resolucion (edx:eax). */
static uint64_t sys_qpc(void)
{
    uint32_t lo, hi;
    __asm__ volatile("int $0x80" : "=a"(lo), "=d"(hi) : "a"(SYS_QPC));
    return ((uint64_t)hi << 32) | lo;
}

static uint32_t sys_selfname(char *buf, uint32_t max)
{
    uint32_t r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(SYS_SELFNAME), "b"(buf), "c"(max)
                     : "memory");
    return r;
}

/* Devuelve el nombre del ejecutable actual (como lo lanzo la shell).
 * hmodule = NULL significa "el proceso actual". Como el lanzador no
 * da ruta, se sintetiza "C:\MyOS\<nombre>" para que los .exe que
 * hacen strrchr(...,'\\') encuentren una barra y puedan derivar su
 * directorio de trabajo. */
uint32_t __attribute__((stdcall)) GetModuleFileNameA(uint32_t hmodule, char *buf, uint32_t max)
{
    const char prefix[] = "C:\\MyOS\\";
    uint32_t n, i, plen = sizeof(prefix) - 1;

    (void)hmodule;
    if (buf == 0 || max == 0)
        return 0;
    for (i = 0; i < plen && i < max - 1; i++)
        buf[i] = prefix[i];
    if (i == max - 1) {
        buf[i] = 0;
        return i;
    }
    n = sys_selfname(buf + plen, max - plen);
    if (n == (uint32_t)-1)
        return 0;
    return plen + n;
}

/* Fase 25: GetModuleFileNameW (MSVC): la A rellena ASCII; convertir
 * A->UTF-16 en el buffer W del caller (max en wchar_t). */
uint32_t __attribute__((stdcall)) GetModuleFileNameW(uint32_t hmodule, uint16_t *buf,
                                                    uint32_t max)
{
    char a[64];
    uint32_t n, i;

    if (buf == 0 || max == 0)
        return 0;
    n = GetModuleFileNameA(hmodule, a, sizeof(a));
    if (n == 0)
        return 0;
    if (n >= max)
        n = max - 1;
    for (i = 0; i < n; i++)
        buf[i] = (uint16_t)(uint8_t)a[i];
    buf[i] = 0;
    return n;
}

/* Linea de comandos real del proceso: el kernel la copia al TIB de la
 * tarea (WIN32_TIB_CMDLINE_OFF) al lanzarla o hacer exec; %fs:0x18 da
 * la base del TIB de la tarea actual. */
#define WIN32_TIB_VA          0x84000000u
#define WIN32_TIB_CMDLINE_OFF 0x100u
#define WIN32_TIB_CMDLINE_LEN 128u

char *__attribute__((stdcall)) GetCommandLineA(void)
{
    trace("[k32] GetCommandLineA\n");
    uint32_t tib = 0;
    __asm__ volatile("mov %%fs:0x18, %0" : "=r"(tib));
    return (char *)(tib + WIN32_TIB_CMDLINE_OFF);
}

/* Fase 25: GetCommandLineW real (MSVC): convierte la linea ASCII del
 * TIB a UTF-16 en un buffer estatico (las DLLs se mapean por proceso,
 * asi que el buffer es privado de cada proceso). */
static uint16_t w_cmdline[WIN32_TIB_CMDLINE_LEN];

uint16_t *__attribute__((stdcall)) GetCommandLineW(void)
{
    char *a = GetCommandLineA();
    uint32_t i;
    for (i = 0; i < WIN32_TIB_CMDLINE_LEN - 1 && a[i]; i++)
        w_cmdline[i] = (uint16_t)(uint8_t)a[i];
    w_cmdline[i] = 0;
    return w_cmdline;
}

static char *env_block[] = { (char *)"PATH=.\0HOME=.\0", 0 };

char **__attribute__((stdcall)) GetEnvironmentStringsA(void) { return env_block; }
void  __attribute__((stdcall)) FreeEnvironmentStringsA(char **p) { (void)p; }

/* Fase 25: bloque de entorno en UTF-16 ("PATH=.\0HOME=.\0\0"). */
static uint16_t w_env_block[] = {
    'P', 'A', 'T', 'H', '=', '.', 0,
    'H', 'O', 'M', 'E', '=', '.', 0,
    0
};

uint16_t *__attribute__((stdcall)) GetEnvironmentStringsW(void) { return w_env_block; }
void __attribute__((stdcall)) FreeEnvironmentStringsW(uint16_t *p) { (void)p; }

/* --- errores --- */

static uint32_t last_error;

uint32_t __attribute__((stdcall)) GetLastError(void)       { return last_error; }
void     __attribute__((stdcall)) SetLastError(uint32_t e) { last_error = e; }

/* --- secciones criticas (RTL_CRITICAL_SECTION vista como int32) --- */

typedef struct { volatile int32_t lock; } cs_t;

void __attribute__((stdcall)) InitializeCriticalSection(void *lp)
{
    cs_t *c = (cs_t *)lp;
    c->lock = 0;
}

void __attribute__((stdcall)) EnterCriticalSection(void *lp)
{
    cs_t *c = (cs_t *)lp;
    for (;;) {
        __asm__ volatile("lock btsl $0, %0" : "+m"(c->lock) : : "cc");
        if (!(c->lock & 1))
            return;
        for (volatile int i = 0; i < 200; i++) ;
    }
}

void __attribute__((stdcall)) LeaveCriticalSection(void *lp)
{
    cs_t *c = (cs_t *)lp;
    __asm__ volatile("lock btrl $0, %0" : "+m"(c->lock) : : "cc");
}

void __attribute__((stdcall)) DeleteCriticalSection(void *lp) { (void)lp; }

/* IsDBCSLeadByte: 0 con codepage de un byte (CP1252, el unico que
 * soportamos); el CRT mingw lo usa en mbstowcs. */
int __attribute__((stdcall)) IsDBCSLeadByte(int b) { (void)b; return 0; }

/* --- TLS --- */

static uint32_t tls_slots[64];

void *   __attribute__((stdcall)) TlsGetValue(uint32_t s) { return (s < 64) ? (void *)tls_slots[s] : 0; }
void     __attribute__((stdcall)) TlsSetValue(uint32_t s, void *v) { if (s < 64) tls_slots[s] = (uint32_t)v; }
uint32_t __attribute__((stdcall)) TlsAlloc(void) { return 1; }
void     __attribute__((stdcall)) TlsFree(uint32_t s) { (void)s; }

/* --- modulos / GetProcAddress --- */

typedef struct {
    char     name[32];
    uint32_t fn;
} win32_export_t;

extern win32_export_t __exports[];

/* Modulos Win32: la base REAL de carga la pregunta el kernel (syscall
 * SYS_DLLBASE, Fase 20-C: las DLLs pueden cargar en direcciones
 * variables). RICHED20 no tiene modulo propio en el kernel: su base es
 * virtual (0xB9000000) y se usa como fallback. */
typedef struct {
    const char *name;
    uint32_t    vbase;      /* base virtual si el kernel no la carga */
} mod_desc_t;

static const mod_desc_t mod_descs[] = {
    { "kernel32.dll",  0xB0000000u },
    { "user32.dll",    0xB0100000u },
    { "ntdll.dll",     0xB0200000u },
    { "msvcrt.dll",    0xB0300000u },
    { "gdi32.dll",     0xB0400000u },
    { "comctl32.dll",  0xB0500000u },
    { "comdlg32.dll",  0xB0600000u },
    { "advapi32.dll",  0xB0700000u },
    { "shell32.dll",   0xB0800000u },
    { "ole32.dll",     0xB0900000u },
    { "shlwapi.dll",   0xB0A00000u },
    { "winspool.drv",  0xB0B00000u },
    { "riched20.dll",  0xB9000000u },
    { 0, 0 },
};

#define SYS_DLLBASE 31

static uint32_t sys_dllbase(const char *name)
{
    uint32_t r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(SYS_DLLBASE), "b"(name)
                     : "memory");
    return r;
}

uint32_t __attribute__((stdcall)) GetModuleHandleA(const char *name)
{
    const mod_desc_t *m;
    uint32_t i, base;
    trace("[k32] GetModuleHandleA\n");
    if (name == 0)
        return KERNEL_BASE;
    for (m = mod_descs; m->name; m++)
        for (i = 0; m->name[i]; i++)
            if (ci_eq(name, m->name)) {
                base = sys_dllbase(m->name);
                return base != 0 ? base : m->vbase;
            }
    return 0;
}

#define SYS_GETPROC 32

uint32_t __attribute__((stdcall)) GetProcAddress(uint32_t hmod, const char *name)
{
    trace("[k32] GetProcAddress\n");
    uint32_t i, r;
    if (hmod == 0)
        hmod = KERNEL_BASE;
    if (hmod == KERNEL_BASE) {
        for (i = 0; __exports[i].name[0]; i++)
            if (ci_eq(__exports[i].name, name))
                return __exports[i].fn;
        return 0;
    }
    /* Otra DLL: el kernel resuelve el export por la base real */
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(SYS_GETPROC), "b"(hmod), "c"(name)
                     : "memory");
    return r;
}

uint32_t __attribute__((stdcall)) LoadLibraryA(const char *name)
{
    uint32_t h;
    trace("[k32] LoadLibraryA\n");
    if (name == 0)
        return 0;
    h = GetModuleHandleA(name);
    return h != 0 ? h : 0;
}

uint32_t __attribute__((stdcall)) FreeLibrary(uint32_t h) { (void)h; return 1; }

/* --- codepage ASCII (CP_ACP/CP_OEM/MB para el CRT) --- */

typedef struct {
    uint32_t max_char_size;
    uint8_t  default_char[8];
    uint8_t  lead[12];
} CPINFO;

uint32_t __attribute__((stdcall)) MultiByteToWideChar(uint32_t cp, uint32_t flags,
                             const char *mbs, int mbs_len,
                             void *wbs, int wb_max)
{
    uint16_t *w = (uint16_t *)wbs;
    int n = (mbs_len < 0) ? (int)strlen_u((const char *)mbs) + 1 : mbs_len;
    int i;
    (void)cp; (void)flags;
    if (w == 0)
        return (uint32_t)n;
    if (n > wb_max)
        return 0;
    for (i = 0; i < n; i++)
        w[i] = (uint16_t)(uint8_t)mbs[i];
    return (uint32_t)n;
}

uint32_t __attribute__((stdcall)) WideCharToMultiByte(uint32_t cp, uint32_t flags,
                             const void *wcs, int wbs_len,
                             char *mbs, int mb_max,
                             const char *def, int *used)
{
    const uint16_t *w = (const uint16_t *)wcs;
    int n = (wbs_len < 0) ? -1 : wbs_len;
    int i;
    (void)cp; (void)flags; (void)def;
    if (n < 0) {
        n = 0;
        while (w[n]) n++;
        n++;
    }
    if (mb_max < n)
        return 0;
    for (i = 0; i < n; i++)
        mbs[i] = (char)(w[i] & 0xFF);
    if (used) *used = (uint32_t)n;
    return (uint32_t)n;
}

uint32_t __attribute__((stdcall)) GetCPInfo(uint32_t cp, CPINFO *info)
{
    int i;
    (void)cp;
    if (info == 0)
        return 0;
    info->max_char_size = 1;
    info->default_char[0] = '?';
    for (i = 1; i < 8; i++)  info->default_char[i] = 0;
    for (i = 0; i < 12; i++) info->lead[i] = 0;
    return 1;
}

/* --- excepciones / sistema --- */

static uint32_t unhandled_filter;

uint32_t __attribute__((stdcall)) SetUnhandledExceptionFilter(uint32_t fn)
{
    uint32_t old = unhandled_filter;
    unhandled_filter = fn;
    return old;
}

uint32_t __attribute__((stdcall)) UnhandledExceptionFilter(uint32_t ei) { (void)ei; return 1; }
int __attribute__((stdcall)) IsDebuggerPresent(void) { return 0; }

void __attribute__((stdcall)) Sleep(uint32_t ms)
{
    uint32_t end = sys_ticks() * 10 + ms;   /* GetTickCount + ms */
    while (sys_ticks() * 10 < end) ;         /* espera real por ticks */
}

uint32_t __attribute__((stdcall)) GetTickCount(void) { return sys_ticks() * 10; }

/* Fase 25: reloj REAL (no stub). FILETIME = unidades de 100 ns desde
 * 1601-01-01. Origen fijo: 2024-01-01T00:00:00Z (unix 1704067200) +
 * offset 1601->1970 (11644473600) = 13348540800 s -> x10^7.
 * Todo aritmetica 32-bit (sin __udivdi3 en ring 3); la fuente es el
 * PIT a 100 Hz, asi que la resolucion es de 10 ms (suficiente para
 * time()/_time64, semilla de rand y tmpnam). */
#define FT_EPOCH_2024 133485408000000000ull

void __attribute__((stdcall)) GetSystemTimeAsFileTime(uint32_t *t)
{
    uint32_t ticks = sys_ticks();
    uint32_t sec = ticks / 100;
    uint32_t ms = (ticks % 100) * 10;   /* 0..990 */
    uint64_t ft;

    if (!t)
        return;
    ft = FT_EPOCH_2024 + (uint64_t)sec * 10000000ull
         + (uint64_t)ms * 10000ull;
    t[0] = (uint32_t)ft;
    t[1] = (uint32_t)(ft >> 32);
}

uint32_t __attribute__((stdcall)) QueryPerformanceCounter(void *c)
{
    if (c)
        *(uint64_t *)c = sys_qpc();
    return 1;
}

uint32_t __attribute__((stdcall)) QueryPerformanceFrequency(void *c)
{
    if (c)
        *(uint64_t *)c = QPC_FREQ;
    return 1;
}

uint32_t GetSystemTime(uint32_t *t)
{
    if (t) {
        int i;
        for (i = 0; i < 8; i++)
            ((uint32_t *)t)[i] = 0;
    }
    return 0;
}

/* --- memoria virtual (no-ops plausibles) --- */

#define PAGE_READWRITE 0x04u
#define MEM_COMMIT     0x1000u
#define MEM_RESERVE    0x2000u
#define MEM_PRIVATE    0x20000u

typedef struct {
    uint32_t base;
    uint32_t alloc_base;
    uint32_t region_size;
    uint32_t state;
    uint32_t protect;
    uint32_t type;
} MEMORY_BASIC_INFORMATION;

uint32_t __attribute__((stdcall)) VirtualProtect(void *addr, uint32_t size, uint32_t prot,
                        uint32_t *old)
{
    (void)addr; (void)size; (void)prot;
    if (old) *old = PAGE_READWRITE;
    return 1;
}

uint32_t __attribute__((stdcall)) VirtualQuery(const void *addr, MEMORY_BASIC_INFORMATION *mbi,
                      uint32_t len)
{
    if (mbi == 0 || len < sizeof(*mbi))
        return 0;
    mbi->base = (uint32_t)addr & ~0xFFFu;
    mbi->alloc_base = 0x80000000u;
    mbi->region_size = 0x1000;
    mbi->state = MEM_COMMIT;
    mbi->protect = PAGE_READWRITE;
    mbi->type = MEM_PRIVATE;
    return sizeof(*mbi);
}

void *__attribute__((stdcall)) VirtualAlloc(void *addr, uint32_t size, uint32_t type, uint32_t prot)
{
    (void)addr; (void)type; (void)prot;
    return win_malloc(size);
}

uint32_t __attribute__((stdcall)) VirtualFree(void *p, uint32_t size, uint32_t type)
{
    (void)p; (void)size; (void)type;
    return 1;
}

/* --- heap (bump del kernel via SYS_MALLOC) --- */

static void win_free(void *p)
{
    int r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(SYS_FREE), "b"(p)
                     : "memory");
    (void)r;
}

uint32_t __attribute__((stdcall)) GetProcessHeap(void) { return 1; }
uint32_t __attribute__((stdcall)) HeapSize(uint32_t h, uint32_t f, const void *p);

uint32_t __attribute__((stdcall)) HeapCreate(uint32_t flags, uint32_t init,
                     uint32_t max)
{
    (void)flags; (void)init; (void)max;
    return 1;                       /* el heap del proceso */
}

uint32_t __attribute__((stdcall)) HeapDestroy(uint32_t heap)
{
    (void)heap;
    return 1;
}

void *__attribute__((stdcall)) HeapAlloc(uint32_t heap, uint32_t flags, uint32_t size)
{
    (void)heap; (void)flags;
    return win_malloc(size);
}

uint32_t __attribute__((stdcall)) HeapFree(uint32_t heap, uint32_t flags, void *p)
{
    (void)heap; (void)flags;
    win_free(p);
    return 1;
}

void *__attribute__((stdcall)) HeapReAlloc(uint32_t heap, uint32_t flags, void *p, uint32_t size)
{
    void *n;
    uint32_t old, i;
    (void)heap; (void)flags;
    n = win_malloc(size);
    if (n == 0)
        return 0;
    old = HeapSize(0, 0, p);
    if (old > size)
        old = size;
    {
        const uint8_t *s = (const uint8_t *)p;
        uint8_t *d = (uint8_t *)n;
        for (i = 0; i < old; i++)
            d[i] = s[i];
    }
    win_free(p);
    return n;
}

uint32_t __attribute__((stdcall)) HeapSize(uint32_t heap, uint32_t flags, const void *p)
{
    uint32_t sz;
    (void)heap; (void)flags;
    if (p == 0)
        return 0;
    /* el header de 16 B del bloque vive en el heap de usuario */
    sz = ((const uint32_t *)p)[-3];     /* offset +4 del header: size */
    return sz;
}

void __attribute__((stdcall)) GetStartupInfo(void *si)
{
    trace("[k32] GetStartupInfo\n");
    if (si) {
        for (uint32_t i = 0; i < 16; i++)
            ((uint32_t *)si)[i] = 0;
    }
}

/* --- archivos (MEFS readonly via SYS_FSIZE/SYS_DREAD/SYS_DLIST) ---
 * Fase 9: CreateFileA/ReadFile/FindFirstFileA reales sobre el
 * filesystem. Los handles son enteros 0x100+i: la tabla guarda el
 * nombre del archivo y la posicion de lectura, el kernel lee de RAM
 * (solo lectura). EL archivo se abre con CreateFileA -> pos=0. */

static int sys_fsize(const char *name)
{
    int r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(SYS_FSIZE), "b"(name)
                     : "memory");
    return r;
}

static int sys_dread(const char *name, void *buf, uint32_t off, uint32_t max)
{
    int r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(SYS_DREAD), "b"(name), "c"(buf), "d"(off),
                       "S"(max)
                     : "memory");
    return r;
}

static int sys_dlist(uint32_t idx, char *name, uint32_t *size)
{
    int r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(SYS_DLIST), "b"(idx), "c"(name), "d"(size)
                     : "memory");
    return r;
}

static int sys_fcreate(const char *name)
{
    int r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(SYS_FCREATE), "b"(name)
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

static int sys_fdelete(const char *name)
{
    int r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(SYS_FDELETE), "b"(name)
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

static int sys_mkdir(const char *name, uint32_t parent)
{
    int r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(SYS_MKDIR), "b"(name), "c"(parent)
                     : "memory");
    return r;
}

/* Abre un archivo del FS. Devuelve HANDLE (0x100+slot) o -1. */
void *__attribute__((stdcall)) CreateFileA(const char *name, uint32_t access, uint32_t share,
                  uint32_t sec_attrs, uint32_t creation, uint32_t flags,
                  uint32_t tmpl)
{
    int i, sz, writable;
    (void)share; (void)sec_attrs; (void)flags; (void)tmpl;
    if (name == 0)
        return (void *)INVALID_HANDLE_VALUE;
    trace("[k32] CreateFileA '");
    trace(name);
    trace("'\n");
    /* GENERIC_WRITE = 0x40000000 (Fase E: Guardar). */
    writable = (access & 0x40000000u) != 0;
    sz = sys_fsize(name);
    if (sz < 0) {
        /* no existe: solo se crea si la disposicion lo pide */
        if (!writable || creation == 3 /*OPEN_EXISTING*/)
            return (void *)INVALID_HANDLE_VALUE;
        if (sys_fcreate(name) != 0)
            return (void *)INVALID_HANDLE_VALUE;
        sz = 0;
    }
    for (i = 0; i < 16; i++)
        if (open_files[i].name[0] == 0)
            break;
    if (i == 16)
        return (void *)INVALID_HANDLE_VALUE;
    {
        uint32_t k = 0;
        while (k < 39 && name[k]) {
            open_files[i].name[k] = name[k];
            k++;
        }
        open_files[i].name[k] = 0;
    }
    open_files[i].size = (uint32_t)sz;
    open_files[i].pos  = 0;
    open_files[i].writable = (uint32_t)writable;
    open_files[i].wlen = 0;
    open_files[i].wbuf = (writable) ? wbuf_global : 0;
    /* Fase 23-C9: el CRT de mingw pasa el oflag (_O_TEXT=0x4000) en
     * dwFlagsAndAttributes; default = binario (sin traduccion). */
    open_files[i].mode = (flags & 0x4000u) ? 1u : 0u;
    open_files[i].pend_cr = 0;
    return (void *)(uint32_t)(0x100 + i);
}

/* Lee 'n' bytes desde la posicion del handle. */
uint32_t __attribute__((stdcall)) ReadFile(void *h, void *buf, uint32_t n, uint32_t *read,
                  uint32_t ovl)
{
    int i = (int)(uint32_t)h - 0x100;
    (void)ovl;
    if (i < 0 || i >= 16 || open_files[i].name[0] == 0) {
        if (read) *read = 0;
        return 0;
    }
    if (!open_files[i].mode) {
        /* binario: crudo */
        int r = sys_dread(open_files[i].name, buf, open_files[i].pos, n);
        if (r <= 0) {
            if (read) *read = 0;
            return 0;
        }
        open_files[i].pos += (uint32_t)r;
        if (read) *read = (uint32_t)r;
        return 1;
    }
    /* Fase 23-C9: modo texto: CRLF -> LF (CR suelto se conserva). */
    {
        uint8_t raw[512];
        uint32_t out = 0;
        uint8_t *dst = (uint8_t *)buf;
        while (out < n) {
            int r = sys_dread(open_files[i].name, raw, open_files[i].pos,
                              sizeof(raw));
            if (r <= 0)
                break;
            open_files[i].pos += (uint32_t)r;
            uint32_t k;
            for (k = 0; k < (uint32_t)r && out < n; k++) {
                uint8_t c = raw[k];
                if (open_files[i].pend_cr) {
                    open_files[i].pend_cr = 0;
                    if (c == '\n') {
                        if (out < n)
                            dst[out++] = '\n';      /* CRLF -> \n */
                        continue;
                    }
                    if (out < n)
                        dst[out++] = '\r';          /* CR suelto -> CR */
                    if (c == '\r') {
                        open_files[i].pend_cr = 1;
                        continue;
                    }
                    if (out < n)
                        dst[out++] = c;
                } else if (c == '\r') {
                    open_files[i].pend_cr = 1;      /* ver si sigue \n */
                } else {
                    if (out < n)
                        dst[out++] = c;
                }
            }
        }
        if (open_files[i].pend_cr && out < n) {
            dst[out++] = '\r';                      /* CR al final */
            open_files[i].pend_cr = 0;
        }
        if (read) *read = out;
        return 1;
    }
}

uint32_t __attribute__((stdcall)) GetFileSize(void *h, uint32_t *high)
{
    int i = (uint32_t)h - 0x100;
    if (i < 0 || i >= 16 || open_files[i].name[0] == 0)
        return INVALID_HANDLE_VALUE;
    if (high) *high = 0;
    return open_files[i].size;
}

uint32_t __attribute__((stdcall)) CloseHandle(void *h)
{
    int i = (uint32_t)h - 0x100;
    if (i >= 0 && i < 16) {
        /* Fase E: si se escribio, persistir al FS + flush al disco */
        if (open_files[i].writable && open_files[i].name[0]) {
            if (open_files[i].wlen > 0)
                sys_fwrite(open_files[i].name, open_files[i].wbuf,
                           open_files[i].wlen);
            else if (open_files[i].wlen == 0)
                sys_fwrite(open_files[i].name, (const void *)"", 0);
            sys_flush();
        }
        open_files[i].name[0] = 0;
        open_files[i].writable = 0;
        open_files[i].wlen = 0;
    }
    return 1;
}

/* --- FindFirstFileA / FindNextFileA / FindClose ---
 * Itera el directorio MEFS por indices (SYS_DLIST): el handle guarda
 * el indice actual. Soporta patrones '*' y '?' sobre el nombre. */

typedef struct {
    uint32_t idx;
    char     pat[64];
} find_state_t;

static find_state_t find_st[4];
static int find_st_n = 0;

static int name_match(const char *pat, const char *name)
{
    while (*pat && *name) {
        if (*pat == '*') {
            pat++;
            if (*pat == 0)
                return 1;
            while (*name && !name_match(pat, name))
                name++;
            return name_match(pat, name);
        }
        if (*pat != '?' && *pat != *name)
            return 0;
        pat++;
        name++;
    }
    while (*pat == '*')
        pat++;
    return *pat == 0 && *name == 0;
}

/* Rellena una entrada WIN32_FIND_DATAA (estructura sdk). */
typedef struct {
    uint32_t dwFileAttributes;
    uint32_t ftCreationTime_low, ftCreationTime_high;
    uint32_t ftLastAccess_low, ftLastAccess_high;
    uint32_t ftLastWrite_low, ftLastWrite_high;
    uint32_t nFileSizeHigh, nFileSizeLow;
    uint32_t dwReserved0, dwReserved1;
    char     cFileName[260];
    char     cAlternateFileName[14];
} WIN32_FIND_DATAA;

static int find_next_entry(find_state_t *st, const char *pat,
                           WIN32_FIND_DATAA *fd)
{
    char     name[16];
    uint32_t sz;
    while (sys_dlist(st->idx, name, &sz) == 0) {
        st->idx++;
        if (name_match(pat, name)) {
            uint32_t i;
            for (i = 0; i < sizeof(*fd); i++)
                ((char *)fd)[i] = 0;
            fd->nFileSizeLow  = sz;
            fd->nFileSizeHigh = 0;
            for (i = 0; i < 16 && name[i]; i++)
                fd->cFileName[i] = name[i];
            fd->cFileName[i] = 0;
            return 1;
        }
    }
    return 0;
}

void *__attribute__((stdcall)) FindFirstFileA(const char *pat, WIN32_FIND_DATAA *fd)
{
    find_state_t *st;
    if (find_st_n >= 4)
        return (void *)INVALID_HANDLE_VALUE;
    st = &find_st[find_st_n++];
    st->idx = 0;
    {
        uint32_t k = 0;
        while (k < 63 && pat[k]) {
            st->pat[k] = pat[k];
            k++;
        }
        st->pat[k] = 0;
    }
    if (find_next_entry(st, st->pat, fd))
        return (void *)(uint32_t)(0x200 + (find_st_n - 1));
    find_st_n--;
    return (void *)INVALID_HANDLE_VALUE;
}

int __attribute__((stdcall)) FindNextFileA(void *h, WIN32_FIND_DATAA *fd)
{
    int i = (uint32_t)h - 0x200;
    if (i < 0 || i >= find_st_n)
        return 0;
    return find_next_entry(&find_st[i], find_st[i].pat, fd);
}

uint32_t __attribute__((stdcall)) FindClose(void *h)
{
    int i = (uint32_t)h - 0x200;
    if (i >= 0 && i < find_st_n)
        find_st_n--;
    return 1;
}

void __attribute__((stdcall)) GetCurrentDirectoryA(uint32_t n, char *buf)
{
    trace("[k32] GetCurrentDirectoryA n=");
    trace_hex(n);
    trace("\n");
    copy_str_n(buf, n, "C:\\MyOS");
}

void __attribute__((stdcall)) GetCurrentDirectoryW(uint32_t n, uint16_t *buf)
{
    char a[64];
    GetCurrentDirectoryA(sizeof(a), a);
    cp437_to_utf16(buf, n, a);
}

void __attribute__((stdcall)) SetCurrentDirectoryA(const char *d)
{
    trace("[k32] SetCurrentDirectoryA '");
    if (d)
        trace(d);
    trace("'\n");
}

void __attribute__((stdcall)) SetCurrentDirectoryW(const uint16_t *d)
{
    char a[64];
    utf16_to_cp437(a, d, sizeof(a));
    SetCurrentDirectoryA(a);
}

/* --- Fase 24-P1.4: directorios y version de arranque ---
 * La raiz del MEFS se mapea a "C:\" (paths virtuales fijos). */
static uint32_t copy_str_n(char *dst, uint32_t n, const char *src)
{
    uint32_t i, len = (uint32_t)strlen_u(src);
    if (n == 0)
        return len;
    for (i = 0; i + 1 < n && src[i]; i++)
        dst[i] = src[i];
    dst[i] = 0;
    return len;
}

uint32_t __attribute__((stdcall)) GetTempPathA(uint32_t n, char *buf)
{
    trace("[k32] GetTempPathA\n");
    return copy_str_n(buf, n, "C:\\TEMP");
}

uint32_t __attribute__((stdcall)) GetWindowsDirectoryA(char *buf, uint32_t n)
{
    trace("[k32] GetWindowsDirectoryA\n");
    return copy_str_n(buf, n, "C:\\WINDOWS");
}

uint32_t __attribute__((stdcall)) GetSystemDirectoryA(char *buf, uint32_t n)
{
    trace("[k32] GetSystemDirectoryA\n");
    return copy_str_n(buf, n, "C:\\WINDOWS\\System32");
}

/* OSVERSIONINFOA: version 6.1 (Win7) para no romper apps que la
 * comprueban. */
typedef struct {
    uint32_t dwOSVersionInfoSize;
    uint32_t dwMajorVersion;
    uint32_t dwMinorVersion;
    uint32_t dwBuildNumber;
    uint32_t dwPlatformId;
    char     szCSDVersion[128];
} my_OSVERSIONINFOA;

uint32_t __attribute__((stdcall)) GetVersionExA(my_OSVERSIONINFOA *vi)
{
    trace("[k32] GetVersionExA\n");
    if (vi == 0)
        return 0;
    vi->dwOSVersionInfoSize = sizeof(my_OSVERSIONINFOA);
    vi->dwMajorVersion = 6;
    vi->dwMinorVersion = 1;
    vi->dwBuildNumber  = 7601;
    vi->dwPlatformId   = 2;     /* VER_PLATFORM_WIN32_NT */
    vi->szCSDVersion[0] = 0;
    return 1;
}

uint32_t __attribute__((stdcall)) GetVersion(void)
{
    return (6 << 16) | 1;       /* major << 16 | minor */
}

/* BY_HANDLE_FILE_INFORMATION (Windows): 5 dword + 3 FILETIME (8 c/u). */
typedef struct {
    uint32_t dwFileAttributes;
    uint32_t ftCreationTime[2];
    uint32_t ftLastAccessTime[2];
    uint32_t ftLastWriteTime[2];
    uint32_t dwVolumeSerialNumber;
    uint32_t nFileSizeHigh;
    uint32_t nFileSizeLow;
    uint32_t nNumberOfLinks;
    uint32_t nFileIndexHigh;
    uint32_t nFileIndexLow;
} my_BY_HANDLE_FILE_INFORMATION;

uint32_t __attribute__((stdcall)) GetFileInformationByHandle(uint32_t h,
                          my_BY_HANDLE_FILE_INFORMATION *info)
{
    struct win32_file_s *f = win32_file_of(h);
    trace("[k32] GetFileInformationByHandle\n");
    if (f == 0 || info == 0)
        return 0;
    info->dwFileAttributes   = 0x80;      /* FILE_ATTRIBUTE_NORMAL */
    info->ftCreationTime[0]  = 0;
    info->ftCreationTime[1]  = 0;
    info->ftLastAccessTime[0] = 0;
    info->ftLastAccessTime[1] = 0;
    info->ftLastWriteTime[0] = 0;
    info->ftLastWriteTime[1] = 0;
    info->dwVolumeSerialNumber = 0x4D594F53u;   /* "MYOS" */
    info->nFileSizeHigh      = 0;
    info->nFileSizeLow       = f->size;
    info->nNumberOfLinks     = 1;
    info->nFileIndexHigh     = 0;
    info->nFileIndexLow      = (uint32_t)(h - 0x100);
    return 1;
}

/* --- Global*: memoria global = el heap del proceso (no movible) --- */

void *__attribute__((stdcall)) GlobalAlloc(uint32_t flags, uint32_t size)
{
    (void)flags;
    if (size == 0)
        return 0;
    return win_malloc(size);
}

void *__attribute__((stdcall)) GlobalLock(void *h)
{
    return h;                   /* GMEM_FIXED: el handle ES el puntero */
}

uint32_t __attribute__((stdcall)) GlobalUnlock(void *h)
{
    (void)h;
    return 1;
}

void *__attribute__((stdcall)) GlobalFree(void *h)
{
    win_free(h);
    return 0;                   /* NULL = exito */
}

/* LocalFree/LocalAlloc: el heap del proceso. */
void *__attribute__((stdcall)) LocalFree(void *h)
{
    win_free(h);
    return 0;
}

/* --- lstr*: strings ANSI --- */

uint32_t __attribute__((stdcall)) lstrlenA(const char *s)
{
    trace("[k32] lstrlenA '");
    if (s) trace(s);
    trace("'\n");
    return strlen_u(s);
}

char *__attribute__((stdcall)) lstrcpyA(char *dst, const char *src)
{
    uint32_t i = 0;
    do { dst[i] = src[i]; } while (src[i++]);
    return dst;
}

char *__attribute__((stdcall)) lstrcpynA(char *dst, const char *src, uint32_t n)
{
    uint32_t i;
    if (n == 0)
        return dst;
    for (i = 0; i < n - 1 && src[i]; i++)
        dst[i] = src[i];
    dst[i] = 0;
    return dst;
}

char *__attribute__((stdcall)) lstrcatA(char *dst, const char *src)
{
    uint32_t d = 0, i = 0;
    while (dst[d]) d++;
    while (src[i]) dst[d++] = src[i++];
    dst[d] = 0;
    return dst;
}

int32_t __attribute__((stdcall)) lstrcmpA(const char *a, const char *b)
{
    while (*a && *b && *a == *b) { a++; b++; }
    return (int32_t)(uint8_t)*a - (int32_t)(uint8_t)*b;
}

int32_t __attribute__((stdcall)) lstrcmpiA(const char *a, const char *b)
{
    for (;;) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb)
            return (int32_t)(uint8_t)ca - (int32_t)(uint8_t)cb;
        if (ca == 0)
            return 0;
        a++; b++;
    }
}

/* --- archivo: atributos / puntero / fin --- */

#define FILE_ATTRIBUTE_NORMAL 0x80
#define FILE_BEGIN 0
#define FILE_CURRENT 1
#define FILE_END 2

uint32_t __attribute__((stdcall)) GetFileAttributesA(const char *name)
{
    if (name == 0)
        return 0xFFFFFFFFu;
    {
        long sz = sys_fsize(name);
        trace("[k32] GetFileAttributesA '");
        trace(name);
        trace("' fsize=");
        if (sz < 0)
            trace("<none>\n");
        else
            trace("<ok>\n");
    }
    if (sys_fsize(name) < 0)
        return 0xFFFFFFFFu;
    return FILE_ATTRIBUTE_NORMAL;
}

uint32_t __attribute__((stdcall)) SetFileAttributesA(const char *name, uint32_t attrs)
{
    (void)name; (void)attrs;
    return 1;
}

uint32_t __attribute__((stdcall)) SetFilePointer(void *h, int32_t dist_low, int32_t *dist_high,
                        uint32_t method)
{
    int i = (uint32_t)h - 0x100;
    uint32_t base = 0;
    int32_t d = dist_low;
    (void)dist_high;
    if (i < 0 || i >= 16 || open_files[i].name[0] == 0)
        return 0xFFFFFFFFu;
    switch (method) {
    case FILE_BEGIN:    base = 0; break;
    case FILE_CURRENT:  base = open_files[i].pos; break;
    case FILE_END:      base = open_files[i].size; break;
    default: return 0xFFFFFFFFu;
    }
    if (d < 0 && base < (uint32_t)(-d))
        d = 0;
    open_files[i].pos = base + (uint32_t)d;
    if (open_files[i].pos > open_files[i].size)
        open_files[i].pos = open_files[i].size;
    return open_files[i].pos;
}

uint32_t __attribute__((stdcall)) SetEndOfFile(void *h)
{
    int i = (uint32_t)h - 0x100;
    if (i >= 0 && i < 16 && open_files[i].writable && open_files[i].name[0]) {
        sys_fwrite(open_files[i].name, open_files[i].wbuf,
                   open_files[i].wlen);
        sys_flush();
    }
    return 1;
}

uint32_t __attribute__((stdcall)) GetFullPathNameA(const char *name, uint32_t n, char *buf,
                          char **filepart)
{
    uint32_t len = 0, last = 0, i;
    trace("[k32] GetFullPathNameA '");
    if (name)
        trace(name);
    trace("' n=");
    trace_hex(n);
    trace("\n");
    if (name == 0 || buf == 0 || n == 0)
        return 0;
    for (i = 0; name[i]; i++) {
        buf[i] = name[i];
        if (name[i] == '\\' || name[i] == '/')
            last = i + 1;
    }
    len = i;
    if (len >= n)
        return 0;
    buf[len] = 0;
    if (filepart)
        *filepart = buf + last;
    return len;
}

/* --- hilos: no hay planificador Win32; se ejecuta el cuerpo del hilo
 * de forma sincrona en la pila actual (valido para hilos de un solo
 * uso, p.ej. el worker de apertura de archivos de metapad 0x40c64e,
 * que termina con ret sin bucles). --- */

typedef void (*thread_fn)(void *);

void *__attribute__((stdcall)) CreateThread(uint32_t attrs, uint32_t stack_size, void *start,
                   void *param, uint32_t flags, uint32_t *tid)
{
    (void)attrs; (void)stack_size; (void)flags;
    trace("[k32] CreateThread start=");
    trace_hex((uint32_t)start);
    trace("\n");
    if (start == 0)
        return 0;
    {
        int r;
        uint32_t tidv = 0;
        __asm__ volatile("int $0x80"
                         : "=a"(r)
                         : "a"(SYS_THREADCREATE), "b"(start), "c"(param),
                           "d"(&tidv)
                         : "memory");
        if (tid)
            *tid = tidv;
        if (r < 0)
            return 0;
        return (void *)(uint32_t)(r + 1);   /* HANDLE del hilo */
    }
}

/* Fase 24-P2.2: termina el hilo actual (llamada por ExitThread y como
 * retorno de la funcion del hilo via el trampolin _thread_ret). */
uint32_t __attribute__((stdcall)) ExitThread(uint32_t code)
{
    trace("[k32] ExitThread\n");
    (void)code;
    __asm__ volatile("int $0x80" : : "a"(SYS_THREADEXIT), "b"(code)
                     : "memory");
    return 0;                   /* no se llega: SYS_THREADEXIT no retorna */
}

/* Retorno de la funcion del hilo: si el hilo no llama ExitThread y su fn
 * retorna, vuelve aqui y se termina. Direccion usada por el kernel como
 * return address en la pila del hilo (win32_resolve kernel32._thread_ret). */
void __attribute__((stdcall)) _thread_ret(void)
{
    ExitThread(0);
}

/* --- procesos: Metapad abre "una segunda copia" con CreateProcessA;
 * sin multitarea Win32 devolvemos FALSE y el llamador muestra error o
 * no hace nada. --- */

uint32_t __attribute__((stdcall)) CreateProcessA(const char *app, char *cmd, uint32_t p1,
                        uint32_t p2, uint32_t inherit, uint32_t flags,
                        void *env, const char *cwd, const void *si,
                        void *pi)
{
    (void)app; (void)cmd; (void)p1; (void)p2; (void)inherit; (void)flags;
    (void)env; (void)cwd; (void)si; (void)pi;
    return 0;
}

/* --- aritmetica: MulDiv sin int64 (los modulos no ligan libgcc).
 * Valores tipicos (puntos por linea de impresion): |a*b| << 2^31. --- */

int32_t __attribute__((stdcall)) MulDiv(int32_t a, int32_t b, int32_t c)
{
    int32_t p, rem, r;
    if (c == 0)
        return -1;
    if (b == 0 || a == 0)
        return 0;
    if (a > 0x7FFFFFFF / b || a < -0x7FFFFFFF / b)
        return -1;              /* |a*b| desborda */
    p = a * b;
    if (c < 0) {
        c = -c;
        p = -p;
    }
    r = p / c;
    rem = p % c;
    if (rem * 2 >= c)           /* redondeo al entero mas cercano */
        r++;
    return r;
}

/* --- config INI (GetPrivateProfileStringA sobre MEFS readonly).
 * Busca [seccion] key=valor en el archivo; si no existe el archivo, la
 * seccion o la key, copia el default (def). --- */

static void ini_read_file(const char *file, char **buf, uint32_t *len)
{
    int32_t sz = sys_fsize(file);
    if (sz <= 0) {
        *buf = 0;
        *len = 0;
        return;
    }
    *buf = (char *)win_malloc((uint32_t)sz + 1);
    if (*buf == 0) {
        *len = 0;
        return;
    }
    if (sys_dread(file, *buf, 0, (uint32_t)sz) != sz) {
        win_free(*buf);
        *buf = 0;
        *len = 0;
        return;
    }
    (*buf)[sz] = 0;
    *len = (uint32_t)sz;
}

static int ini_ci_eq(const char *a, const char *b)
{
    for (;;) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb)
            return 0;
        if (ca == 0)
            return 1;
        a++; b++;
    }
}

static int ini_ci_eq_len(const char *a, const char *b, uint32_t bl)
{
    uint32_t i = 0;
    for (;;) {
        char ca = *a, cb;
        if (i >= bl)
            cb = 0;
        else
            cb = b[i];
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb)
            return 0;
        if (ca == 0)
            return 1;
        a++; i++;
    }
}

static uint32_t ini_find_value(const char *data, const char *sec,
                               const char *key, char *out, uint32_t size)
{
    const char *p = data;
    int in_sec = 0;

    while (*p) {
        const char *eol = p;
        while (*eol && *eol != '\n')
            eol++;
        if (*p == '[') {
            const char *s = p + 1, *end = eol;
            while (end > s && (end[-1] == ']' || end[-1] == '\r'
                   || end[-1] == ' ' || end[-1] == '\t'))
                end--;
            {
                char secbuf[64];
                uint32_t i = 0;
                while (s < end && i < 63)
                    secbuf[i++] = *s++;
                secbuf[i] = 0;
                in_sec = ini_ci_eq(secbuf, sec);
            }
        } else if (in_sec && *p != ';' && *p != '#' && *p != '\r') {
            const char *eq = p;
            while (eq < eol && *eq != '=')
                eq++;
            if (eq < eol) {
                uint32_t kl = (uint32_t)(eq - p);
                if (ini_ci_eq_len(key, p, kl)) {
                    const char *v = eq + 1;
                    const char *vend = eol;
                    uint32_t n = 0;
                    while (vend > v && (vend[-1] == '\r'
                           || vend[-1] == ' ' || vend[-1] == '\t'))
                        vend--;
                    while (v < vend && n < size - 1)
                        out[n++] = *v++;
                    out[n] = 0;
                    return n;
                }
            }
        }
        p = (*eol) ? eol + 1 : eol;
    }
    return 0;
}

static void str_cpy_default(const char *def, char *out, uint32_t size)
{
    uint32_t i = 0;
    if (def == 0)
        def = "";
    while (def[i] && i < size - 1) {
        out[i] = def[i];
        i++;
    }
    out[i] = 0;
}

uint32_t __attribute__((stdcall)) GetPrivateProfileStringA(const char *sec, const char *key,
                                  const char *def, char *out,
                                  uint32_t size, const char *file)
{
    char *data;
    uint32_t len;
    if (out == 0 || size == 0 || file == 0)
        return 0;
    if (sec == 0 || key == 0) {
        str_cpy_default(def, out, size);
        return strlen_u(out);
    }
    ini_read_file(file, &data, &len);
    if (data == 0) {
        str_cpy_default(def, out, size);
        return strlen_u(out);
    }
    if (!ini_find_value(data, sec, key, out, size))
        str_cpy_default(def, out, size);
    win_free(data);
    return strlen_u(out);
}

/* WritePrivateProfileStringA: FS readonly, finge guardar. */
uint32_t __attribute__((stdcall)) WritePrivateProfileStringA(const char *sec, const char *key,
                                    const char *value, const char *file)
{
    (void)sec; (void)key; (void)value; (void)file;
    return 1;
}

/* --- fecha/hora: MyOS no tiene reloj; fechas fijas estables.
 * GetDateFormatA/GetTimeFormatA formatean "sabado, enero 01, 2000" /
 * "9:00 AM" segun el patron (tokens Win32). --- */

static const char *g_months_long[] =
    { "January", "February", "March", "April", "May", "June", "July",
      "August", "September", "October", "November", "December" };
static const char *g_days_long[] =
    { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday",
      "Saturday" };
static const char *g_months_abbr[] =
    { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep",
      "Oct", "Nov", "Dec" };
static const char *g_days_abbr[] =
    { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };

/* Fecha fija: sabado 1 de enero de 2000, 9:00. */
#define FIXED_YEAR  2000
#define FIXED_MONTH 1
#define FIXED_DAY   1
#define FIXED_WDAY  6           /* sabado */
#define FIXED_HOUR  9
#define FIXED_MIN   0
#define FIXED_SEC   0

static void fmt_num(char *out, uint32_t *i, int n)
{
    char tmp[8];
    int t = 0;
    while (n) { tmp[t++] = (char)('0' + n % 10); n /= 10; }
    if (t == 0) tmp[t++] = '0';
    while (t) out[(*i)++] = tmp[--t];
}

static uint32_t fmt_tokens(const char *fmt, char *out, uint32_t size,
                           int date)
{
    uint32_t o = 0;
    const char *p = fmt;

    while (*p && o < size - 1) {
        char c = *p;
        uint32_t run = 1;
        while (p[run] == c)
            run++;
        if (date) {
            if (c == 'y') {
                if (run >= 3) {
                    fmt_num(out, &o, FIXED_YEAR);
                } else {
                    out[o++] = '0'; out[o++] = '0';
                }
                p += run;
                continue;
            }
            if (c == 'M') {
                if (run >= 4) {
                    const char *s = g_months_long[FIXED_MONTH - 1];
                    while (*s && o < size - 1) out[o++] = *s++;
                } else if (run == 3) {
                    const char *s = g_months_abbr[FIXED_MONTH - 1];
                    while (*s && o < size - 1) out[o++] = *s++;
                } else {
                    fmt_num(out, &o, FIXED_MONTH);
                }
                p += run;
                continue;
            }
            if (c == 'd') {
                if (run >= 4) {
                    const char *s = g_days_long[FIXED_WDAY];
                    while (*s && o < size - 1) out[o++] = *s++;
                } else if (run == 3) {
                    const char *s = g_days_abbr[FIXED_WDAY];
                    while (*s && o < size - 1) out[o++] = *s++;
                } else {
                    fmt_num(out, &o, FIXED_DAY);
                }
                p += run;
                continue;
            }
        } else {
            if (c == 'h' || c == 'H') {
                int h = FIXED_HOUR;
                if (run == 1) {
                    if (h > 12) h -= 12;
                    if (h == 0) h = 12;
                } else if (run == 2) {
                    if (h > 12) h -= 12;
                    if (h == 0) h = 12;
                }
                if (run >= 2 && h < 10)
                    out[o++] = '0';
                fmt_num(out, &o, h);
                p += run;
                continue;
            }
            if (c == 'm' || c == 's') {
                int v = (c == 'm') ? FIXED_MIN : FIXED_SEC;
                if (run >= 2 && v < 10)
                    out[o++] = '0';
                fmt_num(out, &o, v);
                p += run;
                continue;
            }
            if (c == 't' || c == 'T') {
                const char *ampm = "AM";
                const char *ampm2 = "am";
                if (FIXED_HOUR >= 12) { ampm = "PM"; ampm2 = "pm"; }
                if (run >= 2) {
                    const char *s = ampm;
                    while (*s && o < size - 1) out[o++] = *s++;
                } else {
                    out[o++] = ampm2[0];
                }
                p += run;
                continue;
            }
        }
        out[o++] = c;
        p++;
    }
    out[o] = 0;
    return o;
}

uint32_t __attribute__((stdcall)) GetDateFormatA(uint32_t locale, uint32_t flags, const void *st,
                        const char *fmt, char *buf, uint32_t size)
{
    const char *def = "M/d/yyyy";
    (void)locale; (void)flags; (void)st;
    if (buf == 0 || size == 0)
        return 0;
    return fmt_tokens(fmt ? fmt : def, buf, size, 1);
}

uint32_t __attribute__((stdcall)) GetTimeFormatA(uint32_t locale, uint32_t flags, const void *st,
                        const char *fmt, char *buf, uint32_t size)
{
    const char *def = "h:mm tt";
    (void)locale; (void)flags; (void)st;
    if (buf == 0 || size == 0)
        return 0;
    return fmt_tokens(fmt ? fmt : def, buf, size, 0);
}

/* GetLocaleInfoA: valores de ingles (EE. UU.), idioma 9 (en-US). */
uint32_t __attribute__((stdcall)) GetLocaleInfoA(uint32_t locale, uint32_t type, char *buf,
                        uint32_t size)
{
    const char *v = "";
    (void)locale;
    switch (type) {
    case 0x01: v = "9"; break;                      /* ILANGUAGE */
    case 0x02: v = "English (United States)"; break; /* SLANGUAGE */
    case 0x06: v = "United States"; break;          /* SCOUNTRY */
    case 0x0E: v = "."; break;                      /* SDECIMAL */
    case 0x1D: v = "/"; break;                      /* SDATE */
    case 0x1E: v = ":"; break;                      /* STIME */
    case 0x1F: v = "M/d/yyyy"; break;               /* SSHORTDATE */
    case 0x20: v = "dddd, MMMM dd, yyyy"; break;    /* SLONGDATE */
    case 0x23: v = "0"; break;                      /* ITIME (12 h) */
    case 0x24: v = "0"; break;                      /* ITLZERO */
    case 0x25: v = "1"; break;                      /* ICENTURY */
    case 0x1001: v = "English"; break;              /* SENGLANGUAGE */
    case 0x1003: v = "h:mm:ss tt"; break;           /* STIMEFORMAT */
    case 0x1004: v = "1252"; break;                 /* IANSI codepage */
    default:
        if (type >= 0x30 && type <= 0x36) {         /* SDAYNAME1..7 */
            v = g_days_long[type - 0x30];
        } else if (type >= 0x38 && type <= 0x43) {  /* SMONTHNAME1..12 */
            v = g_months_long[type - 0x38];
        } else if (type >= 0x47 && type <= 0x4D) {  /* SABBREVDAYNAME1..7 */
            v = g_days_abbr[type - 0x47];
        } else if (type >= 0x4F && type <= 0x5A) {  /* SABBREVMONTHNAME1..12 */
            v = g_months_abbr[type - 0x4F];
        }
        break;
    }
    if (buf == 0 || size == 0)
        return 0;
    {
        uint32_t i = 0;
        while (v[i] && i < size - 1) {
            buf[i] = v[i];
            i++;
        }
        buf[i] = 0;
        return i;
    }
}

/* FormatMessageA: solo FROM_STRING (0x400) y FROM_SYSTEM (0x1000) con
 * una tabla de mensajes fija. Sustituye %1..%9 por args[]. */
uint32_t __attribute__((stdcall)) FormatMessageA(uint32_t flags, const void *src, uint32_t msgid,
                        uint32_t lang, char *buf, uint32_t size,
                        uint32_t *args)
{
    static const char *sys_msgs[] = {
        "No such file or directory",
        "Access is denied",
        "Not enough memory resources are available to process this command",
        "Data error (cyclic redundancy check)",
        "The process cannot access the file because it is being used by another process",
        "The file exists",
        "The parameter is incorrect",
        "The filename, directory name, or volume label syntax is incorrect",
        "Cannot create a file when that file already exists",
    };
    const char *fmt = 0;
    uint32_t o = 0, argi = 0;
    (void)lang;
    if (buf == 0 || size == 0)
        return 0;
    if (flags & 0x400) {                    /* FROM_STRING */
        fmt = (const char *)src;
    } else if (flags & 0x1000) {            /* FROM_SYSTEM */
        switch (msgid) {
        case 2:   fmt = sys_msgs[0]; break;
        case 5:   fmt = sys_msgs[1]; break;
        case 8:   fmt = sys_msgs[2]; break;
        case 13:  fmt = sys_msgs[3]; break;
        case 32:  fmt = sys_msgs[4]; break;
        case 80:  fmt = sys_msgs[5]; break;
        case 87:  fmt = sys_msgs[6]; break;
        case 123: fmt = sys_msgs[7]; break;
        case 183: fmt = sys_msgs[8]; break;
        default:  return 0;
        }
    } else {
        return 0;
    }
    while (fmt && fmt[argi] && o < size - 1) {
        char c = fmt[argi];
        if (c == '%') {
            char n = fmt[argi + 1];
            if (n == '%') {
                buf[o++] = '%';
                argi += 2;
                continue;
            }
            if (n >= '0' && n <= '9') {
                uint32_t idx = (uint32_t)(n - '0');
                const char *v = (args && idx > 0)
                                ? (const char *)args[idx - 1] : "";
                if (idx == 0)
                    v = (const char *)args[0];
                while (*v && o < size - 1)
                    buf[o++] = *v++;
                argi += 2;
                continue;
            }
            if (n == 0)
                break;
        }
        buf[o++] = c;
        argi++;
    }
    buf[o] = 0;
    return o;
}

/* ==================================================================
 * Fase 25 (W2A): thunks W->A de kernel32 + funciones A complementarias
 * (msvcrt llama GetEnvironmentVariableA al arrancar). Direccion del
 * thunk: entrada de cadena = utf16_to_cp437 -> A; salida = A ->
 * cp437_to_utf16. Las lstr*W operan directo sobre UTF-16.
 * ================================================================== */

/* --- A complementarias que no existian --- */

static const char *env_find(const char *name)
{
    uint32_t i, n = strlen_u(name);
    for (i = 0; env_block[i]; i++) {
        const char *e = env_block[i];
        uint32_t k;
        for (k = 0; k < n && e[k] && e[k] != '='; k++)
            if (e[k] != name[k])
                break;
        if (k == n && e[k] == '=')
            return e + k + 1;
    }
    return 0;
}

uint32_t __attribute__((stdcall)) GetEnvironmentVariableA(const char *name, char *buf,
                          uint32_t size)
{
    const char *v;
    trace("[k32] GetEnvironmentVariableA '");
    if (name)
        trace(name);
    trace("'\n");
    v = (name == 0) ? 0 : env_find(name);
    if (v == 0)
        return 0;
    if (buf == 0 || size == 0)
        return strlen_u(v);
    return copy_str_n(buf, size, v);
}

uint32_t __attribute__((stdcall)) SetEnvironmentVariableA(const char *name, const char *value)
{
    (void)name; (void)value;
    return 1;
}

uint32_t __attribute__((stdcall)) DeleteFileA(const char *name)
{
    int r;
    if (name == 0)
        return 0;
    r = sys_fdelete(name);
    return (uint32_t)(r == 0);
}

uint32_t __attribute__((stdcall)) CopyFileA(const char *src, const char *dst,
                        uint32_t fail_if_exists)
{
    uint32_t n;
    int sz;
    char *buf;
    (void)fail_if_exists;
    if (src == 0 || dst == 0)
        return 0;
    sz = sys_fsize(src);
    if (sz < 0)
        return 0;
    buf = (char *)win_malloc((uint32_t)sz + 1);
    if (buf == 0)
        return 0;
    n = sys_dread(src, buf, 0, (uint32_t)sz);
    if (n != (uint32_t)sz) {
        win_free(buf);
        return 0;
    }
    buf[sz] = 0;
    if (sys_fcreate(dst) != 0 || sys_fwrite(dst, buf, (uint32_t)sz) != 0) {
        win_free(buf);
        return 0;
    }
    sys_flush();
    win_free(buf);
    return 1;
}

uint32_t __attribute__((stdcall)) MoveFileA(const char *src, const char *dst)
{
    if (CopyFileA(src, dst, 0) == 0)
        return 0;
    sys_fdelete(src);
    sys_flush();
    return 1;
}

uint32_t __attribute__((stdcall)) CreateDirectoryA(const char *name, const void *sa)
{
    int r;
    (void)sa;
    if (name == 0)
        return 0;
    r = sys_mkdir(name, 0);     /* parent = raiz del MEFS */
    return (uint32_t)(r == 0);
}

uint32_t __attribute__((stdcall)) RemoveDirectoryA(const char *name)
{
    int r;
    if (name == 0)
        return 0;
    r = sys_fdelete(name);      /* mefs_delete vacia dirs sin hijos */
    return (uint32_t)(r == 0);
}

uint32_t __attribute__((stdcall)) GetLogicalDriveStringsA(uint32_t n, char *buf)
{
    /* "C:\" + doble NUL */
    if (buf == 0 || n == 0)
        return 4;
    if (n >= 4) {
        buf[0] = 'C'; buf[1] = ':'; buf[2] = '\\'; buf[3] = 0;
        return 4;
    }
    return 4;
}

/* WIN32_FIND_DATAW: mismo layout que WIN32_FIND_DATAA pero con
 * cFileName/cAlternateFileName en UTF-16. */
typedef struct {
    uint32_t dwFileAttributes;
    uint32_t ftCreationTime_low, ftCreationTime_high;
    uint32_t ftLastAccess_low, ftLastAccess_high;
    uint32_t ftLastWrite_low, ftLastWrite_high;
    uint32_t nFileSizeHigh, nFileSizeLow;
    uint32_t dwReserved0, dwReserved1;
    uint16_t cFileName[260];
    uint16_t cAlternateFileName[14];
} WIN32_FIND_DATAW;

static int find_next_entry_w(find_state_t *st, const char *pat,
                             WIN32_FIND_DATAW *fd)
{
    WIN32_FIND_DATAA a;
    uint32_t i;
    if (find_next_entry(st, pat, &a) == 0)
        return 0;
    for (i = 0; i < sizeof(*fd) / sizeof(uint16_t); i++)
        ((uint16_t *)fd)[i] = 0;
    fd->dwFileAttributes = a.dwFileAttributes;
    fd->nFileSizeHigh    = a.nFileSizeHigh;
    fd->nFileSizeLow     = a.nFileSizeLow;
    cp437_to_utf16(fd->cFileName, 260, a.cFileName);
    return 1;
}

void *__attribute__((stdcall)) FindFirstFileW(const uint16_t *pat, WIN32_FIND_DATAW *fd)
{
    char p[64];
    find_state_t *st;
    if (find_st_n >= 4)
        return (void *)INVALID_HANDLE_VALUE;
    utf16_to_cp437(p, pat, sizeof(p));
    st = &find_st[find_st_n++];
    st->idx = 0;
    {
        uint32_t k = 0;
        while (k < 63 && p[k]) {
            st->pat[k] = p[k];
            k++;
        }
        st->pat[k] = 0;
    }
    if (find_next_entry_w(st, st->pat, fd))
        return (void *)(uint32_t)(0x200 + (find_st_n - 1));
    find_st_n--;
    return (void *)INVALID_HANDLE_VALUE;
}

int __attribute__((stdcall)) FindNextFileW(void *h, WIN32_FIND_DATAW *fd)
{
    int i = (uint32_t)h - 0x200;
    if (i < 0 || i >= find_st_n)
        return 0;
    return find_next_entry_w(&find_st[i], find_st[i].pat, fd);
}

/* --- thunks W->A de cadenas de entrada --- */

uint32_t __attribute__((stdcall)) CreateFileW(const uint16_t *name, uint32_t access,
                       uint32_t share, uint32_t sec_attrs,
                       uint32_t creation, uint32_t flags, uint32_t tmpl)
{
    char a[64];
    if (name == 0)
        return (uint32_t)INVALID_HANDLE_VALUE;
    utf16_to_cp437(a, name, sizeof(a));
    return (uint32_t)CreateFileA(a, access, share, sec_attrs, creation,
                                 flags, tmpl);
}

uint32_t __attribute__((stdcall)) GetFileAttributesW(const uint16_t *name)
{
    char a[64];
    if (name == 0)
        return 0xFFFFFFFFu;
    utf16_to_cp437(a, name, sizeof(a));
    return GetFileAttributesA(a);
}

uint32_t __attribute__((stdcall)) SetFileAttributesW(const uint16_t *name, uint32_t attrs)
{
    char a[64];
    if (name == 0)
        return 0;
    utf16_to_cp437(a, name, sizeof(a));
    return SetFileAttributesA(a, attrs);
}

uint32_t __attribute__((stdcall)) DeleteFileW(const uint16_t *name)
{
    char a[64];
    if (name == 0)
        return 0;
    utf16_to_cp437(a, name, sizeof(a));
    return DeleteFileA(a);
}

uint32_t __attribute__((stdcall)) MoveFileW(const uint16_t *src, const uint16_t *dst)
{
    char s[64], d[64];
    if (src == 0 || dst == 0)
        return 0;
    utf16_to_cp437(s, src, sizeof(s));
    utf16_to_cp437(d, dst, sizeof(d));
    return MoveFileA(s, d);
}

uint32_t __attribute__((stdcall)) CopyFileW(const uint16_t *src, const uint16_t *dst,
                    uint32_t fail_if_exists)
{
    char s[64], d[64];
    if (src == 0 || dst == 0)
        return 0;
    utf16_to_cp437(s, src, sizeof(s));
    utf16_to_cp437(d, dst, sizeof(d));
    return CopyFileA(s, d, fail_if_exists);
}

uint32_t __attribute__((stdcall)) CreateDirectoryW(const uint16_t *name, const void *sa)
{
    char a[64];
    if (name == 0)
        return 0;
    utf16_to_cp437(a, name, sizeof(a));
    return CreateDirectoryA(a, sa);
}

uint32_t __attribute__((stdcall)) RemoveDirectoryW(const uint16_t *name)
{
    char a[64];
    if (name == 0)
        return 0;
    utf16_to_cp437(a, name, sizeof(a));
    return RemoveDirectoryA(a);
}

uint32_t __attribute__((stdcall)) GetEnvironmentVariableW(const uint16_t *name, uint16_t *buf,
                          uint32_t size)
{
    char a[64], v[128];
    uint32_t n;
    if (name == 0)
        return 0;
    utf16_to_cp437(a, name, sizeof(a));
    n = GetEnvironmentVariableA(a, v, sizeof(v));
    if (n == 0)
        return 0;
    if (buf == 0 || size == 0)
        return n;
    return cp437_to_utf16(buf, size, v);
}

uint32_t __attribute__((stdcall)) SetEnvironmentVariableW(const uint16_t *name,
                          const uint16_t *value)
{
    char a[64], v[64];
    if (name == 0)
        return 0;
    utf16_to_cp437(a, name, sizeof(a));
    if (value != 0)
        utf16_to_cp437(v, value, sizeof(v));
    return SetEnvironmentVariableA(a, (value == 0) ? 0 : v);
}

uint32_t __attribute__((stdcall)) GetTempPathW(uint32_t n, uint16_t *buf)
{
    char a[64];
    uint32_t r = GetTempPathA(sizeof(a), a);
    if (buf == 0 || n == 0)
        return r;
    cp437_to_utf16(buf, n, a);
    return r;
}

uint32_t __attribute__((stdcall)) GetWindowsDirectoryW(uint16_t *buf, uint32_t n)
{
    char a[64];
    uint32_t r = GetWindowsDirectoryA(a, sizeof(a));
    if (buf == 0 || n == 0)
        return r;
    cp437_to_utf16(buf, n, a);
    return r;
}

uint32_t __attribute__((stdcall)) GetSystemDirectoryW(uint16_t *buf, uint32_t n)
{
    char a[64];
    uint32_t r = GetSystemDirectoryA(a, sizeof(a));
    if (buf == 0 || n == 0)
        return r;
    cp437_to_utf16(buf, n, a);
    return r;
}

uint32_t __attribute__((stdcall)) GetFullPathNameW(const uint16_t *name, uint32_t n,
                    uint16_t *buf, uint16_t **filepart)
{
    char a[128], out[128];
    uint32_t r, i;
    if (name == 0 || buf == 0 || n == 0)
        return 0;
    utf16_to_cp437(a, name, sizeof(a));
    r = GetFullPathNameA(a, sizeof(out), out, 0);
    if (r == 0)
        return 0;
    if (n <= r)
        return 0;
    cp437_to_utf16(buf, n, out);
    if (filepart) {
        uint32_t last = 0;
        for (i = 0; out[i]; i++)
            if (out[i] == '\\' || out[i] == '/')
                last = i + 1;
        *filepart = buf + last;
    }
    return r;
}

/* --- lstr*W: operan directo sobre UTF-16 --- */

uint32_t __attribute__((stdcall)) lstrlenW(const uint16_t *s)
{
    if (s == 0)
        return 0;
    return wstrlen(s);
}

uint16_t *__attribute__((stdcall)) lstrcpyW(uint16_t *dst, const uint16_t *src)
{
    uint32_t i = 0;
    if (dst == 0 || src == 0)
        return dst;
    do { dst[i] = src[i]; } while (src[i++]);
    return dst;
}

uint16_t *__attribute__((stdcall)) lstrcpynW(uint16_t *dst, const uint16_t *src, uint32_t n)
{
    uint32_t i;
    if (dst == 0)
        return dst;
    if (n == 0)
        return dst;
    for (i = 0; i < n - 1 && src[i]; i++)
        dst[i] = src[i];
    dst[i] = 0;
    return dst;
}

uint16_t *__attribute__((stdcall)) lstrcatW(uint16_t *dst, const uint16_t *src)
{
    uint32_t d = 0, i = 0;
    if (dst == 0)
        return dst;
    while (dst[d]) d++;
    if (src == 0)
        return dst;
    while (src[i]) dst[d++] = src[i++];
    dst[d] = 0;
    return dst;
}

int32_t __attribute__((stdcall)) lstrcmpW(const uint16_t *a, const uint16_t *b)
{
    if (a == 0) a = (const uint16_t *)L"";
    if (b == 0) b = (const uint16_t *)L"";
    while (*a && *b && *a == *b) { a++; b++; }
    return (int32_t)*a - (int32_t)*b;
}

int32_t __attribute__((stdcall)) lstrcmpiW(const uint16_t *a, const uint16_t *b)
{
    if (a == 0) a = (const uint16_t *)L"";
    if (b == 0) b = (const uint16_t *)L"";
    for (;;) {
        uint16_t ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb)
            return (int32_t)ca - (int32_t)cb;
        if (ca == 0)
            return 0;
        a++; b++;
    }
}

/* --- directorios/volumen con salida multi-cadena o struct --- */

uint32_t __attribute__((stdcall)) GetLogicalDriveStringsW(uint32_t n, uint16_t *buf)
{
    char a[8];
    uint32_t r = GetLogicalDriveStringsA(sizeof(a), a);
    if (buf == 0 || n == 0)
        return r;
    cp437_to_utf16(buf, n, a);
    return r;
}

/* VolumeInformation: buf_vol (W), fsname (W); serial + flags numericos. */
uint32_t __attribute__((stdcall)) GetVolumeInformationW(const uint16_t *root, uint16_t *vol,
                    uint32_t vn, uint32_t *serial, uint32_t *max_comp,
                    uint32_t *flags, uint16_t *fs, uint32_t fn)
{
    (void)root;
    if (serial)   *serial = 0x4D594F53u;      /* "MYOS" */
    if (max_comp) *max_comp = 15;
    if (flags)    *flags = 0x00080000u;       /* FILE_SUPPORTS_LONG_NAMES */
    if (vol && vn > 0)
        cp437_to_utf16(vol, vn, "MyOS");
    if (fs && fn > 0)
        cp437_to_utf16(fs, fn, "MEFS");
    return 1;
}

uint32_t __attribute__((stdcall)) GetVersionExW(void *vi_w)
{
    my_OSVERSIONINFOA a;
    uint32_t r;
    (void)vi_w;
    a.dwOSVersionInfoSize = sizeof(my_OSVERSIONINFOA);
    r = GetVersionExA(&a);
    if (r == 0)
        return 0;
    return 1;
}

/* GetModuleHandleW: solo el exe (NULL) o kernel32.dll; el resto 0. */
uint32_t __attribute__((stdcall)) GetModuleHandleW(const uint16_t *name)
{
    char a[64];
    if (name == 0)
        return 0xB0000000u;     /* la base del propio modulo */
    utf16_to_cp437(a, name, sizeof(a));
    return GetModuleHandleA(a);
}

uint32_t __attribute__((stdcall)) GetPrivateProfileStringW(const uint16_t *sec,
                    const uint16_t *key, const uint16_t *def,
                    uint16_t *out, uint32_t size, const uint16_t *file)
{
    char s[64], k[64], d[128], f[64], o[256];
    if (out == 0 || size == 0)
        return 0;
    if (sec) utf16_to_cp437(s, sec, sizeof(s));
    if (key) utf16_to_cp437(k, key, sizeof(k));
    if (def) utf16_to_cp437(d, def, sizeof(d));
    if (file) utf16_to_cp437(f, file, sizeof(f));
    GetPrivateProfileStringA(sec ? s : 0, key ? k : 0, def ? d : 0,
                             o, sizeof(o), file ? f : 0);
    return cp437_to_utf16(out, size, o);
}

uint32_t __attribute__((stdcall)) WritePrivateProfileStringW(const uint16_t *sec,
                    const uint16_t *key, const uint16_t *value,
                    const uint16_t *file)
{
    char s[64], k[64], v[128], f[64];
    if (sec) utf16_to_cp437(s, sec, sizeof(s));
    if (key) utf16_to_cp437(k, key, sizeof(k));
    if (value) utf16_to_cp437(v, value, sizeof(v));
    if (file) utf16_to_cp437(f, file, sizeof(f));
    return WritePrivateProfileStringA(sec ? s : 0, key ? k : 0,
                                      value ? v : 0, file ? f : 0);
}

uint32_t __attribute__((stdcall)) GetDateFormatW(uint32_t locale, uint32_t flags,
                    const void *st, const uint16_t *fmt, uint16_t *buf,
                    uint32_t size)
{
    char f[64], o[256];
    uint32_t n;
    if (buf == 0 || size == 0)
        return 0;
    if (fmt) utf16_to_cp437(f, fmt, sizeof(f));
    n = GetDateFormatA(locale, flags, st, fmt ? f : 0, o, sizeof(o));
    if (n == 0)
        return 0;
    return cp437_to_utf16(buf, size, o);
}

uint32_t __attribute__((stdcall)) GetTimeFormatW(uint32_t locale, uint32_t flags,
                    const void *st, const uint16_t *fmt, uint16_t *buf,
                    uint32_t size)
{
    char f[64], o[256];
    uint32_t n;
    if (buf == 0 || size == 0)
        return 0;
    if (fmt) utf16_to_cp437(f, fmt, sizeof(f));
    n = GetTimeFormatA(locale, flags, st, fmt ? f : 0, o, sizeof(o));
    if (n == 0)
        return 0;
    return cp437_to_utf16(buf, size, o);
}

uint32_t __attribute__((stdcall)) GetLocaleInfoW(uint32_t locale, uint32_t type,
                    uint16_t *buf, uint32_t size)
{
    char o[256];
    uint32_t n;
    if (buf == 0 || size == 0)
        return 0;
    n = GetLocaleInfoA(locale, type, o, sizeof(o));
    if (n == 0)
        return 0;
    return cp437_to_utf16(buf, size, o);
}

/* FormatMessageW: igual que la A pero el buffer de salida es UTF-16. */
uint32_t __attribute__((stdcall)) FormatMessageW(uint32_t flags, const void *src,
                    uint32_t msgid, uint32_t lang, uint16_t *buf,
                    uint32_t size, uint32_t *args)
{
    char o[512];
    uint32_t n;
    if (buf == 0 || size == 0)
        return 0;
    n = FormatMessageA(flags, src, msgid, lang, o, sizeof(o), args);
    if (n == 0)
        return 0;
    return cp437_to_utf16(buf, size, o);
}

/* --- tabla de exports --- */

win32_export_t __exports[] __attribute__((section(".exports"))) = {
    { "GetStdHandle",          (uint32_t)&GetStdHandle },
    { "WriteFile",             (uint32_t)&WriteFile },
    { "ExitProcess",           (uint32_t)&ExitProcess },
    { "GetCurrentProcess",     (uint32_t)&GetCurrentProcess },
    { "GetCurrentProcessId",   (uint32_t)&GetCurrentProcessId },
    { "GetCurrentThreadId",    (uint32_t)&GetCurrentThreadId },
    { "TerminateProcess",      (uint32_t)&TerminateProcess },
    { "GetCommandLineA",       (uint32_t)&GetCommandLineA },
    { "GetCommandLineW",       (uint32_t)&GetCommandLineW },
    { "GetEnvironmentStringsA",(uint32_t)&GetEnvironmentStringsA },
    { "FreeEnvironmentStringsA",(uint32_t)&FreeEnvironmentStringsA },
    { "GetEnvironmentStringsW",(uint32_t)&GetEnvironmentStringsW },
    { "FreeEnvironmentStringsW",(uint32_t)&FreeEnvironmentStringsW },
    { "GetLastError",          (uint32_t)&GetLastError },
    { "SetLastError",          (uint32_t)&SetLastError },
    { "InitializeCriticalSection", (uint32_t)&InitializeCriticalSection },
    { "EnterCriticalSection",  (uint32_t)&EnterCriticalSection },
    { "LeaveCriticalSection",  (uint32_t)&LeaveCriticalSection },
    { "DeleteCriticalSection", (uint32_t)&DeleteCriticalSection },
    { "TlsAlloc",              (uint32_t)&TlsAlloc },
    { "TlsFree",               (uint32_t)&TlsFree },
    { "TlsGetValue",           (uint32_t)&TlsGetValue },
    { "TlsSetValue",           (uint32_t)&TlsSetValue },
    { "GetModuleHandleA",      (uint32_t)&GetModuleHandleA },
    { "GetModuleFileNameA",    (uint32_t)&GetModuleFileNameA },
    { "GetModuleFileNameW",    (uint32_t)&GetModuleFileNameW },
    { "GetProcAddress",        (uint32_t)&GetProcAddress },
    { "LoadLibraryA",          (uint32_t)&LoadLibraryA },
    { "FreeLibrary",           (uint32_t)&FreeLibrary },
    { "MultiByteToWideChar",   (uint32_t)&MultiByteToWideChar },
    { "WideCharToMultiByte",   (uint32_t)&WideCharToMultiByte },
    { "GetCPInfo",             (uint32_t)&GetCPInfo },
    { "SetUnhandledExceptionFilter", (uint32_t)&SetUnhandledExceptionFilter },
    { "UnhandledExceptionFilter",    (uint32_t)&UnhandledExceptionFilter },
    { "IsDebuggerPresent",     (uint32_t)&IsDebuggerPresent },
    { "Sleep",                 (uint32_t)&Sleep },
    { "IsDBCSLeadByte",        (uint32_t)&IsDBCSLeadByte },
    { "GetTickCount",          (uint32_t)&GetTickCount },
    { "GetSystemTimeAsFileTime", (uint32_t)&GetSystemTimeAsFileTime },
    { "QueryPerformanceCounter", (uint32_t)&QueryPerformanceCounter },
    { "QueryPerformanceFrequency", (uint32_t)&QueryPerformanceFrequency },
    { "VirtualProtect",        (uint32_t)&VirtualProtect },
    { "VirtualQuery",          (uint32_t)&VirtualQuery },
    { "VirtualAlloc",          (uint32_t)&VirtualAlloc },
    { "VirtualFree",           (uint32_t)&VirtualFree },
    { "GetProcessHeap",        (uint32_t)&GetProcessHeap },
    { "HeapAlloc",             (uint32_t)&HeapAlloc },
    { "HeapFree",              (uint32_t)&HeapFree },
    { "HeapReAlloc",           (uint32_t)&HeapReAlloc },
    { "HeapSize",              (uint32_t)&HeapSize },
    { "GetStartupInfoA",       (uint32_t)&GetStartupInfo },
    { "CreateFileA",           (uint32_t)&CreateFileA },
    { "ReadFile",              (uint32_t)&ReadFile },
    { "GetFileSize",           (uint32_t)&GetFileSize },
    { "CloseHandle",           (uint32_t)&CloseHandle },
    { "FindFirstFileA",        (uint32_t)&FindFirstFileA },
    { "FindNextFileA",         (uint32_t)&FindNextFileA },
    { "FindClose",             (uint32_t)&FindClose },
    { "GetCurrentDirectoryA",  (uint32_t)&GetCurrentDirectoryA },
    { "GetCurrentDirectoryW",  (uint32_t)&GetCurrentDirectoryW },
    { "SetCurrentDirectoryA",  (uint32_t)&SetCurrentDirectoryA },
{ "SetCurrentDirectoryW",  (uint32_t)&SetCurrentDirectoryW },
     { "GetTempPathA",          (uint32_t)&GetTempPathA },
     { "GetWindowsDirectoryA",  (uint32_t)&GetWindowsDirectoryA },
     { "GetSystemDirectoryA",   (uint32_t)&GetSystemDirectoryA },
     { "GetVersionExA",         (uint32_t)&GetVersionExA },
     { "GetVersion",            (uint32_t)&GetVersion },
     { "GetFileInformationByHandle", (uint32_t)&GetFileInformationByHandle },
     { "GlobalAlloc",           (uint32_t)&GlobalAlloc },
    { "GlobalLock",            (uint32_t)&GlobalLock },
    { "GlobalUnlock",          (uint32_t)&GlobalUnlock },
    { "GlobalFree",            (uint32_t)&GlobalFree },
    { "LocalFree",             (uint32_t)&LocalFree },
    { "lstrlenA",              (uint32_t)&lstrlenA },
    { "lstrcpyA",              (uint32_t)&lstrcpyA },
    { "lstrcpynA",             (uint32_t)&lstrcpynA },
    { "lstrcatA",              (uint32_t)&lstrcatA },
    { "lstrcmpA",              (uint32_t)&lstrcmpA },
    { "lstrcmpiA",             (uint32_t)&lstrcmpiA },
    { "GetFileAttributesA",    (uint32_t)&GetFileAttributesA },
    { "SetFileAttributesA",    (uint32_t)&SetFileAttributesA },
    { "SetFilePointer",        (uint32_t)&SetFilePointer },
    { "SetEndOfFile",          (uint32_t)&SetEndOfFile },
    { "GetFullPathNameA",      (uint32_t)&GetFullPathNameA },
    { "CreateThread",          (uint32_t)&CreateThread },
    { "ExitThread",            (uint32_t)&ExitThread },
    { "_thread_ret",           (uint32_t)&_thread_ret },
    { "CreateProcessA",        (uint32_t)&CreateProcessA },
    { "MulDiv",                (uint32_t)&MulDiv },
    { "GetPrivateProfileStringA", (uint32_t)&GetPrivateProfileStringA },
    { "WritePrivateProfileStringA", (uint32_t)&WritePrivateProfileStringA },
    { "GetDateFormatA",        (uint32_t)&GetDateFormatA },
    { "GetTimeFormatA",        (uint32_t)&GetTimeFormatA },
    { "GetLocaleInfoA",        (uint32_t)&GetLocaleInfoA },
    { "FormatMessageA",        (uint32_t)&FormatMessageA },
    /* Fase 25 (W2A): thunks W->A */
    { "CreateFileW",           (uint32_t)&CreateFileW },
    { "FindFirstFileW",        (uint32_t)&FindFirstFileW },
    { "FindNextFileW",         (uint32_t)&FindNextFileW },
    { "GetEnvironmentVariableA", (uint32_t)&GetEnvironmentVariableA },
    { "SetEnvironmentVariableA", (uint32_t)&SetEnvironmentVariableA },
    { "GetEnvironmentVariableW", (uint32_t)&GetEnvironmentVariableW },
    { "SetEnvironmentVariableW", (uint32_t)&SetEnvironmentVariableW },
    { "DeleteFileA",           (uint32_t)&DeleteFileA },
    { "DeleteFileW",           (uint32_t)&DeleteFileW },
    { "MoveFileA",             (uint32_t)&MoveFileA },
    { "MoveFileW",             (uint32_t)&MoveFileW },
    { "CopyFileA",             (uint32_t)&CopyFileA },
    { "CopyFileW",             (uint32_t)&CopyFileW },
    { "CreateDirectoryA",      (uint32_t)&CreateDirectoryA },
    { "CreateDirectoryW",      (uint32_t)&CreateDirectoryW },
    { "RemoveDirectoryA",      (uint32_t)&RemoveDirectoryA },
    { "RemoveDirectoryW",      (uint32_t)&RemoveDirectoryW },
    { "GetLogicalDriveStringsA", (uint32_t)&GetLogicalDriveStringsA },
    { "GetLogicalDriveStringsW", (uint32_t)&GetLogicalDriveStringsW },
    { "GetVolumeInformationW", (uint32_t)&GetVolumeInformationW },
    { "GetVersionExW",         (uint32_t)&GetVersionExW },
    { "GetModuleHandleW",      (uint32_t)&GetModuleHandleW },
    { "GetFullPathNameW",      (uint32_t)&GetFullPathNameW },
    { "GetTempPathW",          (uint32_t)&GetTempPathW },
    { "GetWindowsDirectoryW",  (uint32_t)&GetWindowsDirectoryW },
    { "GetSystemDirectoryW",   (uint32_t)&GetSystemDirectoryW },
    { "GetFileAttributesW",    (uint32_t)&GetFileAttributesW },
    { "SetFileAttributesW",    (uint32_t)&SetFileAttributesW },
    { "lstrlenW",              (uint32_t)&lstrlenW },
    { "lstrcpyW",              (uint32_t)&lstrcpyW },
    { "lstrcpynW",             (uint32_t)&lstrcpynW },
    { "lstrcatW",              (uint32_t)&lstrcatW },
    { "lstrcmpW",              (uint32_t)&lstrcmpW },
    { "lstrcmpiW",             (uint32_t)&lstrcmpiW },
    { "GetPrivateProfileStringW", (uint32_t)&GetPrivateProfileStringW },
    { "WritePrivateProfileStringW", (uint32_t)&WritePrivateProfileStringW },
    { "GetDateFormatW",        (uint32_t)&GetDateFormatW },
    { "GetTimeFormatW",        (uint32_t)&GetTimeFormatW },
    { "GetLocaleInfoW",        (uint32_t)&GetLocaleInfoW },
    { "FormatMessageW",        (uint32_t)&FormatMessageW },
    { "", 0 },
};