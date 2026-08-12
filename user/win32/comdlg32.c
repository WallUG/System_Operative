/* MyOS - user/win32/comdlg32.c
 * comdlg32.dll: modulo Win32 fijo (ring 3) en 0xB6000000.
 * Dialogos comunes: stubs que devuelven FALSE (0). metapad.exe llama a
 * estos dialogs solo cuando el usuario los invoca; con 0 el programa
 * sigue funcionando sin abrir el dialogo (OpenFile/Find/...). */

#include <stdint.h>

uint32_t GetOpenFileNameA(const void *ofn)
{
    (void)ofn;
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
