/* MyOS - user/win32/wintwo.c
 * Prueba del message loop real (Fase 23-B6): dos ventanas top-level
 * con wndprocs distintos; un clic en cada una debe llegar a su propio
 * wndproc (WM_LBUTTONDOWN). Tras 2 clics se cierra con PostQuitMessage
 * y GetMessageA devuelve 0 -> WM_QUIT. */
#include <windows.h>

static int clicks;
static void logstr(const char *s)
{
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD n = 0;
    int len = 0;
    while (s[len]) len++;
    WriteFile(h, s, len, &n, 0);
}

static LRESULT CALLBACK WndProcA(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)hwnd; (void)wp; (void)lp;
    if (msg == WM_LBUTTONDOWN) {
        logstr("A: click\n");
        clicks++;
        if (clicks == 2)
            PostQuitMessage(0);
        return 0;
    }
    if (msg == WM_CLOSE) {
        logstr("A: WM_CLOSE\n");
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

static LRESULT CALLBACK WndProcB(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)hwnd; (void)wp; (void)lp;
    if (msg == WM_LBUTTONDOWN) {
        logstr("B: click\n");
        clicks++;
        if (clicks == 2)
            PostQuitMessage(0);
        return 0;
    }
    if (msg == WM_CLOSE) {
        logstr("B: WM_CLOSE\n");
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

int main(void)
{
    WNDCLASS wc;
    HWND hw1, hw2;
    MSG msg;

    logstr("wintwo: creando ventanas\n");

    wc.style = 0;
    wc.lpfnWndProc = WndProcA;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = GetModuleHandleA(0);
    wc.hIcon = 0;
    wc.hCursor = 0;
    wc.hbrBackground = 0;
    wc.lpszMenuName = 0;
    wc.lpszClassName = "WndA";
    if (!RegisterClassA(&wc)) {
        logstr("wintwo: RegisterClassA A fallo\n");
        return 1;
    }
    wc.lpfnWndProc = WndProcB;
    wc.lpszClassName = "WndB";
    if (!RegisterClassA(&wc)) {
        logstr("wintwo: RegisterClassA B fallo\n");
        return 1;
    }

    hw1 = CreateWindowExA(0, "WndA", "Ventana A", 0,
                          60, 60, 400, 300, 0, 0, 0, 0);
    hw2 = CreateWindowExA(0, "WndB", "Ventana B", 0,
                          340, 140, 400, 300, 0, 0, 0, 0);
    if (!hw1 || !hw2) {
        logstr("wintwo: CreateWindowExA fallo\n");
        return 1;
    }
    logstr("wintwo: ventanas creadas\n");
    ShowWindow(hw1, 1);
    ShowWindow(hw2, 1);

    while (GetMessageA(&msg, 0, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    logstr("wintwo: loop terminado (WM_QUIT)\n");
    return 0;
}