# Fase 2 — Entrada del Kernel y C/C++ Freestanding

## Punto de entrada en ASM

```nasm
; entry.asm
[bits 32]
extern kmain
global _start

_start:
    mov esp, stack_top
    call kmain
    cli
.hang:
    hlt
    jmp .hang

section .bss
align 16
stack_bottom:
    resb 16384          ; 16 KB de pila para el kernel
stack_top:
```

## Linker script (`linker.ld`)

Define el mapa de memoria del kernel. Crítico: decide y documenta la dirección base (p. ej. `1M` para kernels 32 bits cargados por GRUB, o la que uses en tu bootloader propio).

```ld
ENTRY(_start)

SECTIONS
{
    . = 1M;

    .text : { *(.multiboot) *(.text) }
    .rodata : { *(.rodata) }
    .data : { *(.data) }
    .bss : { *(COMMON) *(.bss) }
}
```

## Cabecera Multiboot2 (si se usa GRUB en vez de bootloader propio)

```nasm
section .multiboot
align 8
header_start:
    dd 0xE85250D6                ; magic multiboot2
    dd 0                          ; architecture: i386
    dd header_end - header_start
    dd 0x100000000 - (0xE85250D6 + 0 + (header_end - header_start))
    ; tags opcionales aquí (framebuffer, etc.)
    dw 0
    dw 0
    dd 8
header_end:
```

`grub.cfg`:
```
menuentry "MyOS" {
    multiboot2 /boot/kernel.elf
    boot
}
```
Empaquetar: `grub-mkrescue -o myos.iso isodir/` con `isodir/boot/kernel.elf` y `isodir/boot/grub/grub.cfg`.

## `kmain.c` — freestanding, sin libc

Reglas estrictas para código freestanding:
- **No** incluir `<stdio.h>`, `<stdlib.h>` del host. Usa solo headers freestanding: `<stdint.h>`, `<stddef.h>`, `<stdbool.h>` (estos SÍ son válidos, no dependen de runtime).
- Implementa tu propia mini-libc: `memcpy`, `memset`, `memmove`, `strlen`, `strcmp` — funciones que el compilador puede generar llamadas a ellas implícitamente (p. ej. al copiar structs grandes), así que deben existir.
- No hay `malloc` hasta que exista un heap manager (Fase 4). No hay excepciones en C++ (compilar con `-fno-exceptions -fno-rtti`).

```c
#include <stdint.h>
#include <stddef.h>

static uint16_t* const VGA_MEMORY = (uint16_t*) 0xB8000;
static const int VGA_WIDTH = 80;
static const int VGA_HEIGHT = 25;

void kmain(void) {
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        VGA_MEMORY[i] = (uint16_t)' ' | (0x0F << 8);
    }
    const char* msg = "Kernel loaded. Welcome to MyOS.";
    for (int i = 0; msg[i] != '\0'; i++) {
        VGA_MEMORY[i] = (uint16_t)msg[i] | (0x0F << 8);
    }

    for (;;) { __asm__ volatile ("hlt"); }
}
```

## C++ freestanding (si se eligió C++)

Flags obligatorios: `-fno-exceptions -fno-rtti -fno-use-cxa-atexit -nostdlib -fno-threadsafe-statics`.

Debes proveer stubs mínimos para que el linker no falle por símbolos del ABI de Itanium requeridos por constructores globales:

```cpp
extern "C" void* memset(void* dest, int val, size_t len);

extern "C" void __cxa_pure_virtual() { for(;;); }

void* operator new(size_t size);   // implementar una vez exista el heap (Fase 4)
void operator delete(void* p) noexcept;
```

Y ejecutar los constructores globales manualmente desde `_start`/`kmain` recorriendo `.init_array`:
```c
extern void (*__init_array_start[])();
extern void (*__init_array_end[])();
void call_constructors() {
    for (void (**ctor)() = __init_array_start; ctor != __init_array_end; ctor++) (*ctor)();
}
```

## Checklist antes de avanzar a Fase 3
- [x] El kernel compila sin warnings sobre símbolos indefinidos de libc
- [x] `kmain` se ejecuta y escribe correctamente en memoria de video (verificar en pantalla de QEMU)
- [x] El binario final respeta el layout definido en `linker.ld` (verificar con `objdump -h kernel.elf`)
- [x] (Si C++) constructores globales se ejecutan correctamente antes de la lógica principal
