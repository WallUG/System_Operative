/* MyOS - user/hello.c
 * Programa de usuario (Fase 7): ELF32 ET_EXEC compilado con
 * -m32 -ffreestanding -fno-pic -fno-stack-protector -nostdlib y
 * enlazado con -Ttext=0x80000000 (region de usuario del kernel).
 * El kernel lo mapea en un PD aislado (proteccion de memoria).
 * Syscalls: int 0x80 con eax = numero, ebx/ecx = argumentos.
 *   SYS_PRINT (1): imprime la cadena en ebx.
 *   SYS_EXIT  (2): termina la tarea. */

#include <stdint.h>

#define SYS_PRINT 1
#define SYS_EXIT  2

static void sys_print(const char *s)
{
    /* clobber "memory": sin el, GCC -O2 elimina las escrituras al buffer
     * (cree que la syscall no lee memoria). */
    __asm__ volatile("int $0x80" : : "a"(SYS_PRINT), "b"(s) : "memory");
}

static void sys_exit(void)
{
    __asm__ volatile("int $0x80" : : "a"(SYS_EXIT));
}

void _start(void)
{
    /* cuenta 0..9 con retardo para que el scheduler lo intercale */
    for (int i = 0; i < 10; i++) {
        for (volatile uint32_t d = 0; d < 100000; d++)
            ;
        /* convertir i a decimal + '\n' en un buffer local */
        char buf[16];
        int n = i;
        int pos = 0;
        do {
            buf[pos++] = (char)('0' + n % 10);
            n /= 10;
        } while (n > 0);
        for (int j = 0; j < pos / 2; j++) {
            char t = buf[j];
            buf[j] = buf[pos - 1 - j];
            buf[pos - 1 - j] = t;
        }
        buf[pos++] = '\n';
        buf[pos] = 0;
        sys_print(buf);
    }
    sys_print("USER: hello.elf termino, saliendo via SYS_EXIT\n");
    sys_exit();
}
