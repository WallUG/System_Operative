/* MyOS - kernel/syscall.h */
#ifndef MYOS_SYSCALL_H
#define MYOS_SYSCALL_H

#include "idt.h"

#define SYS_PRINT   1   /* ebx = char* (ring 3)               */
#define SYS_EXIT    2   /* termina la tarea actual            */
#define SYS_FORK    3   /* clona la tarea (hijo: retorno 0)   */
#define SYS_EXEC    4   /* ebx = char* nombre de archivo ELF  */
#define SYS_GETPID  5   /* pid de la tarea actual             */

void syscall_init(void);            /* registra el gate 0x80 (DPL=3) */
void syscall_handler(registers_t *regs);

#endif
