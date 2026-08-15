/* MyOS - user/win32/regtest.c
 * Fase 24-P1.3: registro persistente. RegCreateKeyExA + RegSetValueExA +
 * RegQueryValueExA + RegOpenKeyExA sobre advapi32.
 * - Ejecucion 1 (sin registry.ini): escribe AppName="Hello" y Count=42,
 *   los relee y verifica (round-trip en memoria + persistencia a .ini).
 * - Ejecucion 2 (registry.ini ya existe): lee Count=42 persistido.
 * - Siempre: RegOpenKeyExA de la clave debe tener exito. exit 0 si ok. */
#include <windows.h>

#define KEY_READ      0x20019
#define KEY_ALL_ACCESS 0xF003F
#define REG_SZ   1
#define REG_DWORD 4

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

static void sys_exit(int code)
{
    __asm__ volatile("int $0x80" : : "a"(2), "b"(code) : "memory");
}

int main(void)
{
    HKEY hk = 0, hk2 = 0;
    DWORD type = 0, size = 0, count = 0;
    char buf[64];
    int ok = 1;

    if (RegCreateKeyExA(HKEY_CURRENT_USER, "Software\\MyOS\\Test",
                        0, 0, 0, KEY_ALL_ACCESS, 0, &hk, 0) != 0) {
        logmsg("reg: RegCreateKeyExA fallo\n");
        sys_exit(1);
    }

    size = sizeof count;
    if (RegQueryValueExA(hk, "Count", 0, &type, (LPBYTE)&count, &size)
        == 2 /* ERROR_FILE_NOT_FOUND */) {
        logmsg("reg: run 1 (sin registro previo)\n");
        RegSetValueExA(hk, "AppName", 0, REG_SZ, (const BYTE*)"Hello", 6);
        count = 42;
        RegSetValueExA(hk, "Count", 0, REG_DWORD, (const BYTE*)&count, 4);
        size = sizeof buf; buf[0] = 0;
        RegQueryValueExA(hk, "AppName", 0, &type, (LPBYTE)buf, &size);
        size = sizeof count;
        RegQueryValueExA(hk, "Count", 0, &type, (LPBYTE)&count, &size);
        logmsg("reg: AppName=");
        logmsg(buf);
        logmsg(" Count=");
        lognum((unsigned)count);
        logmsg("\n");
        ok = (count == 42 && buf[0] == 'H');
    } else {
        size = sizeof count;
        RegQueryValueExA(hk, "Count", 0, &type, (LPBYTE)&count, &size);
        logmsg("reg: run 2 (persistido) Count=");
        lognum((unsigned)count);
        logmsg("\n");
        ok = (count == 42);
    }

    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\MyOS\\Test",
                      0, KEY_READ, &hk2) == 0) {
        logmsg("reg: open ok\n");
    } else {
        logmsg("reg: open FALLO\n");
        ok = 0;
    }

    if (hk) RegCloseKey(hk);
    if (hk2) RegCloseKey(hk2);

    logmsg("reg: fin ok=");
    lognum((unsigned)ok);
    logmsg("\n");
    sys_exit(ok ? 0 : 1);
    return 0;
}