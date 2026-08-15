/* MyOS - user/win32/ctldemo.c
 * Fase 24-P2.1: toolbar + statusbar + trackbar + treeview (comctl32).
 * Crea una ventana con los 4 controles hijo y verifica via SendMessageA:
 *  - toolbar: TB_BUTTONCOUNT == 2
 *  - statusbar: SB_GETTEXT(part 1) == "Listo"
 *  - trackbar: TBM_GETPOS == 7 (tras TBM_SETRANGE 0..10 + TBM_SETPOS)
 *  - treeview: TVM_GETCOUNT == 2 (raiz + hijo)
 * El app hace paint (pide un mensaje) y termina con exit 0 si ok. */
#include <windows.h>
#include <commctrl.h>
#include <stdint.h>

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

static void sys_exit(int code)
{
    __asm__ volatile("int $0x80" : : "a"(2), "b"(code) : "memory");
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)hwnd; (void)wp; (void)lp;
    if (msg == WM_CLOSE)
        PostQuitMessage(0);
    return DefWindowProcA(hwnd, msg, wp, lp);
}

int main(void)
{
    WNDCLASS wc;
    HWND hw, tb, sb, tr, tv;
    MSG msg;
    TBBUTTON btns[2];
    int parts[2];
    int pos;
    char sbuf[32];
    HTREEITEM hroot, hchild;
    TV_INSERTSTRUCT ins;
    int ok = 1;

    logmsg("ctl: start\n");
    InitCommonControls();

    wc.style = 0;
    wc.lpfnWndProc = WndProc;
    wc.cbClsExtra = 0; wc.cbWndExtra = 0;
    wc.hInstance = GetModuleHandleA(0);
    wc.hIcon = 0; wc.hCursor = 0; wc.hbrBackground = 0;
    wc.lpszMenuName = 0; wc.lpszClassName = "CtlDemo";
    RegisterClassA(&wc);

    hw = CreateWindowExA(0, "CtlDemo", "Controles comctl32", 0,
                         40, 40, 420, 300, 0, 0, 0, 0);
    if (!hw) { logmsg("ctl: create fallo\n"); return 1; }

    /* toolbar */
    tb = CreateWindowExA(0, "ToolbarWindow32", "", WS_CHILD,
                         4, 4, 400, 28, hw, 0, 0, 0);
    SendMessageA(tb, TB_BUTTONSTRUCTSIZE, (WPARAM)sizeof(TBBUTTON), 0);
    btns[0].iBitmap = 0; btns[0].idCommand = 100; btns[0].fsState = 1;
    btns[0].fsStyle = 0; btns[0].bReserved[0] = 0; btns[0].bReserved[1] = 0;
    btns[0].dwData = 0; btns[0].iString = -1;
    btns[1] = btns[0]; btns[1].idCommand = 101;
    SendMessageA(tb, TB_ADDBUTTONS, 2, (LPARAM)btns);
    {
        int n = (int)SendMessageA(tb, TB_BUTTONCOUNT, 0, 0);
        logmsg("ctl: toolbar btns=");
        lognum(n);
        logmsg("\n");
        ok = ok && (n == 2);
    }

    /* statusbar */
    sb = CreateWindowExA(0, "msctls_statusbar32", "", WS_CHILD,
                         4, 260, 400, 22, hw, 0, 0, 0);
    parts[0] = 200; parts[1] = -1;
    SendMessageA(sb, SB_SETPARTS, 2, (LPARAM)parts);
    SendMessageA(sb, SB_SETTEXTA, 1, (LPARAM)"Listo");
    {
        int n = (int)SendMessageA(sb, SB_GETTEXTA, 1, (LPARAM)sbuf);
        sbuf[n] = 0;
        logmsg("ctl: statusbar text=");
        logmsg(sbuf);
        logmsg("\n");
        ok = ok && (sbuf[0] == 'L');
    }

    /* trackbar */
    tr = CreateWindowExA(0, "msctls_trackbar32", "", WS_CHILD,
                         4, 120, 400, 30, hw, 0, 0, 0);
    SendMessageA(tr, TBM_SETRANGE, 0, (LPARAM)0x000A0000); /* min..max */
    SendMessageA(tr, TBM_SETPOS, 1, 7);
    pos = (int)SendMessageA(tr, TBM_GETPOS, 0, 0);
    logmsg("ctl: trackbar pos=");
    lognum(pos);
    logmsg("\n");
    ok = ok && (pos == 7);

    /* treeview */
    tv = CreateWindowExA(0, "SysTreeView32", "", WS_CHILD,
                         4, 160, 400, 90, hw, 0, 0, 0);
    ins.hParent = TVI_ROOT;
    ins.hInsertAfter = TVI_LAST;
    ins.item.mask = TVIF_TEXT;
    ins.item.pszText = "Raiz";
    ins.item.cchTextMax = 8;
    hroot = (HTREEITEM)SendMessageA(tv, TVM_INSERTITEMA, 0, (LPARAM)&ins);
    ins.hParent = hroot;
    ins.item.pszText = "Hijo";
    hchild = (HTREEITEM)SendMessageA(tv, TVM_INSERTITEMA, 0, (LPARAM)&ins);
    {
        int n = (int)SendMessageA(tv, TVM_GETCOUNT, 0, 0);
        logmsg("ctl: treeview nodes=");
        lognum(n);
        logmsg(" (root=");
        lognum((int)hroot);
        logmsg(",child=");
        lognum((int)hchild);
        logmsg(")\n");
        ok = ok && (n == 2 && hroot == (HTREEITEM)1 && hchild == (HTREEITEM)2);
    }

    ShowWindow(hw, 1);

    /* drena 1 mensaje para hacer paint del layout completo */
    if (PeekMessageA(&msg, 0, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    logmsg("ctl: fin ok=");
    lognum(ok);
    logmsg("\n");
    sys_exit(ok ? 0 : 1);
    return 0;
}