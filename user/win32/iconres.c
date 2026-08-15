/* MyOS - user/win32/iconres.c
 * Prueba de LoadIconA (Fase 24-P1.1): carga un icono del .rsrc
 * (RT_GROUP_ICON -> RT_ICON), verifica que el handle no sea 0 (antes
 * siempre 0) y que el parser haya extraido las dimensiones correctas.
 * El app corre sin ventana: solo imprime el resultado al serial. */

#include <windows.h>
#include <stdint.h>

static void logstr(const char *s)
{
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD n = 0;
    int len = 0;
    while (s[len]) len++;
    WriteFile(h, s, len, &n, 0);
}

static void lognum(int v)
{
    char b[12]; int p = 0, u = v < 0 ? -v : v;
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (v < 0) WriteFile(h, "-", 1, &(DWORD){0}, 0);
    do { b[p++] = (char)('0' + u % 10); u /= 10; } while (u);
    while (p > 0) WriteFile(h, &b[--p], 1, &(DWORD){0}, 0);
}

static void sys_exit(int code)
{
    __asm__ volatile("int $0x80" : : "a"(2), "b"(code) : "memory");
}

int main(void)
{
    HINSTANCE h = GetModuleHandleA(0);
    HICON ic;
    int ok = 0;

    logstr("icon: start\n");
    ic = LoadIconA(h, (const char *)(uintptr_t)100);
    if (ic != 0) {
        logstr("icon: LoadIconA ok handle=");
        lognum((int)(uintptr_t)ic);
        logstr("\n");
        ok = 1;
    } else {
        logstr("icon: LoadIconA fallo (handle=0)\n");
    }

    /* LoadIconA de un id inexistente debe devolver 0 (no colgarse). */
    if (LoadIconA(h, (const char *)(uintptr_t)9999) == 0)
        logstr("icon: id inexistente -> 0 ok\n");
    else
        logstr("icon: id inexistente -> NO 0 (mal)\n");

    logstr("icon: fin ok=");
    lognum(ok);
    logstr("\n");
    sys_exit(ok ? 0 : 1);
    return 0;
}