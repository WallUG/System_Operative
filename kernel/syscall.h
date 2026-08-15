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
#define SYS_GFXINFO 15  /* ebx=&struct{lfb_va,w,h,bpp}: info grafica  */
#define SYS_MOUSEINFO 16 /* ebx=&struct{x,y,buttons}: posicion raton  */
#define SYS_EVENT 17    /* ebx=&struct{type,x,y,buttons,key}: -1 vacio */
#define SYS_WINCREATE 18 /* ebx=&{title*,x,y,w,h,buf_va,buf_sz,flags}->id */
#define SYS_WINCLOSE 19 /* ebx=id: cierra la ventana                  */
#define SYS_WINMOVE 20  /* ebx=id, ecx=dx, edx=dy: mueve la ventana   */
#define SYS_WINUPDATE 21 /* ebx=id: recompone (la app termino de pin- */
                         /* tar su buffer)                            */
#define SYS_WININFO 22  /* ebx=id, ecx=&{x,y,w,h,cx,cy,cw,ch}         */
#define SYS_WINTITLE 23 /* ebx=id, ecx=title*: titulo de la barra      */
#define SYS_EXEBASE 24  /* base ImageBase del PE actual (0 si ELF)     */
#define SYS_MENUBAR 25  /* ebx=id, ecx=on, edx=flat*: barra de menu    */
                        /* (Fase D; flat = labels top-level NUL-sep)   */
#define SYS_FCREATE 26  /* ebx=nombre: crea archivo vacio (Fase E)     */
#define SYS_FWRITE 27   /* ebx=nombre, ecx=buf, edx=len: sobrescribe    */
#define SYS_FDELETE 28  /* ebx=nombre: elimina archivo                 */
#define SYS_FLUSH  29   /* persiste superbloque+directorio al disco     */
#define SYS_TOOLBAR 30  /* ebx=id, ecx=on, edx=flat*: barra herramientas */
                        /* (Fase 20-B; igual que SYS_MENUBAR)           */
#define SYS_DLLBASE 31  /* ebx=nombre_dll -> base real de carga (Fase C)
                           (0 si no existe)                             */
#define SYS_GETPROC 32  /* ebx=base_dll, ecx=nombre -> VA export (0 si no) */
#define SYS_DLISTDIR 33 /* ebx=parent, ecx=idx, edx=&{name[16],size,flags}:
                           entrada idx-esima de un directorio (Fase 21)   */
#define SYS_DPARENT 34  /* ebx=idx -> parent (MEFS_ROOT si raiz/inexist)  */
#define SYS_DLOOKUP 35  /* ebx=parent, ecx=name -> indice de la entrada   */
#define SYS_MOUSE_INJECT 36 /* ebx=&mouse_event_t: evento sintetico (Fase
                               23-A3, solo tests: el monitor de QEMU no
                               inyecta PS/2 fiable en headless)          */
#define SYS_MKDIR 37        /* ebx=nombre, ecx=parent: crea subdirectorio
                               (Fase 23-A4)                              */
#define SYS_FCREATE_IN 38   /* ebx=parent, ecx=nombre: crea archivo en un
                               subdirectorio (Fase 23-A4)                */
#define SYS_THREADCREATE 39 /* ebx=fn, ecx=param, edx=&tid (Fase 24-P2.2) */
#define SYS_THREADEXIT 40   /* ebx=code: termina el hilo actual          */

void syscall_init(void);            /* registra el gate 0x80 (DPL=3) */
void syscall_handler(registers_t *regs);

#endif