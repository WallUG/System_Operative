/* MyOS - user/win32/txtmode.c
 * Prueba de modos binario/texto (Fase 23-C9): _O_TEXT traduce LF->CRLF
 * al escribir y CRLF->LF al leer; _O_BINARY (default) pasa crudo.
 * Imprime resultados al serial (stdout del .exe). */

#include <windows.h>
#include <fcntl.h>
#include <io.h>

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
    char b[12];
    int p = 0, u = v < 0 ? -v : v;
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (v < 0) { char c = '-'; WriteFile(h, &c, 1, &(DWORD){0}, 0); }
    do { b[p++] = (char)('0' + u % 10); u /= 10; } while (u);
    while (p > 0) WriteFile(h, &b[--p], 1, &(DWORD){0}, 0);
}

static int cr_count(const char *buf, int n)
{
    int c = 0, i;
    for (i = 0; i < n; i++)
        if (buf[i] == '\r') c++;
    return c;
}

int main(void)
{
    HANDLE h;
    DWORD wr = 0, rd = 0;
    char buf[64];
    int fd, n;

    logstr("t9: start\n");

    /* 1) binario (default): "Hola\nMundo\n" se guarda con LF crudo */
    h = CreateFileA("t9_a.txt", GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
    if (h == INVALID_HANDLE_VALUE) { logstr("t9: A create fallo\n"); return 1; }
    WriteFile(h, "Hola\nMundo\n", 11, &wr, 0);
    CloseHandle(h);
    h = CreateFileA("t9_a.txt", GENERIC_READ, 0, 0, OPEN_EXISTING, 0, 0);
    ReadFile(h, buf, 64, &rd, 0);
    CloseHandle(h);
    logstr("t9: A bin w=");
    lognum(wr);
    logstr(" rd=");
    lognum(rd);
    logstr(" cr=");
    lognum(cr_count(buf, (int)rd));
    logstr("\n");

    /* 2) texto (_O_TEXT): "Linea1\nLinea2\n" se guarda como CRLF */
    h = CreateFileA("t9_b.txt", GENERIC_WRITE, 0, 0, CREATE_ALWAYS,
                    _O_TEXT, 0);
    if (h == INVALID_HANDLE_VALUE) { logstr("t9: B create fallo\n"); return 1; }
    WriteFile(h, "Linea1\nLinea2\n", 14, &wr, 0);
    CloseHandle(h);
    h = CreateFileA("t9_b.txt", GENERIC_READ, 0, 0, OPEN_EXISTING, 0, 0);
    rd = 0;
    ReadFile(h, buf, 64, &rd, 0);
    CloseHandle(h);
    logstr("t9: B txt escrito, raw cr=");
    lognum(cr_count(buf, (int)rd));
    logstr(" rd=");
    lognum(rd);
    logstr("\n");

    /* 3) re-leer t9_b.txt en modo texto: CRLF -> LF (0 CRs) */
    h = CreateFileA("t9_b.txt", GENERIC_READ, 0, 0, OPEN_EXISTING,
                    _O_TEXT, 0);
    rd = 0;
    ReadFile(h, buf, 64, &rd, 0);
    CloseHandle(h);
    logstr("t9: B txt leido cr=");
    lognum(cr_count(buf, (int)rd));
    logstr(" len=");
    lognum(rd);
    logstr(" data=[");
    logstr(buf);
    logstr("]");
    logstr(" match=");
    lognum(rd == 14 && cr_count(buf, 14) == 0 &&
           buf[0] == 'L' && buf[6] == '\n' && buf[13] == '\n' ? 1 : 0);
    logstr("\n");

    /* 4) _open/_write/_read con _O_TEXT (CRT low-level) */
    fd = _open("t9_c.txt", _O_WRONLY | _O_CREAT | _O_TRUNC | _O_TEXT, 0644);
    if (fd < 0) { logstr("t9: C open fallo\n"); return 1; }
    n = _write(fd, "X\nY\n", 4);
    _close(fd);
    fd = _open("t9_c.txt", _O_RDONLY | _O_TEXT, 0);
    rd = 0;
    n = _read(fd, buf, 64);
    _close(fd);
    logstr("t9: C text read n=");
    lognum(n);
    logstr(" cr=");
    lognum(cr_count(buf, n));
    logstr(" match=");
    lognum(n == 4 && buf[0] == 'X' && buf[2] == 'Y' ? 1 : 0);
    logstr("\n");

    /* 5) _open binario sobre t9_c.txt: ve CRLF crudo */
    fd = _open("t9_c.txt", _O_RDONLY | _O_BINARY, 0);
    n = _read(fd, buf, 64);
    _close(fd);
    logstr("t9: C bin n=");
    lognum(n);
    logstr(" cr=");
    lognum(cr_count(buf, n));
    logstr("\n");

    logstr("t9: fin\n");
    return 0;
}