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

#define INVALID_HANDLE_VALUE ((uint32_t)-1)

/* --- tabla de archivos abiertos (Fase E: escritura) --- */

struct win32_file_s {
    char     name[40];
    uint32_t size;
    uint32_t pos;
    uint32_t writable;      /* abierto para GENERIC_WRITE (Fase E) */
    uint32_t wlen;          /* bytes acumulados en wbuf               */
    uint8_t *wbuf;          /* buffer de escritura (apunta a wbuf_global) */
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

uint32_t GetStdHandle(uint32_t which)
{
    (void)which;
    return STD_OUTPUT_HANDLE;
}

uint32_t WriteFile(uint32_t h, const void *buf, uint32_t n,
                   uint32_t *written)
{
    int r;
    /* Fase E: handle de archivo abierto para escritura -> acumular en
     * wbuf; el contenido se escribe al FS en CloseHandle (o SetEndOfFile). */
    if (h != STD_OUTPUT_HANDLE) {
        struct win32_file_s *f = win32_file_of((uint32_t)h);
        if (f == 0 || !f->writable)
            return 0;
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

void ExitProcess(uint32_t code)     { trace("[k32] ExitProcess\n"); sys_exit(code); }
void TerminateProcess(uint32_t h, uint32_t code) { (void)h; sys_exit(code); }
uint32_t GetCurrentProcess(void)    { return (uint32_t)-1; }

static uint32_t sys_getpid(void)
{
    uint32_t r;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(5));
    return r;
}

uint32_t GetCurrentProcessId(void)  { return sys_getpid(); }

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
uint32_t GetModuleFileNameA(uint32_t hmodule, char *buf, uint32_t max)
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

/* Linea de comandos real del proceso: el kernel la copia al TIB de la
 * tarea (WIN32_TIB_CMDLINE_OFF) al lanzarla o hacer exec; %fs:0x18 da
 * la base del TIB de la tarea actual. */
#define WIN32_TIB_VA          0x84000000u
#define WIN32_TIB_CMDLINE_OFF 0x100u

char *GetCommandLineA(void)
{
    trace("[k32] GetCommandLineA\n");
    uint32_t tib = 0;
    __asm__ volatile("mov %%fs:0x18, %0" : "=r"(tib));
    return (char *)(tib + WIN32_TIB_CMDLINE_OFF);
}

static char *env_block[] = { (char *)"PATH=.\0HOME=.\0", 0 };

char **GetEnvironmentStringsA(void) { return env_block; }
void  FreeEnvironmentStringsA(char **p) { (void)p; }

/* --- errores --- */

static uint32_t last_error;

uint32_t GetLastError(void)       { return last_error; }
void     SetLastError(uint32_t e) { last_error = e; }

/* --- secciones criticas (RTL_CRITICAL_SECTION vista como int32) --- */

typedef struct { volatile int32_t lock; } cs_t;

void InitializeCriticalSection(void *lp)
{
    cs_t *c = (cs_t *)lp;
    c->lock = 0;
}

void EnterCriticalSection(void *lp)
{
    cs_t *c = (cs_t *)lp;
    for (;;) {
        __asm__ volatile("lock btsl $0, %0" : "+m"(c->lock) : : "cc");
        if (!(c->lock & 1))
            return;
        for (volatile int i = 0; i < 200; i++) ;
    }
}

void LeaveCriticalSection(void *lp)
{
    cs_t *c = (cs_t *)lp;
    __asm__ volatile("lock btrl $0, %0" : "+m"(c->lock) : : "cc");
}

void DeleteCriticalSection(void *lp) { (void)lp; }

/* IsDBCSLeadByte: 0 con codepage de un byte (CP1252, el unico que
 * soportamos); el CRT mingw lo usa en mbstowcs. */
int IsDBCSLeadByte(int b) { (void)b; return 0; }

/* --- TLS --- */

static uint32_t tls_slots[64];

void *   TlsGetValue(uint32_t s) { return (s < 64) ? (void *)tls_slots[s] : 0; }
void     TlsSetValue(uint32_t s, void *v) { if (s < 64) tls_slots[s] = (uint32_t)v; }
uint32_t TlsAlloc(void) { return 1; }
void     TlsFree(uint32_t s) { (void)s; }

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

uint32_t GetModuleHandleA(const char *name)
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

uint32_t GetProcAddress(uint32_t hmod, const char *name)
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

uint32_t LoadLibraryA(const char *name)
{
    uint32_t h;
    trace("[k32] LoadLibraryA\n");
    if (name == 0)
        return 0;
    h = GetModuleHandleA(name);
    return h != 0 ? h : 0;
}

uint32_t FreeLibrary(uint32_t h) { (void)h; return 1; }

/* --- codepage ASCII (CP_ACP/CP_OEM/MB para el CRT) --- */

typedef struct {
    uint32_t max_char_size;
    uint8_t  default_char[8];
    uint8_t  lead[12];
} CPINFO;

uint32_t MultiByteToWideChar(uint32_t cp, uint32_t flags,
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

uint32_t WideCharToMultiByte(uint32_t cp, uint32_t flags,
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

uint32_t GetCPInfo(uint32_t cp, CPINFO *info)
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

uint32_t SetUnhandledExceptionFilter(uint32_t fn)
{
    uint32_t old = unhandled_filter;
    unhandled_filter = fn;
    return old;
}

uint32_t UnhandledExceptionFilter(uint32_t ei) { (void)ei; return 1; }
int IsDebuggerPresent(void) { return 0; }

void Sleep(uint32_t ms)
{
    (void)ms;
    for (volatile uint32_t i = 0; i < 100000; i++) ;
}

uint32_t GetTickCount(void) { return 0; }

void GetSystemTimeAsFileTime(uint32_t *t) { if (t) { t[0] = 0; t[1] = 0; } }
uint32_t QueryPerformanceCounter(void *c) { if (c) *(uint64_t *)c = 0; return 1; }
uint32_t QueryPerformanceFrequency(void *c) { if (c) *(uint64_t *)c = 1000; return 1; }

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

uint32_t VirtualProtect(void *addr, uint32_t size, uint32_t prot,
                        uint32_t *old)
{
    (void)addr; (void)size; (void)prot;
    if (old) *old = PAGE_READWRITE;
    return 1;
}

uint32_t VirtualQuery(const void *addr, MEMORY_BASIC_INFORMATION *mbi,
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

void *VirtualAlloc(void *addr, uint32_t size, uint32_t type, uint32_t prot)
{
    (void)addr; (void)type; (void)prot;
    return win_malloc(size);
}

uint32_t VirtualFree(void *p, uint32_t size, uint32_t type)
{
    (void)p; (void)size; (void)type;
    return 1;
}

/* --- heap (bump del kernel via SYS_MALLOC) --- */

static void win_free(void *p)
{
    int r;
    (void)p;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(SYS_FREE) : "memory");
    (void)r;
}

uint32_t GetProcessHeap(void) { return 1; }

void *HeapAlloc(uint32_t heap, uint32_t flags, uint32_t size)
{
    (void)heap; (void)flags;
    return win_malloc(size);
}

uint32_t HeapFree(uint32_t heap, uint32_t flags, void *p)
{
    (void)heap; (void)flags;
    win_free(p);
    return 1;
}

void *HeapReAlloc(uint32_t heap, uint32_t flags, void *p, uint32_t size)
{
    (void)heap; (void)flags; (void)p;
    return win_malloc(size);
}

uint32_t HeapSize(uint32_t heap, uint32_t flags, const void *p)
{
    (void)heap; (void)flags; (void)p;
    return 0x1000;
}

void GetStartupInfo(void *si)
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

/* Abre un archivo del FS. Devuelve HANDLE (0x100+slot) o -1. */
void *CreateFileA(const char *name, uint32_t access, uint32_t share,
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
    return (void *)(uint32_t)(0x100 + i);
}

/* Lee 'n' bytes desde la posicion del handle. */
uint32_t ReadFile(void *h, void *buf, uint32_t n, uint32_t *read,
                  uint32_t ovl)
{
    int i = (int)(uint32_t)h - 0x100;
    int r;
    (void)ovl;
    if (i < 0 || i >= 16 || open_files[i].name[0] == 0) {
        if (read) *read = 0;
        return 0;
    }
    r = sys_dread(open_files[i].name, buf, open_files[i].pos, n);
    if (r <= 0) {
        if (read) *read = 0;
        return 0;
    }
    open_files[i].pos += (uint32_t)r;
    if (read) *read = (uint32_t)r;
    return 1;
}

uint32_t GetFileSize(void *h, uint32_t *high)
{
    int i = (uint32_t)h - 0x100;
    if (i < 0 || i >= 16 || open_files[i].name[0] == 0)
        return INVALID_HANDLE_VALUE;
    if (high) *high = 0;
    return open_files[i].size;
}

uint32_t CloseHandle(void *h)
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

void *FindFirstFileA(const char *pat, WIN32_FIND_DATAA *fd)
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

int FindNextFileA(void *h, WIN32_FIND_DATAA *fd)
{
    int i = (uint32_t)h - 0x200;
    if (i < 0 || i >= find_st_n)
        return 0;
    return find_next_entry(&find_st[i], find_st[i].pat, fd);
}

uint32_t FindClose(void *h)
{
    int i = (uint32_t)h - 0x200;
    if (i >= 0 && i < find_st_n)
        find_st_n--;
    return 1;
}

void GetCurrentDirectoryA(uint32_t n, char *buf)
{
    trace("[k32] GetCurrentDirectoryA n=");
    trace_hex(n);
    trace("\n");
    (void)n;
    if (buf)
        buf[0] = 0;
}

void GetCurrentDirectoryW(uint32_t n, uint16_t *buf)
{
    (void)n;
    if (buf)
        buf[0] = 0;
}

void SetCurrentDirectoryA(const char *d)
{
    trace("[k32] SetCurrentDirectoryA '");
    if (d)
        trace(d);
    trace("'\n");
}

void SetCurrentDirectoryW(const uint16_t *d) { (void)d; }

/* --- Global*: memoria global = el heap del proceso (no movible) --- */

void *GlobalAlloc(uint32_t flags, uint32_t size)
{
    (void)flags;
    if (size == 0)
        return 0;
    return win_malloc(size);
}

void *GlobalLock(void *h)
{
    return h;                   /* GMEM_FIXED: el handle ES el puntero */
}

uint32_t GlobalUnlock(void *h)
{
    (void)h;
    return 1;
}

void *GlobalFree(void *h)
{
    win_free(h);
    return 0;                   /* NULL = exito */
}

/* LocalFree/LocalAlloc: el heap del proceso. */
void *LocalFree(void *h)
{
    win_free(h);
    return 0;
}

/* --- lstr*: strings ANSI --- */

uint32_t lstrlenA(const char *s)
{
    trace("[k32] lstrlenA '");
    if (s) trace(s);
    trace("'\n");
    return strlen_u(s);
}

char *lstrcpyA(char *dst, const char *src)
{
    uint32_t i = 0;
    do { dst[i] = src[i]; } while (src[i++]);
    return dst;
}

char *lstrcpynA(char *dst, const char *src, uint32_t n)
{
    uint32_t i;
    if (n == 0)
        return dst;
    for (i = 0; i < n - 1 && src[i]; i++)
        dst[i] = src[i];
    dst[i] = 0;
    return dst;
}

char *lstrcatA(char *dst, const char *src)
{
    uint32_t d = 0, i = 0;
    while (dst[d]) d++;
    while (src[i]) dst[d++] = src[i++];
    dst[d] = 0;
    return dst;
}

int32_t lstrcmpA(const char *a, const char *b)
{
    while (*a && *b && *a == *b) { a++; b++; }
    return (int32_t)(uint8_t)*a - (int32_t)(uint8_t)*b;
}

int32_t lstrcmpiA(const char *a, const char *b)
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

uint32_t GetFileAttributesA(const char *name)
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

uint32_t SetFileAttributesA(const char *name, uint32_t attrs)
{
    (void)name; (void)attrs;
    return 1;
}

uint32_t SetFilePointer(void *h, int32_t dist_low, int32_t *dist_high,
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

uint32_t SetEndOfFile(void *h)
{
    int i = (uint32_t)h - 0x100;
    if (i >= 0 && i < 16 && open_files[i].writable && open_files[i].name[0]) {
        sys_fwrite(open_files[i].name, open_files[i].wbuf,
                   open_files[i].wlen);
        sys_flush();
    }
    return 1;
}

uint32_t GetFullPathNameA(const char *name, uint32_t n, char *buf,
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

void *CreateThread(uint32_t attrs, uint32_t stack_size, void *start,
                   void *param, uint32_t flags, uint32_t *tid)
{
    thread_fn fn = (thread_fn)start;
    (void)attrs; (void)stack_size; (void)flags;
    trace("[k32] CreateThread start=");
    trace_hex((uint32_t)start);
    trace("\n");
    if (fn != 0)
        fn(param);
    if (tid)
        *tid = 1;
    return (void *)1;
}

/* --- procesos: Metapad abre "una segunda copia" con CreateProcessA;
 * sin multitarea Win32 devolvemos FALSE y el llamador muestra error o
 * no hace nada. --- */

uint32_t CreateProcessA(const char *app, char *cmd, uint32_t p1,
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

int32_t MulDiv(int32_t a, int32_t b, int32_t c)
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

uint32_t GetPrivateProfileStringA(const char *sec, const char *key,
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
uint32_t WritePrivateProfileStringA(const char *sec, const char *key,
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

uint32_t GetDateFormatA(uint32_t locale, uint32_t flags, const void *st,
                        const char *fmt, char *buf, uint32_t size)
{
    const char *def = "M/d/yyyy";
    (void)locale; (void)flags; (void)st;
    if (buf == 0 || size == 0)
        return 0;
    return fmt_tokens(fmt ? fmt : def, buf, size, 1);
}

uint32_t GetTimeFormatA(uint32_t locale, uint32_t flags, const void *st,
                        const char *fmt, char *buf, uint32_t size)
{
    const char *def = "h:mm tt";
    (void)locale; (void)flags; (void)st;
    if (buf == 0 || size == 0)
        return 0;
    return fmt_tokens(fmt ? fmt : def, buf, size, 0);
}

/* GetLocaleInfoA: valores de ingles (EE. UU.), idioma 9 (en-US). */
uint32_t GetLocaleInfoA(uint32_t locale, uint32_t type, char *buf,
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
uint32_t FormatMessageA(uint32_t flags, const void *src, uint32_t msgid,
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

/* --- tabla de exports --- */

win32_export_t __exports[] __attribute__((section(".exports"))) = {
    { "GetStdHandle",          (uint32_t)&GetStdHandle },
    { "WriteFile",             (uint32_t)&WriteFile },
    { "ExitProcess",           (uint32_t)&ExitProcess },
    { "GetCurrentProcess",     (uint32_t)&GetCurrentProcess },
    { "GetCurrentProcessId",   (uint32_t)&GetCurrentProcessId },
    { "TerminateProcess",      (uint32_t)&TerminateProcess },
    { "GetCommandLineA",       (uint32_t)&GetCommandLineA },
    { "GetCommandLineW",       (uint32_t)&GetCommandLineA },
    { "GetEnvironmentStringsA",(uint32_t)&GetEnvironmentStringsA },
    { "FreeEnvironmentStringsA",(uint32_t)&FreeEnvironmentStringsA },
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
    { "CreateProcessA",        (uint32_t)&CreateProcessA },
    { "MulDiv",                (uint32_t)&MulDiv },
    { "GetPrivateProfileStringA", (uint32_t)&GetPrivateProfileStringA },
    { "WritePrivateProfileStringA", (uint32_t)&WritePrivateProfileStringA },
    { "GetDateFormatA",        (uint32_t)&GetDateFormatA },
    { "GetTimeFormatA",        (uint32_t)&GetTimeFormatA },
    { "GetLocaleInfoA",        (uint32_t)&GetLocaleInfoA },
    { "FormatMessageA",        (uint32_t)&FormatMessageA },
    { "", 0 },
};