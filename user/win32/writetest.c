/* MyOS - Fase E: test de escritura Win32.
 * CreateFileA(GENERIC_WRITE) + WriteFile + CloseHandle sobre un archivo
 * nuevo, y relectura con ReadFile para verificar el contenido. Imprime el
 * resultado por consola (printf/WriteFile a stdout). */
#include <windows.h>
#include <stdio.h>

int main(void)
{
    const char *name = "saved.txt";
    const char *text = "escrito desde MyOS!\nsegunda linea\n";
    DWORD written = 0, read = 0;
    HANDLE h;
    char buf[128];

    printf("writetest: creando '%s'\n", name);

    /* GENERIC_WRITE, CREATE_ALWAYS (crea/trunca). */
    h = CreateFileA(name, GENERIC_WRITE, 0, 0, CREATE_ALWAYS,
                    0, 0);
    if (h == INVALID_HANDLE_VALUE) {
        printf("FALLO CreateFileA (write)\n");
        return 1;
    }
    if (!WriteFile(h, text, (DWORD)strlen(text), &written, 0)) {
        printf("FALLO WriteFile\n");
        CloseHandle(h);
        return 1;
    }
    printf("escribi %lu bytes\n", written);
    if (!SetEndOfFile(h)) {
        printf("FALLO SetEndOfFile\n");
        CloseHandle(h);
        return 1;
    }
    CloseHandle(h);

    /* releer para verificar */
    h = CreateFileA(name, GENERIC_READ, 0, 0, OPEN_EXISTING, 0, 0);
    if (h == INVALID_HANDLE_VALUE) {
        printf("FALLO CreateFileA (read)\n");
        return 1;
    }
    if (!ReadFile(h, buf, sizeof(buf), &read, 0)) {
        printf("FALLO ReadFile\n");
        CloseHandle(h);
        return 1;
    }
    buf[read] = 0;
    printf("leidos %lu bytes: %s", read, buf);
    CloseHandle(h);
    printf("OK\n");
    return 0;
}