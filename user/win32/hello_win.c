/* Programa de prueba compilado con la toolchain REAL de Windows
 * (i686-w64-mingw32-gcc). Su CRT (mainCRTStartup) hace el arranque y
 * llama a main() como si estuvieramos en Windows: usamos printf/malloc
 * del CRT estatico, que internamente tiran de KERNEL32.dll:
 *   GetStdHandle -> WriteFile -> ExitProcess  (entre otras).
 * La DLL kernel32 de MyOS (user/win32/kernel32.c) implementa ese
 * conjunto minimo de funciones Win32 en ring 3.
 *
 * Compilar:  make win_hello   (ver Makefile)
 * No se incluye en fs.bin: se copia a mano o via SYS_EXEC.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    char *heap = NULL;
    size_t n = 0;

    printf("Hello from a REAL Windows-CRT exe!\n");   /* WriteFile */
    printf("argc = %d\n", argc);                     /* vigilan los argv */

    heap = malloc(64);                               /* HeapAlloc via kernel32 */
    if (heap) {
        strcpy(heap, "heap works");
        n = strlen(heap);
        printf("malloc: %zu bytes = '%s'\n", n, heap);
        free(heap);
    }

    putchar('\n');
    puts("bye");                                    
    return 42;
}