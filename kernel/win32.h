/* MyOS - kernel/win32.h
 * Capa Win32: modulos ring 3 fijos (decision "Modulos ring 3 fijos").
 *
 * kernel32.dll / user32.dll / ntdll.dll son programas ELF32 normales
 * (toolchain MyOS) enlazados en direcciones virtuales FIJAS de la
 * region de DLL (0xB0000000-0xB3FFFFFF), por debajo de la region del
 * programa (0x80000000+). El kernel los lee del FS ("kernel32.elf",
 * etc), valida sus cabeceras ELF y los mapea en esa base en TODO PD de
 * usuario nuevo: todos los .exe ven los modulos en las mismas
 * direcciones (fijos, sin relocaciones).
 *
 * Cada modulo declara una tabla de exports en la seccion .exports:
 *   typedef struct { char name[16]; uint32_t fn; } win32_export_t;
 *   win32_export_t __exports[] = { ..., {0,0} };
 * El kernel copia esa tabla a RAM (win32.c) y pe.c resuelve las
 * entradas .idata del .exe contra ella (win32_resolve). */
#ifndef MYOS_WIN32_H
#define MYOS_WIN32_H

#include <stdint.h>

/* Region fija de los modulos: 0xB0000000 (4 x 1 MiB, uno por DLL). */
#define WIN32_REGION_BASE   0xB0000000u
#define WIN32_DLL_KERNEL32  0   /* base + 0        kernel32.dll */
#define WIN32_DLL_USER32    1   /* base + 0x100000  user32.dll */
#define WIN32_DLL_NTDLL     2   /* base + 0x200000  ntdll.dll */
#define WIN32_MAX_DLLS      4

/* VA del descriptor de imports de los .exe (pe.h): el programa lo
 * declara como seccion .idata; el kernel lo rellena con las VA de las
 * funciones resueltas. */
#define WIN32_IDATA_VA      0x82000000u
#define WIN32_IDATA_MAX     64          /* max imports por .exe */

/* VA del TIB (Thread Information Block) que usan los CRTs de mingw:
 * cada tarea de usuario mapea una pagina USER en esta VA (el segmento
 * FS de la GDT, base = WIN32_TIB_VA, apunta aqui). %fs:0x18 = Self. */
#define WIN32_TIB_VA        0x84000000u

/* Export single: name, funcion VA dentro del modulo. */
#define WIN32_EXPORT_MAX    64          /* por DLL */
#define WIN32_EXPORT_NAME   32          /* nombre de export, hasta "SetUnhandledExceptionFilter" */

typedef struct {
    char     name[WIN32_EXPORT_NAME];
    uint32_t fn;
} win32_export_t;

/* Un DLL cargado: nombre base "kernel32", base VA, size, exports. */
typedef struct {
    uint32_t base;
    uint32_t size;
    uint32_t file_size;
    uint32_t n_exports;
    win32_export_t exports[WIN32_EXPORT_MAX];
    const uint8_t *bin;         /* binario ELF en RAM del kernel */
} win32_module_t;

/* Lee los modulos DLL del FS (kernel32.elf, user32.elf, ntdll.elf),
 * valida sus cabeceras ELF y guarda la tabla de exports. 0 = OK. */
int  win32_init(void);
/* Mapea `modulo` (instancia fija, alineada 4 KiB) en el espacio de
 * usuario de pd: una pagina USER RW por 4 KiB de modulo. */
int  win32_map(uint32_t pd, uint32_t mod_idx);
/* Idem pero mapea TODOS los modulos cargados (llamado al crear la
 * tarea) y el TIB de usuario en WIN32_TIB_VA. */
int  win32_map_all(uint32_t pd);
/* Mapea y rellena el TIB (Thread Information Block) de usuario en
 * WIN32_TIB_VA: una pagina USER nueva con Self/StackBase/StackLimit. */
int  win32_tib_map(uint32_t pd);
/* Escribe [USER_ESP0_INIT] = msvcrt!_crt_ret: return address de
 * arranque del CRT de mingw (lea esp,ecx-4; ret). pd = el de la tarea. */
int  win32_crt_ret_init(uint32_t pd);
/* Resuelve "dll.func" -> direccion virtual de la funcion (0 si no
 * existe). dll_fname = "kernel32.dll" (o base/binario del archivo). */
uint32_t win32_resolve(const char *dll, const char *fn);
/* Numero de modulos cargados. */
int  win32_count(void);

#endif