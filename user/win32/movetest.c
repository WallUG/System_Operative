/* MyOS - user/win32/movetest.c
 * Prueba de CreateWindowExA extendido (Fase 23-B7): MoveWindow,
 * SetWindowText (WM_SETTEXT), GetWindowRect y WM_MOVE/WM_SIZE.
 * El wndproc imprime los mensajes al serial; tras las llamadas imprime
 * el rect via GetWindowRect y termina con PostQuitMessage. */
#include <windows.h>

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

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)hwnd;
    switch (msg) {
    case WM_MOVE:
        logstr("mv: WM_MOVE x=");
        lognum((short)LOWORD(lp));
        logstr(" y=");
        lognum((short)HIWORD(lp));
        logstr("\n");
        return 0;
    case WM_SIZE:
        logstr("mv: WM_SIZE w=");
        lognum(LOWORD(lp));
        logstr(" h=");
        lognum(HIWORD(lp));
        logstr("\n");
        return 0;
    case WM_SETTEXT:
        logstr("mv: WM_SETTEXT\n");
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

int main(void)
{
    WNDCLASS wc;
    HWND hw;
    MSG msg;
    RECT rc;

    logstr("mv: start\n");
    wc.style = 0;
    wc.lpfnWndProc = WndProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = GetModuleHandleA(0);
    wc.hIcon = 0;
    wc.hCursor = 0;
    wc.hbrBackground = 0;
    wc.lpszMenuName = 0;
    wc.lpszClassName = "MoveTest";
    RegisterClassA(&wc);

    hw = CreateWindowExA(0, "MoveTest", "Ventana movida", 0,
                         60, 60, 400, 300, 0, 0, 0, 0);
    if (!hw) { logstr("mv: create fallo\n"); return 1; }
    ShowWindow(hw, 1);

    logstr("mv: llamando MoveWindow(160,120)\n");
    MoveWindow(hw, 160, 120, 400, 300, 1);

    logstr("mv: llamando SetWindowText\n");
    SetWindowTextA(hw, (LPCSTR)"Nuevo titulo");

    GetWindowRect(hw, &rc);
    logstr("mv: esperando screendump\n");
    logstr("mv: rect=");
    lognum(rc.left); logstr(",");
    lognum(rc.top); logstr(",");
    lognum(rc.right); logstr(",");
    lognum(rc.bottom);
    logstr("\n");

    { volatile unsigned long n = 400000000;  /* ~10 s: ventana viva para el screendump */
      while (n-- > 0) ; }
    PostQuitMessage(0);
    while (GetMessageA(&msg, 0, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    logstr("mv: fin\n");
    return 0;
}