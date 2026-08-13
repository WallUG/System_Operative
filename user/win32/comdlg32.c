/* MyOS - user/win32/comdlg32.c
 * comdlg32.dll: modulo Win32 fijo (ring 3) en 0xB6000000.
 * Dialogos comunes: stubs que devuelven FALSE (0). metapad.exe llama a
 * estos dialogs solo cuando el usuario los invoca; con 0 el programa
 * sigue funcionando sin abrir el dialogo (OpenFile/Find/...). */

#include <stdint.h>

static int dlg_write(const char *s, uint32_t len)
{
    int r;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(1), "b"(s), "c"(len));
    return r;
}

static void dlg_str(const char *s)
{
    uint32_t n = 0;
    if (!s) { dlg_write("(null)", 6); return; }
    while (s[n]) n++;
    dlg_write(s, n);
}

uint32_t GetOpenFileNameA(const void *ofn)
{
    const char *f = ofn ? *(const char **)((const char *)ofn + 0x1C) : 0;
    const char *id = ofn ? *(const char **)((const char *)ofn + 0x2C) : 0;
    const char *ti = ofn ? *(const char **)((const char *)ofn + 0x30) : 0;
    uint32_t fl = ofn ? *(uint32_t *)((const char *)ofn + 0x34) : 0;
    dlg_write("[cdlg] GetOpenFileNameA\n", 23);
    dlg_write("  lpstrFile='", 13); dlg_str(f); dlg_write("'\n", 2);
    dlg_write("  initialDir='", 15); dlg_str(id); dlg_write("'\n", 2);
    dlg_write("  title='", 10); dlg_str(ti); dlg_write("' flags=", 8);
    {
        uint32_t x = fl, d = 1000000000, started = 0;
        char tmp[16]; int k = 0;
        if (x == 0) tmp[k++] = '0';
        while (x) { if (x / d || started) { tmp[k++] = '0' + x / d; started = 1; x %= d; } d /= 10; }
        tmp[k] = 0;
        dlg_write(tmp, k);
    }
    dlg_write("\n", 1);
    return 0;
}

uint32_t GetSaveFileNameA(const void *ofn)
{
    (void)ofn;
    return 0;
}

/* FindTextA/ReplaceTextA devuelven HWND del dialogo modelo (0 = no
 * abierto). */
uint32_t FindTextA(const void *fr)
{
    (void)fr;
    return 0;
}

uint32_t ReplaceTextA(const void *fr)
{
    (void)fr;
    return 0;
}

uint32_t ChooseFontA(const void *cf)
{
    (void)cf;
    return 0;
}

uint32_t ChooseColorA(const void *cc)
{
    (void)cc;
    return 0;
}

uint32_t PrintDlgA(const void *pd)
{
    (void)pd;
    return 0;
}

uint32_t PageSetupDlgA(const void *psd)
{
    (void)psd;
    return 0;
}

uint32_t CommDlgExtendedError(void)
{
    return 0;
}

typedef struct {
    char     name[32];
    uint32_t fn;
} win32_export_t;

win32_export_t __exports[] __attribute__((section(".exports"))) = {
    { "GetOpenFileNameA",     (uint32_t)&GetOpenFileNameA },
    { "GetSaveFileNameA",     (uint32_t)&GetSaveFileNameA },
    { "FindTextA",            (uint32_t)&FindTextA },
    { "ReplaceTextA",         (uint32_t)&ReplaceTextA },
    { "ChooseFontA",          (uint32_t)&ChooseFontA },
    { "ChooseColorA",         (uint32_t)&ChooseColorA },
    { "PrintDlgA",            (uint32_t)&PrintDlgA },
    { "PageSetupDlgA",        (uint32_t)&PageSetupDlgA },
    { "CommDlgExtendedError", (uint32_t)&CommDlgExtendedError },
    { "", 0 },
};
