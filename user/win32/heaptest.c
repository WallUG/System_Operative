/* MyOS - user/win32/heaptest.c
 * Prueba del heap por proceso (Fase 23-C10): HeapAlloc/HeapFree/
 * HeapReAlloc/HeapSize reutilizan huecos (free real) y el coalesce
 * fusiona bloques contiguos; malloc/free/realloc del CRT (msvcrt)
 * sobre el mismo heap. Imprime resultados al serial. */

#include <windows.h>
#include <stdlib.h>
#include <string.h>

static void logstr(const char *s)
{
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD n = 0;
    int len = 0;
    while (s[len]) len++;
    WriteFile(h, s, len, &n, 0);
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

static void lognum(int v)
{
    char b[12];
    int p = 0, u = v < 0 ? -v : v;
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (v < 0) { char c = '-'; WriteFile(h, &c, 1, &(DWORD){0}, 0); }
    do { b[p++] = (char)('0' + u % 10); u /= 10; } while (u);
    while (p > 0) WriteFile(h, &b[--p], 1, &(DWORD){0}, 0);
}

int main(void)
{
    void *a, *b, *c, *d;
    HANDLE hp;

    logstr("h10: start\n");
    hp = GetProcessHeap();

    /* 1) alloc/free/realloc/HeapSize */
    a = HeapAlloc(hp, 0, 100);
    if (!a) { logstr("h10: alloc1 fallo\n"); return 1; }
    logstr("h10: a=");
    loghex((unsigned int)a);
    logstr(" size=");
    lognum(HeapSize(hp, 0, a));
    logstr("\n");

    /* 2) free + realloc: el hueco se reutiliza con el mismo puntero */
    b = HeapAlloc(hp, 0, 200);
    if (!b) { logstr("h10: alloc2 fallo\n"); return 1; }
    HeapFree(hp, 0, b);
    c = HeapAlloc(hp, 0, 200);          /* mismo tamano: mismo hueco */
    logstr("h10: b=");
    loghex((unsigned int)b);
    logstr(" c=");
    loghex((unsigned int)c);
    logstr("\n");

    /* 3) coalesce: 2 bloques contiguos libres se fusionan */
    HeapFree(hp, 0, c);
    d = HeapAlloc(hp, 0, 400);          /* cabe solo si hay 400 libres
                                         * seguidos (100+200+header) */
    logstr("h10: d=");
    loghex((unsigned int)d);
    logstr("\n");
    HeapFree(hp, 0, d);

    /* 4) HeapReAlloc preserva el contenido */
    a = HeapAlloc(hp, 0, 64);
    if (!a) { logstr("h10: ralloc base fallo\n"); return 1; }
    strcpy((char *)a, "contenido preservado");
    b = HeapReAlloc(hp, 0, a, 300);
    logstr("h10: realloc=");
    loghex((unsigned int)b);
    logstr(" txt=");
    logstr((const char *)b);
    logstr("\n");
    HeapFree(hp, 0, b);

    /* 5) malloc/free del CRT (msvcrt): free real + reutilizacion */
    a = malloc(100);
    if (!a) { logstr("h10: malloc1 fallo\n"); return 1; }
    logstr("h10: m1=");
    loghex((unsigned int)a);
    logstr("\n");
    free(a);
    b = malloc(100);
    logstr("h10: m2=");
    loghex((unsigned int)b);
    logstr("\n");
    free(b);

    /* 6) realloc del CRT preserva contenido */
    a = malloc(16);
    strcpy((char *)a, "crt realloc");
    b = realloc(a, 512);
    logstr("h10: r=");
    loghex((unsigned int)b);
    logstr(" txt=");
    logstr((const char *)b);
    logstr("\n");
    free(b);

    logstr("h10: fin\n");
    return 0;
}