/* MyOS - user/win32/w2demo.c
 * Fase 25-W2A paso 4: binario objetivo de prueba compilado con
 * -DUNICODE -D_UNICODE: TODAS las llamadas son versiones W (el mismo
 * patron de imports de un notepad.exe real de MSVC: RegisterClassW,
 * CreateWindowExW, GetMessageW, DispatchMessageW, DefWindowProcW,
 * SendMessageW, SetWindowTextW, GetWindowTextW, LoadStringW,
 * CharNextW, CreateFileW, ReadFile/WriteFile, GetModuleFileNameW...).
 * Mini-notepad: ventana con EDIT hijo, teclas -> WM_CHAR, Ctrl+S
 * guarda, Ctrl+O abre, Esc o 'q' sale. Imprime hitos al serial. */

#define UNICODE
#define _UNICODE
#include <windows.h>
#include <string.h>

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

#define MAXBUF 4096
static wchar_t doc[MAXBUF];       /* el documento */
static int doclen = 0;
static HWND hedit;
static WCHAR fname[MAX_PATH];

static void save_doc(void)
{
    HANDLE h;
    DWORD wr = 0;
    char tmp[MAXBUF];
    int i, n;

    /* sincroniza el documento desde el EDIT (sin EN_CHANGE real) */
    n = GetWindowTextW(hedit, doc, MAXBUF);
    if (n >= 0) {
        doclen = n;
        doc[n] = 0;
    }
    h = CreateFileW(fname, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
    if (h == INVALID_HANDLE_VALUE) {
        logstr("w2demo: save FAIL\n");
        return;
    }
    /* documento W -> A (cp437 por el thunk interno del FS) */
    for (i = 0; i < doclen; i++)
        tmp[i] = (char)(doc[i] & 0xFF);
    tmp[doclen] = 0;
    if (!WriteFile(h, tmp, doclen, &wr, 0) || wr != (DWORD)doclen)
        logstr("w2demo: save WRITE FAIL\n");
    CloseHandle(h);
    logstr("w2demo: saved "); lognum(doclen); logstr(" bytes\n");
}

static void open_doc(void)
{
    HANDLE h = CreateFileW(fname, GENERIC_READ, 0, 0, OPEN_EXISTING, 0, 0);
    DWORD rd = 0;
    char tmp[MAXBUF];
    int i;
    if (h == INVALID_HANDLE_VALUE) {
        logstr("w2demo: open FAIL\n");
        return;
    }
    rd = 0;
    if (!ReadFile(h, tmp, MAXBUF - 1, &rd, 0))
        rd = 0;
    CloseHandle(h);
    for (i = 0; i < (int)rd; i++)
        doc[i] = (WCHAR)(unsigned char)tmp[i];
    doclen = (int)rd;
    doc[doclen] = 0;
    SetWindowTextW(hedit, doc);
    logstr("w2demo: opened "); lognum(doclen); logstr(" bytes\n");
}

static LRESULT CALLBACK wndproc(HWND hwnd, UINT m, WPARAM a, LPARAM b)
{
    switch (m) {
    case WM_CREATE:
        logstr("w2demo: WM_CREATE\n");
        hedit = CreateWindowExW(0, L"EDIT", L"",
                                WS_CHILD | WS_VISIBLE | WS_VSCROLL |
                                ES_MULTILINE | ES_AUTOVSCROLL,
                                0, 0, 10, 10, hwnd, (HMENU)1, 0, 0);
        if (hedit == NULL) {
            logstr("w2demo: FAIL edit create\n");
            return -1;
        }
        SetWindowTextW(hedit, L"");
        return 0;
    case WM_SIZE:
        MoveWindow(hedit, 0, 0, LOWORD(b), HIWORD(b), TRUE);
        return 0;
    case WM_COMMAND:
        if (LOWORD(a) == 1 && HIWORD(a) == 0) {
            /* EN_CHANGE del EDIT: sincroniza el documento */
            int n = GetWindowTextW(hedit, doc, MAXBUF);
            if (n >= 0) {
                doclen = n;
                doc[n] = 0;
            }
            return 0;
        }
        break;
    case WM_CHAR:
        logstr("w2demo: WM_CHAR '");
        {
            char c = (char)a;
            WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), &c, 1, &(DWORD){0}, 0);
        }
        logstr("'\n");
        if (a == 27) {              /* Esc */
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
            return 0;
        }
        return DefWindowProcW(hwnd, m, a, b);
    case WM_CLOSE:
        logstr("w2demo: WM_CLOSE\n");
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        logstr("w2demo: WM_DESTROY\n");
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, m, a, b);
}

