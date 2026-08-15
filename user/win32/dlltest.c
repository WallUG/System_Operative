/* MyOS - user/win32/dlltest.c
 * Prueba de las DLLs de la Fase 23-B8: OLE32 (CoInitialize),
 * SHLWAPI (StrStrI/StrCmpI/PathFileExists/PathFindFileName/
 * PathRemoveFileSpec) y WINSPOOL (impresion a print.txt).
 * Imprime los resultados al serial (stdout del .exe). */

#include <windows.h>
#include <shlwapi.h>
#include <winspool.h>

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

int main(void)
{
    char pathbuf[64];
    const char *p;
    DWORD written = 0;
    HANDLE printer = 0;

    logstr("d8: start\n");

    /* --- OLE32 --- */
    logstr("d8: coinit=");
    lognum(CoInitialize(0));
    logstr("\n");
    CoUninitialize();
    logstr("d8: couninit ok\n");

    /* --- SHLWAPI --- */
    logstr("d8: strstri=");
    p = StrStrI("Hello World", "WORLD");
    lognum(p ? (int)(p - "Hello World") : -1);
    logstr("\n");
    logstr("d8: strcmpi=");
    lognum(StrCmpI("HELLO", "hello"));
    logstr("\n");
    logstr("d8: pathfile_mp=");
    lognum(PathFileExists("metapad.exe"));
    logstr(" pathfile_no=");
    lognum(PathFileExists("noexiste.bin"));
    logstr("\n");
    logstr("d8: findname=");
    p = PathFindFileName("C:\\dirs\\sub\\archivo.txt");
    logstr(p ? p : "(null)");
    logstr("\n");
    {
        int i = 0;
        while (i < 63 && "C:\\dirs\\sub\\archivo.txt"[i]) {
            pathbuf[i] = "C:\\dirs\\sub\\archivo.txt"[i];
            i++;
        }
        pathbuf[i] = 0;
    }
    logstr("d8: rmspec=");
    lognum(PathRemoveFileSpec(pathbuf));
    logstr(" dir=");
    logstr(pathbuf);
    logstr("\n");

    /* --- WINSPOOL: imprimir a print.txt --- */
    logstr("d8: openprinter=");
    lognum(OpenPrinterA("Archivo", &printer, 0));
    logstr("\n");
    logstr("d8: startdoc=");
    lognum(StartDocPrinterA(printer, 1, 0));
    logstr("\n");
    StartPagePrinter(printer);
    logstr("d8: writeprinter=");
    lognum(WritePrinter(printer, "Hola impresora!\n", 16, &written));
    logstr(" written=");
    lognum(written);
    logstr("\n");
    EndPagePrinter(printer);
    logstr("d8: enddoc=");
    lognum(EndDocPrinter(printer));
    logstr("\n");
    logstr("d8: closeprinter=");
    lognum(ClosePrinter(printer));
    logstr("\n");

    logstr("d8: fin\n");
    return 0;
}