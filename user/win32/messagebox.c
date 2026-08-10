/* MyOS - user/win32/messagebox.c
 * Probe de la escalera de compatibilidad: Stage 6 (GUI user32).
 * Compilado con la toolchain REAL de Windows (mingw-w64), importa
 * USER32.dll!MessageBoxA. Dibuja una ventana en el framebuffer VBE y
 * espera Enter (serial/teclado) para devolver IDOK (1). */
#include <windows.h>
#include <stdio.h>

int main(void)
{
    int r;

    printf("mb: abriendo MessageBoxA en el framebuffer...\n");
    r = MessageBoxA(NULL,
                    "Hola desde MyOS - GUI (Fase 12)",
                    "MyOS MessageBox", MB_OK);
    printf("mb: MessageBoxA devolvio %d (esperado 1 = IDOK)\n", r);
    return r == IDOK ? 0 : 2;
}
