/* MyOS - user/win32/bootpaths.c
 * Fase 24-P1.4: kernel32 de arranque. Verifica GetTempPathA,
 * GetWindowsDirectoryA, GetSystemDirectoryA, GetVersionExA (6.1) y
 * GetFileInformationByHandle (tamano del archivo). exit 0 si ok. */
#include <windows.h>

static void logmsg(const char *s)
{
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD n = 0;
    int len = 0;
    while (s[len]) len++;
    WriteFile(h, s, len, &n, 0);
}

static void lognum(unsigned v)
{
    char b[12];
    int p = 0;
    do { b[p++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (p > 0) { char c = b[--p];
        WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), &c, 1, &(DWORD){0}, 0); }
}

static int eq(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static void sys_exit(int code)
{
    __asm__ volatile("int $0x80" : : "a"(2), "b"(code) : "memory");
}

int main(void)
{
    char buf[260];
    OSVERSIONINFOA vi;
    BY_HANDLE_FILE_INFORMATION fi;
    HANDLE fh;
    int ok = 1;

    GetTempPathA(sizeof buf, buf);
    logmsg("boot: temp="); logmsg(buf); logmsg("\n");
    ok = ok && eq(buf, "C:\\TEMP");

    GetWindowsDirectoryA(buf, sizeof buf);
    logmsg("boot: win="); logmsg(buf); logmsg("\n");
    ok = ok && eq(buf, "C:\\WINDOWS");

    GetSystemDirectoryA(buf, sizeof buf);
    logmsg("boot: sys="); logmsg(buf); logmsg("\n");
    ok = ok && eq(buf, "C:\\WINDOWS\\System32");

    vi.dwOSVersionInfoSize = sizeof vi;
    GetVersionExA(&vi);
    logmsg("boot: ver=");
    lognum((unsigned)vi.dwMajorVersion); logmsg(".");
    lognum((unsigned)vi.dwMinorVersion); logmsg("\n");
    ok = ok && (vi.dwMajorVersion == 6 && vi.dwMinorVersion == 1);

    fh = CreateFileA("readme.txt", GENERIC_READ, 0, 0, OPEN_EXISTING, 0, 0);
    if (fh != INVALID_HANDLE_VALUE &&
        GetFileInformationByHandle(fh, &fi)) {
        logmsg("boot: size=");
        lognum((unsigned)fi.nFileSizeLow);
        logmsg("\n");
        ok = ok && (fi.nFileSizeLow > 0);
        CloseHandle(fh);
    } else {
        logmsg("boot: GetFileInformationByHandle FALLO\n");
        ok = 0;
    }

    logmsg("boot: fin ok=");
    lognum((unsigned)ok);
    logmsg("\n");
    sys_exit(ok ? 0 : 1);
    return 0;
}