int WINAPI wWinMain(HINSTANCE hinst, HINSTANCE prev, LPWSTR cmdline, int show)
{
    WNDCLASSW wc;
    HWND hwnd;
    MSG msg;
    HACCEL acc;
    wchar_t buf[128];
    int i;

    (void)hinst; (void)prev; (void)cmdline; (void)show;

    logstr("w2demo: start\n");

    /* nombre del modulo (GetModuleFileNameW) */
    if (GetModuleFileNameW(0, fname, MAX_PATH) == 0) {
        logstr("w2demo: FAIL GetModuleFileNameW\n");
        return 1;
    }
    for (i = 0; fname[i] && fname[i] != '.'; i++) ;
    if (i >= 3 && fname[i - 3] == 't' && fname[i - 2] == 'x' &&
        fname[i - 1] == 't')
        fname[i] = 0;               /* demote.txt -> demote */
    else
        fname[0] = 0;
    if (fname[0] == 0) {
        static const WCHAR demo[] = { 'd','e','m','o','.','t','x','t',0 };
        for (i = 0; demo[i]; i++)
            fname[i] = demo[i];
        fname[i] = 0;
    }

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = wndproc;
    wc.hInstance = hinst;
    wc.lpszClassName = L"NotepadW";
    if (!RegisterClassW(&wc)) {
        logstr("w2demo: FAIL RegisterClassW\n");
        return 1;
    }

    hwnd = CreateWindowExW(0, L"NotepadW", L"w2demo - notepad W",
                           WS_OVERLAPPEDWINDOW,
                           CW_USEDEFAULT, CW_USEDEFAULT, 480, 360,
                           NULL, NULL, hinst, NULL);
    if (hwnd == NULL) {
        logstr("w2demo: FAIL CreateWindowExW\n");
        return 1;
    }
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    acc = LoadAcceleratorsW(hinst, L"ACC");
    if (acc != NULL)
        logstr("w2demo: LoadAcceleratorsW ok (recurso)\n");

    /* GetClassNameW + CharNextW de cortesia */
    if (GetClassNameW(hwnd, buf, 128) == 0 ||
        CharNextW(buf) != buf + 1) {
        logstr("w2demo: FAIL GetClassNameW\n");
        return 1;
    }
    logstr("w2demo: classW='");
    {
        char t[64];
        for (i = 0; i < 63 && buf[i]; i++)
            t[i] = (char)buf[i];
        t[i] = 0;
        logstr(t);
    }
    logstr("'\n");

    logstr("w2demo: loop\n");
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (msg.message == WM_KEYDOWN || msg.message == WM_CHAR) {
            logstr("w2demo: msg m="); lognum(msg.message);
            logstr(" w="); lognum(msg.wParam); logstr("\n");
        }
        if (msg.message == WM_KEYDOWN && (msg.wParam == 'S' || msg.wParam == 's') &&
            (GetKeyState(VK_CONTROL) & 0x8000)) {
            save_doc();
            continue;
        }
        if (msg.message == WM_KEYDOWN && (msg.wParam == 'O' || msg.wParam == 'o') &&
            (GetKeyState(VK_CONTROL) & 0x8000)) {
            open_doc();
            continue;
        }
        if (msg.message == WM_KEYDOWN && (msg.wParam == 'Q' || msg.wParam == 'q')) {
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
            continue;
        }
        if (msg.message == WM_CHAR && msg.wParam == 27) {   /* Esc */
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
            continue;
        }
        if (msg.message == WM_KEYDOWN && msg.wParam == 27) {   /* Esc */
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
            continue;
        }
        if (acc != NULL && TranslateAcceleratorW(hwnd, acc, &msg))
            continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    logstr("w2demo: exit\n");
    return 0;
}