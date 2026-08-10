/* MyOS - kernel/syscall.h */
#ifndef MYOS_SYSCALL_H
#define MYOS_SYSCALL_H

#include "idt.h"

#define SYS_PRINT   1   /* ebx = char* (ring 3): cadena terminada en \0 */
#define SYS_EXIT    2   /* termina la tarea actual                     */
#define SYS_FORK    3   /* clona la tarea (hijo: retorno 0)           */
#define SYS_EXEC    4   /* ebx = char* nombre de archivo ELF          */
#define SYS_GETPID  5   /* pid de la tarea actual                     */
#define SYS_READ    6   /* consola: ebx=buf, ecx=max -> chars leidos  */
#define SYS_WRITE   7   /* consola: ebx=buf, ecx=len (bytes exactos)  */
#define SYS_FSIZE   8   /* ebx = char* nombre -> tamano o -1          */
#define SYS_FREAD   9   /* ebx=nombre, ecx=buf, edx=input: bytes o -1 */
#define SYS_MALLOC  10  /* ebx = bytes -> ptr de usuario o 0          */
#define SYS_FREE    11  /* ebx = ptr (heap bump; no-op por ahora)     */
#define SYS_DREAD   12  /* ebx=nombre, ecx=buf, edx=off, esi=max      */
#define SYS_DLIST   13  /* ebx=idx dir, ecx=name[16], edx=&size       */
#define SYS_SELFNAME 14 /* ebx=buf, ecx=max: nombre del exe actual    */

void syscall_init(void);            /* registra el gate 0x80 (DPL=3) */
void syscall_handler(registers_t *regs);

#endif