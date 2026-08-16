/* MyOS - user/win32/w2atest.c
 * Fase 25 (W2A), paso 1: set de inicializacion W de MSVC.
 * Verifica: GetCommandLineW, GetModuleFileNameW, GetEnvironmentStringsW,
 * GetCurrentThreadId, GetTickCount real (>0, creciente), QPC real
 * (frecuencia 1193182, contador >0 y creciente), GetSystemTimeAsFileTime
 * real (>= 2024-01-01) y las secciones criticas. Imprime al serial. */

#include <windows.h>
#include <string.h>
#include <wchar.h>

typedef int (__cdecl *wgetmainargs_fn)(int *, wchar_t ***, wchar_t ***, int *, int *);

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

    /* ============ Fase 25 paso 2: thunks W->A ============ */

    /* 1) CreateFileW + WriteFile + GetFileAttributesW + DeleteFileW */
    {
        HANDLE h = CreateFileW(L"wfile_w.txt", 0x40000000, 0, 0, 2, 0, 0);
        DWORD wr = 0;
        if (h == INVALID_HANDLE_VALUE) {
            logstr("w2atest: FAIL CreateFileW\n"); fails++;
        } else {
            static const char data[] = "hola W2A";
            if (!WriteFile(h, data, sizeof(data) - 1, &wr, 0) || wr != sizeof(data) - 1) {
                logstr("w2atest: FAIL WriteFile\n"); fails++;
            }
            CloseHandle(h);
        }
        if (GetFileAttributesW(L"wfile_w.txt") == 0xFFFFFFFFu) {
            logstr("w2atest: FAIL GetFileAttributesW\n"); fails++;
        }
        if (!DeleteFileW(L"wfile_w.txt")) {
            logstr("w2atest: FAIL DeleteFileW\n"); fails++;
        }
        if (GetFileAttributesW(L"wfile_w.txt") != 0xFFFFFFFFu) {
            logstr("w2atest: FAIL DeleteFileW (sigue existiendo)\n"); fails++;
        }
        logstr("w2atest: createw/writew/attrsW/deletew ok\n");
    }

    /* 2) CreateFileW con nombre no-ASCII (cp437): "café.txt" -> 0x82 */
    {
        HANDLE h = CreateFileW(L"caf\x00e9.txt", 0x40000000, 0, 0, 2, 0, 0);
        DWORD wr = 0;
        if (h == INVALID_HANDLE_VALUE) {
            logstr("w2atest: FAIL cp437 create\n"); fails++;
        } else {
            CloseHandle(h);
        }
        if (!DeleteFileW(L"caf\x00e9.txt")) {
            logstr("w2atest: FAIL cp437 delete\n"); fails++;
        }
        logstr("w2atest: cp437 unicode name ok\n");
    }

    /* 3) FindFirstFileW/FindNextFileW: listar la raiz, buscar
     * w2atest.exe */
    {
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW(L"*", &fd);
        int found = 0, n = 0;
        if (h == INVALID_HANDLE_VALUE) {
            logstr("w2atest: FAIL FindFirstFileW\n"); fails++;
        } else {
            do {
                n++;
                if (lstrcmpW(fd.cFileName, L"w2atest.exe") == 0)
                    found = 1;
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
        logstr("w2atest: findw items="); lognum(n);
        logstr(" found="); lognum(found); logstr("\n");
        if (n == 0 || !found) {
            logstr("w2atest: FAIL FindFirstFileW\n"); fails++;
        }
    }

    /* 4) directorios y paths W */
    {
        wchar_t tmp[MAX_PATH], win[MAX_PATH], sys[MAX_PATH], cur[MAX_PATH];
        GetTempPathW(MAX_PATH, tmp);
        GetWindowsDirectoryW(win, MAX_PATH);
        GetSystemDirectoryW(sys, MAX_PATH);
        GetCurrentDirectoryW(MAX_PATH, cur);
        logstr("w2atest: paths W: temp=");
        logw(tmp, 32); logstr(" win="); logw(win, 32);
        logstr(" sys="); logw(sys, 32); logstr(" cur="); logw(cur, 32);
        logstr("\n");
        if (lstrcmpW(tmp, L"C:\\TEMP") || lstrcmpW(win, L"C:\\WINDOWS") ||
            lstrcmpW(sys, L"C:\\WINDOWS\\System32") ||
            lstrcmpW(cur, L"C:\\MyOS")) {
            logstr("w2atest: FAIL paths W\n"); fails++;
        }
    }

    logstr("w2atest: m4 done\n");
    /* 5) GetEnvironmentVariableW */
    {
        wchar_t v[64];
        DWORD n = GetEnvironmentVariableW(L"PATH", v, 64);
        if (n == 0 || lstrcmpW(v, L".") != 0) {
            logstr("w2atest: FAIL GetEnvironmentVariableW\n"); fails++;
        }
        logstr("w2atest: envW PATH='"); logw(v, 16); logstr("'\n");
    }

    logstr("w2atest: m5 done\n");
    /* 6) lstr*W directos */
    {
        wchar_t buf[64];
        lstrcpyW(buf, L"Hello");
        lstrcatW(buf, L" World");
        if (lstrlenW(buf) != 11 || lstrcmpW(buf, L"Hello World") != 0 ||
            lstrcmpiW(buf, L"hello WORLD") != 0) {
            logstr("w2atest: FAIL lstr*W\n"); fails++;
        }
        logstr("w2atest: lstr*W ok\n");
    }

    logstr("w2atest: m6 done\n");
    /* 7) GetFullPathNameW + GetLogicalDriveStringsW */
    {
        wchar_t full[MAX_PATH], *fp, drv[8];
        DWORD n = GetFullPathNameW(L"metapad.exe", MAX_PATH, full, &fp);
        GetLogicalDriveStringsW(8, drv);
        logstr("w2atest: fullW='");
        logw(full, 40); logstr("' drvW='"); logw(drv, 4); logstr("'\n");
        if (n == 0 || lstrcmpW(full, L"metapad.exe") != 0 ||
            lstrcmpW(drv, L"C:\\") != 0) {
            logstr("w2atest: FAIL fullpath/drives W\n"); fails++;
        }
    }

    logstr("w2atest: m7 done\n");
    /* 8) CopyFileW + MoveFileW */
    {
        HANDLE h = CreateFileW(L"cf_src.txt", 0x40000000, 0, 0, 2, 0, 0);
        if (h != INVALID_HANDLE_VALUE) {
            static const char d[] = "copy me";
            DWORD wr = 0;
            WriteFile(h, d, sizeof(d) - 1, &wr, 0);
            CloseHandle(h);
        }
        if (!CopyFileW(L"cf_src.txt", L"cf_dst.txt", 0)) {
            logstr("w2atest: FAIL CopyFileW\n"); fails++;
        }
        if (!MoveFileW(L"cf_src.txt", L"cf_moved.txt")) {
            logstr("w2atest: FAIL MoveFileW\n"); fails++;
        }
        DeleteFileW(L"cf_src.txt");
        DeleteFileW(L"cf_dst.txt");
        DeleteFileW(L"cf_moved.txt");
        logstr("w2atest: copyw/movew ok\n");
    }

    logstr("w2atest: m8 done\n");
    /* 9) CreateDirectoryW + RemoveDirectoryW */
    {
        if (!CreateDirectoryW(L"w2adir", 0)) {
            logstr("w2atest: FAIL CreateDirectoryW\n"); fails++;
        }
        if (!RemoveDirectoryW(L"w2adir")) {
            logstr("w2atest: FAIL RemoveDirectoryW\n"); fails++;
        }
        logstr("w2atest: mkdirW/rmdirW ok\n");
    }

    logstr("w2atest: m9 done\n");
    /* 10) FormatMessageW / GetDateFormatW / GetLocaleInfoW */
    {
        wchar_t msg[128], dt[64], loc[64];
        DWORD n = FormatMessageW(0x1000, 0, 2, 0, msg, 128, 0);
        GetDateFormatW(0, 0, 0, 0, dt, 64);
        GetLocaleInfoW(0, 0x02, loc, 64);
        logstr("w2atest: fmtW n="); lognum(n);
        logstr(" msg='"); logw(msg, 32);
        logstr("' date='"); logw(dt, 32); logstr("' loc='"); logw(loc, 24);
        logstr("'\n");
        if (n == 0 || msg[0] == 0 || dt[0] == 0 || loc[0] == 0) {
            logstr("w2atest: FAIL fmt/date/locale W\n"); fails++;
        }
    }

    /* ============ Fase 25 paso 3: user32 W + msvcrt W ============ */

    /* 11) RegisterClassW + CreateWindowExW (clase W, top-level) */
    {
        WNDCLASSW wc;
        HWND h;
        memset(&wc, 0, sizeof(wc));
        wc.lpfnWndProc = DefWindowProcA;
        wc.lpszClassName = L"W2ACls";
        if (!RegisterClassW(&wc)) {
            logstr("w2atest: FAIL RegisterClassW\n"); fails++;
        } else {
            h = CreateWindowExW(0, L"W2ACls", L"w2a title", WS_OVERLAPPEDWINDOW,
                                50, 50, 300, 200, 0, 0, 0, 0);
            if (h == NULL) {
                logstr("w2atest: FAIL CreateWindowExW\n"); fails++;
            } else {
                DestroyWindow(h);
            }
            logstr("w2atest: regw/createw ok\n");
        }
    }

    /* 12) EDIT hijo: SetWindowTextW/GetWindowTextW/GetClassNameW/
     * SendMessageW(WM_SETTEXT/WM_GETTEXT) */
    {
        HWND par, edit;
        wchar_t buf[128];
        par = CreateWindowExW(0, L"W2ACls", L"parent", WS_OVERLAPPEDWINDOW,
                              20, 20, 320, 240, 0, 0, 0, 0);
        if (par == NULL) {
            logstr("w2atest: FAIL CreateWindowExW parent\n"); fails++;
        } else {
            edit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE,
                                   10, 10, 200, 20, (HWND)par, 0, 0, 0);
            if (edit == NULL) {
                logstr("w2atest: FAIL CreateWindowExW EDIT\n"); fails++;
            } else {
                if (!SetWindowTextW(edit, L"hello W")) {
                    logstr("w2atest: FAIL SetWindowTextW\n"); fails++;
                }
                if (GetWindowTextW(edit, buf, 128) == 0 ||
                    lstrcmpW(buf, L"hello W") != 0) {
                    logstr("w2atest: FAIL GetWindowTextW\n"); fails++;
                }
                if (GetWindowTextLengthW(edit) != 7) {
                    logstr("w2atest: FAIL GetWindowTextLengthW\n"); fails++;
                }
                if (GetClassNameW(edit, buf, 128) == 0 ||
                    lstrcmpW(buf, L"EDIT") != 0) {
                    logstr("w2atest: FAIL GetClassNameW\n"); fails++;
                }
                SendMessageW(edit, WM_SETTEXT, 0, (LPARAM)L"world");
                if (GetWindowTextW(edit, buf, 128) == 0 ||
                    lstrcmpW(buf, L"world") != 0) {
                    logstr("w2atest: FAIL SendMessageW WM_SETTEXT\n"); fails++;
                }
                SendMessageW(edit, WM_GETTEXT, 128, (LPARAM)buf);
                if (lstrcmpW(buf, L"world") != 0) {
                    logstr("w2atest: FAIL SendMessageW WM_GETTEXT\n"); fails++;
                }
                logstr("w2atest: settextW/gettextW/classW/sendW ok\n");
            }
            DestroyWindow(par);
        }
    }

    /* 13) CharUpperW/CharLowerW/CharNextW */
    {
        wchar_t s[16];
        lstrcpyW(s, L"AbC dEf");
        CharUpperW(s);
        if (lstrcmpW(s, L"ABC DEF") != 0) {
            logstr("w2atest: FAIL CharUpperW\n"); fails++;
        }
        CharLowerW(s);
        if (lstrcmpW(s, L"abc def") != 0) {
            logstr("w2atest: FAIL CharLowerW\n"); fails++;
        }
        if (CharUpperW((LPWSTR)(DWORD_PTR)'z') != (LPWSTR)(DWORD_PTR)'Z' ||
            CharLowerW((LPWSTR)(DWORD_PTR)'Q') != (LPWSTR)(DWORD_PTR)'q') {
            logstr("w2atest: FAIL CharUpperW/LowerW char\n"); fails++;
        }
        if (CharNextW((LPCWSTR)(DWORD_PTR)s) != (LPCWSTR)(s + 1) ||
            CharNextW((LPCWSTR)(DWORD_PTR)(s + 7)) != NULL) {
            logstr("w2atest: FAIL CharNextW\n"); fails++;
        }
        logstr("w2atest: charW ok\n");
    }

    /* 14) msvcrt W: wcscpy/wcscat/wcsstr/_wcsicmp/_wtoi/_itow/_wcslwr */
    {
        wchar_t s[64];
        wcscpy(s, L"caf");
        wcscat(s, L"eteria");
        if (wcscmp(s, L"cafeteria") != 0 ||
            wcsstr(s, L"teria") != s + 4 ||
            _wcsicmp(L"CAFETERIA", L"cafeteria") != 0 ||
            _wcsnicmp(L"ABCX", L"abcY", 3) != 0) {
            logstr("w2atest: FAIL wcscat/wcsstr/_wcsicmp\n"); fails++;
        }
        if (wcslen(L"hola") != 4 || _wtoi(L"-42") != -42 ||
            _wtoi(L"12ab") != 12 || _wtol(L"99") != 99) {
            logstr("w2atest: FAIL _wtoi\n"); fails++;
        }
        _itow(1234, s, 10);
        if (wcscmp(s, L"1234") != 0) {
            logstr("w2atest: FAIL _itow\n"); fails++;
        }
        _wcslwr(s);
        _itow(1234, s, 10);
        if (wcscmp(s, L"1234") != 0) {
            logstr("w2atest: FAIL _wcslwr\n"); fails++;
        }
        logstr("w2atest: msvcrt W ok\n");
    }

    /* 15) _wgetmainargs: argv W de la linea de comandos real */
    {
        int argc = 0, dontfree = 0, mode = 0;
        wchar_t **argv = 0, **envp = 0;
        wgetmainargs_fn wget = (wgetmainargs_fn)GetProcAddress(
            GetModuleHandleA("msvcrt.dll"), "_wgetmainargs");
        if (wget == NULL) {
            logstr("w2atest: FAIL _wgetmainargs (no resuelto)\n"); fails++;
        } else if (wget(&argc, &argv, &envp, &dontfree, &mode) != 0 ||
            argc < 1 || argv == 0 || argv[0] == 0 ||
            lstrcmpW(argv[0], L"w2atest.exe") != 0 || argv[argc] != 0) {
            logstr("w2atest: FAIL _wgetmainargs\n"); fails++;
        } else {
            logstr("w2atest: wgetmainargs argc="); lognum(argc);
            logstr(" argv0='"); logw(argv[0], 32); logstr("'\n");
        }
    }

    if (fails) {
        logstr("w2atest:FAIL\n");
        return 1;
    }
    logstr("w2atest:PASS\n");
    return 0;
}