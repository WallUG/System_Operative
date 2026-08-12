/* MyOS - user/win32/gdi_demo.c
 * Prueba del GDI de dibujo (Fase 18 / Hito B slice 2): ventana que pinta
 * con GDI real (FillRect, Rectangle, MoveToEx/LineTo, TextOutA) en su
 * WM_PAINT. Compila con i686-w64-mingw32-gcc -luser32 -lgdi32.
 */

#include <windows.h>

static void paint_demo(HDC hdc)
{
    HBRUSH blue;
    HPEN pen;
    TEXTMETRICA tm;

    /* rectangulo azul relleno, interior gris claro (pincel stock) */
    blue = CreateSolidBrush(RGB(0x40, 0x60, 0xC0));
    SelectObject(hdc, blue);
    Rectangle(hdc, 20, 20, 180, 100);
    FillRect(hdc, &(RECT){ 30, 30, 170, 90 }, (HBRUSH)(LTGRAY_BRUSH + 1));

    /* linea diagonal roja (boligrafo creado) */
    pen = CreatePen(PS_SOLID, 2, RGB(0xC0, 0x20, 0x20));
    SelectObject(hdc, pen);
    MoveToEx(hdc, 20, 120, NULL);
    LineTo(hdc, 180, 200);
    DeleteObject(pen);

    /* texto de prueba con el DC */
    SetBkMode(hdc, OPAQUE);
    SetBkColor(hdc, RGB(0xF0, 0xF0, 0xF0));
    SetTextColor(hdc, RGB(0x00, 0x00, 0x00));
    GetTextMetricsA(hdc, &tm);
    TextOutA(hdc, 20, 220, "Hola GDI (8x16, alineado arriba)", 32);

    DeleteObject(blue);
}

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT m, WPARAM wp, LPARAM lp)
{
    PAINTSTRUCT ps;
    (void)wp; (void)lp;
    switch (m) {
    case WM_PAINT:
        BeginPaint(hwnd, &ps);
        paint_demo(ps.hdc);
        EndPaint(hwnd, &ps);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd, m, wp, lp);
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    WNDCLASSA wc;
    HWND hwnd;
    MSG msg;
    (void)prev; (void)cmd; (void)show;

    wc.style = 0;
    wc.lpfnWndProc = wnd_proc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = inst;
    wc.hIcon = NULL;
    wc.hCursor = NULL;
    wc.hbrBackground = NULL;
    wc.lpszMenuName = NULL;
    wc.lpszClassName = "gdi_demo";
    if (!RegisterClassA(&wc))
        return 1;

    hwnd = CreateWindowExA(0, "gdi_demo", "GDI Demo MyOS",
                           WS_OVERLAPPEDWINDOW, 100, 60, 360, 300,
                           NULL, NULL, inst, NULL);
    if (!hwnd)
        return 2;
    ShowWindow(hwnd, SW_SHOWNORMAL);
    UpdateWindow(hwnd);
    while (GetMessageA(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return (int)msg.wParam;
}