/* MyOS - tools/dll.ld
 * Linker script para los modulos Win32 (kernel32/user32/ntdll):
 * ELF32 enlazados a una base fija de la region de DLL (WIN32_REGION_BASE
 * 0xB0000000 + i*0x100000, ver kernel/win32.h). Igual que user.ld:
 * un solo LOAD R E con .text/.rodata/.data/.bss, sin el segmento de
 * cabeceras en 0x7ffff000.
 *
 * La base se fija por el simbolo DLL_BASE que pasa el Makefile con
 * -defsym=DLL_BASE=0xB0000000. La tabla de exports (.exports, seccion
 * normal del objeto) queda dentro del segmento: el kernel la localiza
 * por la tabla de secciones shdr del ELF. */

ENTRY(_start)

SECTIONS {
    . = DLL_BASE;
    .text : { *(.text*) }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) }
    .bss : { *(.bss*) }
}