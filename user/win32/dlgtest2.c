/* MyOS - user/win32/dlgtest2.c
 * Fase 24-P1.2: DialogBoxParamA con un EDIT control real. El dialogo
 * tiene un EDITTEXT (IDC_EDIT) y un DEFPUSHBUTTON OK. Se escriben 3
 * caracteres (ABC) que deben ir al EDIT enfocado, y al pulsar Enter
 * (IDOK) el DlgProc lee GetDlgItemTextA y verifica que sea "ABC".
 * EndDialog devuelve IDOK solo si coincide (exit 0). */
#include <windows.h>

#define IDD_DLG  201
#define IDC_EDIT 203

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

static BOOL CALLBACK DlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)lp;
    switch (msg) {
    case WM_INITDIALOG:
        logmsg("dlg2: WM_INITDIALOG\n");
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK) {
            char buf[64];
            int n = GetDlgItemTextA(hwnd, IDC_EDIT, buf, sizeof buf);
            logmsg("dlg2: leido ");
            lognum((unsigned)n);
            logmsg(" = ");
            logmsg(buf);
            logmsg("\n");
            EndDialog(hwnd, (n == 3 && buf[0] == 'a' && buf[1] == 'b'
                             && buf[2] == 'c') ? IDOK : 0);
            return TRUE;
        }
        break;
    case WM_CLOSE:
        logmsg("dlg2: WM_CLOSE -> EndDialog\n");
        EndDialog(hwnd, 0);
        return TRUE;
    }
    return FALSE;
}

int main(void)
{
    int r;
    logmsg("dlg2: abriendo dialogo con edit\n");
    r = DialogBoxParamA(GetModuleHandleA(0), (LPCSTR)IDD_DLG, 0, DlgProc, 0);
    logmsg("dlg2: devolvio ");
    lognum((unsigned)r);
    logmsg("\n");
    return (r == IDOK) ? 0 : 1;
}