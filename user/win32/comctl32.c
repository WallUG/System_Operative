/* MyOS - user/win32/comctl32.c
 * comctl32.dll: modulo Win32 fijo (ring 3) en 0xB5000000.
 * Stubs para metapad.exe: InitCommonControls(Ex) (importadas por
 * ordinal 8/17), CreateToolbarEx y PropertySheetA.
 *
 * CreateToolbarEx -> NULL: metapad sigue sin barra de herramientas.
 * PropertySheetA -> 0: el dialogo de propiedades (pestanas) no abre.
 */

#include <stdint.h>

/* InitCommonControls: sin efectos. Exportada como ordinal 8 (la importa
 * metapad.exe por ordinal) y por nombre (otros .exe). */
void InitCommonControls(void)
{
}

/* InitCommonControlsEx: TRUE, sin efectos. Ordinal 17. */
uint32_t InitCommonControlsEx(const void *icc)
{
    (void)icc;
    return 1;
}

/* CreateToolbarEx: handle fake no nulo (0x200). metapad solo comprueba
 * que no sea NULL para habilitar la barra; no hay toolbar real. */
uint32_t CreateToolbarEx(uint32_t hwnd, uint32_t ws, uint32_t wID,
                         int nBitmaps, uint32_t hBMInst, uint32_t wBMID,
                         const void *lpButtons, int nButtons,
                         int dxButton, int dyButton, uint32_t dxBitmap,
                         uint32_t dyBitmap, uint32_t uStructSize)
{
    (void)hwnd; (void)ws; (void)wID; (void)nBitmaps; (void)hBMInst;
    (void)wBMID; (void)lpButtons; (void)nButtons; (void)dxButton;
    (void)dyButton; (void)dxBitmap; (void)dyBitmap; (void)uStructSize;
    return 0x200;
}

/* PropertySheetA: devuelve 0 (no abre). */
uint32_t PropertySheetA(const void *psh)
{
    (void)psh;
    return 0;
}

typedef struct {
    char     name[32];
    uint32_t fn;
} win32_export_t;

win32_export_t __exports[] __attribute__((section(".exports"))) = {
    { "ord#8",            (uint32_t)&InitCommonControls },
    { "ord#17",           (uint32_t)&InitCommonControlsEx },
    { "InitCommonControls",   (uint32_t)&InitCommonControls },
    { "InitCommonControlsEx", (uint32_t)&InitCommonControlsEx },
    { "CreateToolbarEx",  (uint32_t)&CreateToolbarEx },
    { "PropertySheetA",   (uint32_t)&PropertySheetA },
    { "", 0 },
};
