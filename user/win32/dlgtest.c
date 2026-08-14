/* MyOS - user/win32/dlgtest.c
 * Prueba de DialogBoxParamA/EndDialog (Fase 23-B5): abre un dialogo
 * modal con un DEFPUSHBUTTON OK (id=IDOK=1) y, al pulsarlo, EndDialog
 * devuelve IDOK. Imprime WM_INITDIALOG y el resultado al consola. */
#include <windows.h>

#define IDD_DLG  101
#define IDC_TEXT 102

static char out[128];

static void logmsg(const char *s)
{
    /* WriteFile a stdout via GetStdHandle no esta garantizado en consola;
     * usamos una variable global que el kernel pueda volcar o simplemente
     * dejamos el flujo: el resultado de DialogBox se comprueba por la
     * salida de la app (consola). */
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
    while (p > 0) { char c = b[--p]; WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),
                                               &c, 1, &(DWORD){0}, 0); }
}

static BOOL CALLBACK DlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)lp;
    switch (msg) {
    case WM_INITDIALOG:
        logmsg("dlg: WM_INITDIALOG\n");
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK) {
            logmsg("dlg: WM_COMMAND IDOK -> EndDialog\n");
            EndDialog(hwnd, IDOK);
            return TRUE;
        }
        break;
    case WM_CLOSE:
        logmsg("dlg: WM_CLOSE -> EndDialog\n");
        EndDialog(hwnd, 0);
        return TRUE;
    }
    return FALSE;
}

int main(void)
{
    int r;
    logmsg("dlg: abriendo DialogBox...\n");
    r = DialogBoxParamA(GetModuleHandleA(0), (LPCSTR)IDD_DLG,
                        0, DlgProc, 0);
    logmsg("dlg: DialogBox devolvio ");
    lognum((unsigned)r);
    logmsg("\n");
    return (r == IDOK) ? 0 : 1;
}