/* MyOS - user/win32/dir.c
 * dir.exe: aplicacion de consola Windows REAL (CRT de mingw-w64)
 * compilada con i686-w64-mingw32-gcc. Ejercita la capa Win32 de MyOS:
 *   - FindFirstFileA/FindNextFileA/FindClose (kernel32 -> SYS_DLIST)
 *   - CreateFileA/ReadFile/CloseHandle    (kernel32 -> SYS_DREAD)
 *   - printf con anchos/flags (msvcrt)
 * Lista el directorio MEFS y muestra un archivo de texto del FS. */

#include <windows.h>
#include <stdio.h>

int main(void)
{
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA("*", &fd);
    HANDLE f;
    char buf[512];
    DWORD rd;

    if (h == INVALID_HANDLE_VALUE) {
        printf("find: directorio vacio\n");
        return 1;
    }
    printf("== FS via FindFirstFileA (real Win32 API) ==\n");
    do {
        printf("  %-30s %8lu bytes\n", fd.cFileName,
               (unsigned long)fd.nFileSizeLow);
    } while (FindNextFileA(h, &fd));
    FindClose(h);

    f = CreateFileA("readme.txt", GENERIC_READ, 0, NULL, OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) {
        printf("readme.txt: CreateFileA fallo\n");
        return 1;
    }
    printf("\n== readme.txt via CreateFileA/ReadFile ==\n");
    while (ReadFile(f, buf, sizeof(buf), &rd, NULL) && rd > 0)
        fwrite(buf, 1, rd, stdout);
    printf("\n== fin ==\n");
    CloseHandle(f);
    return 0;
}