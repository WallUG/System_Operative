/* MyOS - user/exec.c
 * Demo de exec (SYS_EXEC): reemplaza la imagen de la tarea actual por
 * la de otro ELF del FS (hello.elf). Si exec tiene exito, el mensaje
 * posterior no llega a imprimirse nunca. */

#include <stdint.h>

#define SYS_PRINT 1
#define SYS_EXIT  2
#define SYS_EXEC  4

static void sys_print(const char *s)
{
    int r;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(SYS_PRINT), "b"(s) : "memory");
    (void)r;
}

static void sys_exit(void)
{
    int r;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(SYS_EXIT));
    (void)r;
}

static void sys_exec(const char *file)
{
    int r;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(SYS_EXEC), "b"(file)
                     : "memory");
    (void)r;
}

void _start(void)
{
    sys_print("EXEC: programa original, llamando a exec(hello.elf)\n");
    sys_exec("hello.elf");
    sys_print("EXEC: exec fallo (esto no deberia imprimirse)\n");
    sys_exit();
}
