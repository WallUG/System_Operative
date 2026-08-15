/* MyOS - user/win32/blitclip.c
 * Fase 24-P2.3: GDI blits (BitBlt/StretchBlt) + clipboard.
 * GetPixel devuelve el valor crudo del buffer (formato px_disp); los
 * px_disp esperados se hardcodean aqui (verificado contra el LFB):
 *   px_disp(rojo 0x000000FF) = 0x0000FF00
 *   px_disp(verde 0x0000FF00) = 0x00FF0000
 *   px_disp(azul 0x00FF0000) = 0x000000FF
 * Pasos:
 *  - fill azul + rectangulo rojo 20x20 -> GetPixel(20,20) interior rojo.
 *  - memoria DC (CreateCompatibleDC/Bitmap) verde, BitBlt al cliente
 *    -> GetPixel(100,100)=verde.
 *  - StretchBlt 10x10 rojo (mem) a 40x40 -> GetPixel(200,200)=rojo.
 * Clipboard: Set/GetClipboardData round-trip. exit 0 si ok. */
#include <windows.h>
#include <wingdi.h>
#include <string.h>

static void logmsg(const char *s)
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

static void loghex(unsigned v)
{
    static const char *hx = "0123456789ABCDEF";
    char b[11];
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    b[0] = '0'; b[1] = 'x';
    for (int i = 0; i < 8; i++) b[2 + i] = hx[(v >> (28 - i * 4)) & 0xF];
    b[10] = 0;
    logmsg(b);
}

static void sys_exit(int code)
{
    __asm__ volatile("int $0x80" : : "a"(2), "b"(code) : "memory");
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)hwnd; (void)wp; (void)lp;
    if (msg == WM_CLOSE) PostQuitMessage(0);
    return DefWindowProcA(hwnd, msg, wp, lp);
}

int main(void)
{
    WNDCLASS wc;
    HWND hw;
    HDC hdc, memdc;
    HBITMAP bmp;
    HBRUSH blue, red, green;
    RECT rc;
    int ok = 1;

    logmsg("blit: start\n");
    wc.style = 0; wc.lpfnWndProc = WndProc;
    wc.cbClsExtra = 0; wc.cbWndExtra = 0;
    wc.hInstance = GetModuleHandleA(0);
    wc.hIcon = 0; wc.hCursor = 0; wc.hbrBackground = 0;
    wc.lpszMenuName = 0; wc.lpszClassName = "Blit";
    RegisterClassA(&wc);
    hw = CreateWindowExA(0, "Blit", "BlitTest", 0, 40, 40, 320, 260, 0,0,0,0);
    if (!hw) { logmsg("blit: create fallo\n"); return 1; }

    hdc = GetDC(hw);
    blue  = CreateSolidBrush(RGB(0, 0, 255));
    red   = CreateSolidBrush(RGB(255, 0, 0));
    green = CreateSolidBrush(RGB(0, 255, 0));

    rc.left = 0; rc.top = 0; rc.right = 320; rc.bottom = 260;
    FillRect(hdc, &rc, blue);
    SelectObject(hdc, red);
    Rectangle(hdc, 10, 10, 30, 30);      /* relleno rojo + borde negro */
    {
        unsigned c = GetPixel(hdc, 20, 20);   /* interior, no el borde */
        logmsg("blit: px(20,20)="); loghex(c); logmsg("\n");
        ok = ok && (c == 0x00FF0000);        /* px_disp(rojo) */
    }

    /* memoria DC verde + BitBlt al cliente */
    memdc = CreateCompatibleDC(hdc);
    bmp = CreateCompatibleBitmap(hdc, 64, 64);
    SelectObject(memdc, bmp);
    rc.left = 0; rc.top = 0; rc.right = 64; rc.bottom = 64;
    FillRect(memdc, &rc, green);
    BitBlt(hdc, 100, 100, 50, 50, memdc, 0, 0, SRCCOPY);
    {
        unsigned c = GetPixel(hdc, 100, 100);
        logmsg("blit: px(100,100)="); loghex(c); logmsg("\n");
        ok = ok && (c == 0x0000FF00);        /* px_disp(verde) */
    }

    /* StretchBlt: 10x10 rojo en el mem DC -> 40x40 en (200,200) */
    rc.left = 0; rc.top = 0; rc.right = 10; rc.bottom = 10;
    FillRect(memdc, &rc, red);
    StretchBlt(hdc, 200, 200, 40, 40, memdc, 0, 0, 10, 10, SRCCOPY);
    {
        unsigned c = GetPixel(hdc, 200, 200);
        logmsg("blit: px(200,200)="); loghex(c); logmsg("\n");
        ok = ok && (c == 0x00FF0000);        /* px_disp(rojo) */
    }
    ReleaseDC(hw, hdc);
    logmsg("blit: blits ok="); lognum(ok); logmsg("\n");

    /* clipboard */
    if (OpenClipboard(0)) {
        EmptyClipboard();
        {
            char *s = (char *)GlobalAlloc(0, 32);
            strcpy(s, "hola clipboard");
            SetClipboardData(CF_TEXT, (HANDLE)s);
        }
        CloseClipboard();
    }
    if (IsClipboardFormatAvailable(CF_TEXT)) {
        logmsg("blit: clip disponible\n");
        if (OpenClipboard(0)) {
            char *r = (char *)GetClipboardData(CF_TEXT);
            logmsg("blit: clip texto='");
            if (r) logmsg(r);
            logmsg("'\n");
            ok = ok && (r && strcmp(r, "hola clipboard") == 0);
            CloseClipboard();
        }
    } else {
        logmsg("blit: clip NO disponible\n");
        ok = 0;
    }

    logmsg("blit: fin ok="); lognum(ok); logmsg("\n");
    sys_exit(ok ? 0 : 1);
    return 0;
}