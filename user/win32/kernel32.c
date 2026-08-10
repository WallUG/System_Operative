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

#define INVALID_HANDLE_VALUE ((uint32_t)-1)

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
    if (h != STD_OUTPUT_HANDLE)
        return 0;
    r = sys_write((const char *)buf, n);
    if (written)
        *written = (uint32_t)(r > 0 ? r : 0);
    return (uint32_t)(r > 0 ? 1 : 0);
}

void ExitProcess(uint32_t code)     { sys_exit(code); }
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
 * hmodule = NULL significa "el proceso actual". */
uint32_t GetModuleFileNameA(uint32_t hmodule, char *buf, uint32_t max)
{
    uint32_t n;

    (void)hmodule;
    if (buf == 0 || max == 0)
        return 0;
    n = sys_selfname(buf, max);
    if (n == (uint32_t)-1)
        return 0;
    return n;
}

char *GetCommandLineA(void)
{
    static char cmd[] = "program.exe\0";
    return cmd;
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

uint32_t GetModuleHandleA(const char *name)
{
    if (name == 0)
        return KERNEL_BASE;
    if (ci_eq(name, "kernel32") || ci_eq(name, "kernel32.dll")
        || ci_eq(name, "msvcrt") || ci_eq(name, "msvcrt.dll")
        || ci_eq(name, "ntdll") || ci_eq(name, "ntdll.dll")
        || ci_eq(name, "user32") || ci_eq(name, "user32.dll"))
        return KERNEL_BASE;
    return 0;
}

uint32_t GetProcAddress(uint32_t hmod, const char *name)
{
    uint32_t i;
    if (hmod == 0)
        hmod = KERNEL_BASE;
    if (hmod != KERNEL_BASE)
        return 0;
    for (i = 0; __exports[i].name[0]; i++)
        if (ci_eq(__exports[i].name, name))
            return __exports[i].fn;
    return 0;
}

uint32_t LoadLibraryA(const char *name)
{
    if (name == 0)
        return 0;
    return GetModuleHandleA(name) != 0 ? KERNEL_BASE : 0;
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

typedef struct {
    char     name[40];
    uint32_t size;
    uint32_t pos;
} win32_file_t;

static win32_file_t open_files[16];

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

/* Abre un archivo del FS. Devuelve HANDLE (0x100+slot) o -1. */
void *CreateFileA(const char *name, uint32_t access, uint32_t share,
                  uint32_t sec_attrs, uint32_t creation, uint32_t flags,
                  uint32_t tmpl)
{
    int i, sz;
    (void)access; (void)share; (void)sec_attrs; (void)creation;
    (void)flags; (void)tmpl;
    if (name == 0)
        return (void *)INVALID_HANDLE_VALUE;
    sz = sys_fsize(name);
    if (sz < 0)
        return (void *)INVALID_HANDLE_VALUE;
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
    if (i >= 0 && i < 16)
        open_files[i].name[0] = 0;
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

void SetCurrentDirectoryA(const char *d) { (void)d; }
void SetCurrentDirectoryW(const uint16_t *d) { (void)d; }

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
    { "", 0 },
};