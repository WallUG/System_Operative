/* MyOS - user/quick.c: minimo de 2 syscalls + exit para test del iret */
#include <stdint.h>
static void sys_print(const char *s) { int r; __asm__ volatile("int $0x80" : "=a"(r) : "a"(1), "b"(s) : "memory"); (void)r; }
static void sys_exit(void) { int r; __asm__ volatile("int $0x80" : "=a"(r) : "a"(2)); (void)r; }
void _start(void)
{
    for (volatile uint32_t d = 0; d < 800000; d++)
        ;
    sys_print("Q1\n");
    for (volatile uint32_t d = 0; d < 800000; d++)
        ;
    sys_print("T2\n");
    sys_exit();
    for (;;) ;
}
