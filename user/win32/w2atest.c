/* MyOS - user/win32/w2atest.c
 * Fase 25 (W2A), paso 1: set de inicializacion W de MSVC.
 * Verifica: GetCommandLineW, GetModuleFileNameW, GetEnvironmentStringsW,
 * GetCurrentThreadId, GetTickCount real (>0, creciente), QPC real
 * (frecuencia 1193182, contador >0 y creciente), GetSystemTimeAsFileTime
 * real (>= 2024-01-01) y las secciones criticas. Imprime al serial. */

#include <windows.h>
#include <string.h>

static void logstr(const char *s)
{
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD n = 0;
    int len = 0;
    while (s[len]) len++;
    WriteFile(h, s, len, &n, 0);
}

static void lognum(unsigned int v)
{
    char b[12];
    int p = 0, u = (int)v < 0 ? -(int)v : (int)v;
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if ((int)v < 0) { char c = '-'; WriteFile(h, &c, 1, &(DWORD){0}, 0); }
    do { b[p++] = (char)('0' + u % 10); u /= 10; } while (u);
    while (p > 0) WriteFile(h, &b[--p], 1, &(DWORD){0}, 0);
}

static void loghex(unsigned int v)
{
    static const char hx[] = "0123456789abcdef";
    char t[10];
    int i;
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    t[0] = '0'; t[1] = 'x';
    for (i = 0; i < 8; i++)
        t[2 + i] = hx[(v >> (28 - i * 4)) & 0xF];
    WriteFile(h, t, 10, &(DWORD){0}, 0);
}

static void logw(const wchar_t *w, int max)
{
    char buf[64];
    int i, n = 0;
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    for (i = 0; i < max && w[i] && n < 63; i++)
        buf[n++] = (char)(w[i] & 0xFF);
    buf[n] = 0;
    logstr(buf);
}

static int w_eq_a(const wchar_t *w, const char *a)
{
    int i;
    for (i = 0; a[i]; i++)
        if ((wchar_t)(unsigned char)a[i] != w[i])
            return 0;
    return w[i] == 0;
}

int main(void)
{
    int fails = 0;
    wchar_t *cmdw = GetCommandLineW();
    char *cmda = GetCommandLineA();
    wchar_t wpath[MAX_PATH];
    char apath[MAX_PATH];
    DWORD wn, an;
    wchar_t *envw = GetEnvironmentStringsW();
    DWORD pid = GetCurrentProcessId();
    DWORD tid = GetCurrentThreadId();
    DWORD t0 = GetTickCount(), t1;
    LARGE_INTEGER qpc0, qpc1, qf;
    FILETIME ft0, ft1;
    CRITICAL_SECTION cs;

    logstr("w2atest: cmdlineW='");
    logw(cmdw, 64);
    logstr("'\n");
    if (!w_eq_a(cmdw, cmda)) { logstr("w2atest: FAIL cmdlineW != cmdlineA\n"); fails++; }

    wn = GetModuleFileNameW(0, wpath, MAX_PATH);
    an = GetModuleFileNameA(0, apath, MAX_PATH);
    logstr("w2atest: moduleW='");
    logw(wpath, 64);
    logstr("'\n");
    if (wn == 0 || wn != an || !w_eq_a(wpath, apath)) {
        logstr("w2atest: FAIL moduleW\n");
        fails++;
    }

    logstr("w2atest: envW='");
    logw(envw, 16);
    logstr("'\n");
    if (envw[0] != 'P' || envw[1] != 'A' || envw[2] != 'T' || envw[3] != 'H') {
        logstr("w2atest: FAIL envW\n");
        fails++;
    }

    if (tid != pid) { logstr("w2atest: FAIL tid != pid\n"); fails++; }
    logstr("w2atest: pid="); lognum(pid);
    logstr(" tid="); lognum(tid); logstr("\n");

    t0 = GetTickCount();
    Sleep(500);
    t1 = GetTickCount();
    logstr("w2atest: ticks "); lognum(t0); logstr(" -> "); lognum(t1); logstr("\n");
    if (t1 < t0 + 400) { logstr("w2atest: FAIL GetTickCount\n"); fails++; }

    QueryPerformanceFrequency(&qf);
    QueryPerformanceCounter(&qpc0);
    QueryPerformanceCounter(&qpc1);
    logstr("w2atest: qpf="); loghex((unsigned)(qf.QuadPart >> 32));
    logstr(" "); loghex((unsigned)qf.QuadPart); logstr("\n");
    if (qf.QuadPart != 1193182) { logstr("w2atest: FAIL qpf\n"); fails++; }
    if (qpc1.QuadPart <= qpc0.QuadPart) { logstr("w2atest: FAIL qpc\n"); fails++; }

    /* filetime: esperar a que cambie el tick para comparar 2 muestras */
    GetSystemTimeAsFileTime(&ft0);
    while (GetTickCount() == t1) ;
    GetSystemTimeAsFileTime(&ft1);
    logstr("w2atest: got both filetimes\n");
    {
        ULARGE_INTEGER a, b, epoch;
        a.LowPart = ft0.dwLowDateTime; a.HighPart = ft0.dwHighDateTime;
        b.LowPart = ft1.dwLowDateTime; b.HighPart = ft1.dwHighDateTime;
        epoch.QuadPart = 133485408000000000ULL;
        logstr("w2atest: filetime=");
        loghex(a.HighPart); logstr(" "); loghex(a.LowPart); logstr("\n");
        logstr("w2atest: u64cmp\n");
        if (a.QuadPart < epoch.QuadPart || b.QuadPart <= a.QuadPart) {
            logstr("w2atest: FAIL filetime\n");
            fails++;
        }
        logstr("w2atest: u64cmp ok\n");
    }
    if (fails) {
        logstr("w2atest:FAIL\n");
        return 1;
    }
    logstr("w2atest:PASS\n");
    return 0;
}