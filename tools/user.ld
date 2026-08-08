/* MyOS - tools/user.ld
 * Linker script para programas de usuario: todo el contenido en la
 * region de usuario 0x80000000 (paginas mapeadas por el kernel en un
 * PD aislado). Evita el segmento de cabeceras que binutils moderno
 * crea a 0x7ffff000 (fuera de la region de usuario) con -Ttext a secas.
 * Un solo LOAD R E: .text/.rodata/.data y .bss quedan como memsz >
 * filesz (el kernel rellena bss con ceros). */

ENTRY(_start)

SECTIONS {
    . = 0x80000000;
    .text : { *(.text*) }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) }
    .bss : { *(.bss*) }
}
