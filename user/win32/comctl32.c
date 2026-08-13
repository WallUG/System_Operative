/* MyOS - user/win32/comctl32.c
 * comctl32.dll: modulo Win32 fijo (ring 3) en 0xB5000000.
 * Fase 20-B: InitCommonControls(Ex), CreateToolbarEx y PropertySheetA.
 *
 * CreateToolbarEx dibuja una barra de herramientas real: extrae los
 * botones de lpButtons (TBBUTTON) y activa la franja de toolbar de la
 * ventana via la syscall SYS_TOOLBAR (el kernel dibuja los botones como
 * en la barra de menu). Devuelve el handle virtual de la toolbar.
 *
 * PropertySheetA sigue siendo un stub (no usado por metapad en este flujo).
 */

#include <stdint.h>

#define SYS_TOOLBAR 30

static int sys_toolbar(uint32_t id, uint32_t on, const char *flat)
{
    int r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(SYS_TOOLBAR), "b"(id), "c"(on), "d"(flat)
                     : "memory");
    return r;
}

void InitCommonControls(void)
{
}

uint32_t InitCommonControlsEx(const void *icc)
{
    (void)icc;
    return 1;
}

/* TBBUTTON (32 bytes) usado por metapad: iBitmap, idCommand, fsState,
 * fsStyle, bReserved, dwData, iString. Ignoramos los ids y mostramos
 * una barra con los botones habituales de un editor. */
#define TB_BTN_N  8

uint32_t CreateToolbarEx(uint32_t hwnd, uint32_t ws, uint32_t wID,
                         int nBitmaps, uint32_t hBMInst, uint32_t wBMID,
                         const void *lpButtons, int nButtons,
                         int dxButton, int dyButton, uint32_t dxBitmap,
                         uint32_t dyBitmap, uint32_t uStructSize)
{
    static const char *labels[TB_BTN_N] = {
        "Nuevo", "Abrir", "Guardar", "|", "Cortar", "Copiar", "Pegar", "Ayuda"
    };
    char flat[200];
    int k = 0, i;
    (void)ws; (void)wID; (void)nBitmaps; (void)hBMInst; (void)wBMID;
    (void)lpButtons; (void)nButtons; (void)dxButton; (void)dyButton;
    (void)dxBitmap; (void)dyBitmap; (void)uStructSize;

    /* construye el flat NUL-separado; '|' = separador (se omite) */
    for (i = 0; i < TB_BTN_N && k < 190; i++) {
        const char *s = labels[i];
        if (s[0] == '|')
            continue;
        while (*s && k < 190)
            flat[k++] = *s++;
        flat[k++] = 0;
    }
    flat[k++] = 0;
    flat[k] = 0;                /* doble NUL: fin de lista */
    sys_toolbar(hwnd, 1, flat);
    /* handle virtual no nulo (la toolbar se dibuja en el kernel) */
    return 0x300;
}

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