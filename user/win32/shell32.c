/* MyOS - user/win32/shell32.c
 * shell32.dll: modulo Win32 fijo (ring 3) en 0xB8000000.
 * Stubs para metapad.exe: ShellExecuteA (viewer externo / navegador)
 * devuelve exito sin hacer nada (valores 0-32 son errores; >32 exito
 * no verificable). Drag & drop de archivos: no hay drops. */

#include <stdint.h>

/* ShellExecuteA: HINSTANCE > 32 = exito. No hay procesos externos:
 * devuelve exito para que el llamador no muestre error. */
uint32_t __attribute__((stdcall)) ShellExecuteA(uint32_t hwnd, const char *op, const char *file,
                       const char *params, const char *dir, int show)
{
    (void)hwnd; (void)op; (void)file; (void)params; (void)dir;
    (void)show;
    return 42;
}

/* DragQueryFileA: 0 archivos arrastrados. */
uint32_t __attribute__((stdcall)) DragQueryFileA(uint32_t hdrop, uint32_t i, char *buf,
                        uint32_t size)
{
    (void)hdrop; (void)i; (void)buf; (void)size;
    return 0;
}

void __attribute__((stdcall)) DragFinish(uint32_t hdrop)
{
    (void)hdrop;
}

typedef struct {
    char     name[32];
    uint32_t fn;
} win32_export_t;

win32_export_t __exports[] __attribute__((section(".exports"))) = {
    { "ShellExecuteA",  (uint32_t)&ShellExecuteA },
    { "DragQueryFileA", (uint32_t)&DragQueryFileA },
    { "DragFinish",     (uint32_t)&DragFinish },
    { "", 0 },
};
