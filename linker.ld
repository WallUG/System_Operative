/* MyOS - linker.ld
 * Layout del kernel en memoria: base 0x10000, igual que la direccion de
 * carga del bootloader (ver DESIGN.md). El binario plano via objcopy
 * empieza exactamente en _start, que es lo que el bootloader invoca. */

ENTRY(_start)

SECTIONS
{
    . = 0x10000;

    .text : {
        *(.text*)
    }

    .rodata : {
        *(.rodata*)
    }

    .data : {
        *(.data*)
    }

    .bss : {
        __bss_start = .;
        *(COMMON)
        *(.bss*)
        __bss_end = .;
    }
}
