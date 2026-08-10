/* MyOS - user/win32/proc.c
 * Probe de la escalera de compatibilidad: APIs de proceso de Win32.
 * Compilado con la toolchain REAL de Windows (mingw-w64, CRT dinamico
 * contra nuestros shims). Valida:
 *   - GetCurrentProcessId  (pid real de la tarea via SYS_GETPID)
 *   - GetModuleFileNameA   (nombre del exe lanzado via SYS_SELFNAME)
 *   - argc/argv reales     (GetCommandLineA -> TIB -> __getmainargs)
 *   - ExitProcess/exit     (terminacion con codigo de salida)
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    DWORD pid = GetCurrentProcessId();
    char buf[64];
    DWORD n = GetModuleFileNameA(NULL, buf, sizeof(buf));

    printf("proc: GetCurrentProcessId = %lu\n", (unsigned long)pid);
    printf("proc: GetModuleFileNameA = ");
    if (n > 0 && n < sizeof(buf)) {
        printf("'%s' (%lu chars)\n", buf, (unsigned long)n);
    } else {
        printf("FALLO (n=%lu)\n", (unsigned long)n);
    }

    printf("proc: argc = %d\n", argc);
    {
        int i;
        for (i = 0; i < argc; i++)
            printf("proc: argv[%d] = '%s'\n", i, argv[i]);
    }

    printf("proc: longitud de la cadena = %lu\n",
           (unsigned long)strlen(buf));
    return 7;
}
