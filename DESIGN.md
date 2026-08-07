# MyOS - Diseño y decisiones de bajo nivel

Sistema operativo didáctico desarrollado por fases. Este documento fija decisiones que
no deben cambiar sin actualizar este archivo.

## Arquitectura y lenguajes

- **Arquitectura objetivo**: x86 (IA-32, 32 bits). Modo real → modo protegido. Long mode (64 bits) se evalúa más adelante.
- **Kernel**: C freestanding (a partir de la Fase 2). Sin libc del host.
- **Ensamblador**: NASM (sintaxis Intel) para bootloader, stubs de ISR y context switch.
- **Toolchain**: compilador del host (gcc) con flags freestanding y `-m32` (fallback documentado en la skill; un cross-compiler `i686-elf` dedicado sería lo ideal en producción).

## Mapa de memoria (boot y kernel temprano)

| Rango               | Uso                                            |
|---------------------|------------------------------------------------|
| 0x00000000-0x000003FF | IVT (BIOS) - reservado                      |
| 0x00000400-0x000004FF | BDA (BIOS Data Area) - reservado            |
| 0x00000500-0x00007BFF | RAM libre (estructuras BIOS efímeras)       |
| 0x00007C00-0x00007DFF | **Bootloader** (512 bytes, temporal)        |
| 0x00010000-...      | **Kernel** (cargado por `int 0x13` LBA desde sector 1). Tope actual: 32 KB (64 sectores) |
| 0x00090000-0x0009FFFF | **Pila del kernel** (crece hacia abajo desde 0x90000) |
| 0x000B8000-0x000BFFFF | **VGA texto** (80x25, attr 2 bytes/char)    |
| 0x000C0000-0x000FFFFF | ROMs adaptador - reservado                 |
| 0x00100000+         | RAM alta (libre; futuros: heap, tablas de página, usuarios) |

Límite práctico del kernel antes de chocar con la pila: 0x90000 − 0x10000 = 512 KB.

## Layout de la imagen de disco (formato raw)

| LBA | Contenido                                  |
|-----|--------------------------------------------|
| 0   | Bootloader `boot.bin` (512 bytes, firma 0xAA55) |
| 1.. | Kernel (pad a 64 sectores = 32 KB)        |

## ABI y convenciones

- **Llamada**: cdecl i386 - argumentos en la pila (push de derecha a izquierda), retorno en `eax`, `ecx`/`edx` volatiles, `ebx`/`esi`/`edi`/`ebp` callee-saved.
- **Flags del kernel (C)**: `-m32 -ffreestanding -fno-stack-protector -fno-pic -fno-pie -fno-builtin -Wall -Wextra`.
- **Entrada del kernel**: `kernel/entry.asm` (org 0x10000) es el punto de entrada desde el bootloader; en Fase 2 llamará a `kmain()` de C.
- **Stack**: arranca en 0x90000 (16 KiB creciendo hacia abajo); 16 bits → PM sin reconfigurar, layout independiente del modo.

## Comunicación del bootloader → kernel (Fase 1)

Sin parámetros por ahora: el kernel stub asume carga en 0x10000. La Fase 2 definirá una
estructura de boot info pasada en registro/pila si hace falta (equivalente a multiboot).

## Bitágora de fases

- [x] Fase 0 - Toolchain instalada y Makefile base (i386)
- [x] Fase 1 - Bootloader: mensaje BIOS, carga LBA, A20, GDT, salto a modo protegido
- [ ] Fase 2 - Kernel C freestanding (`kmain`), linker script, VGA driver
- [ ] Fase 3 - IDT, ISR/IRQ, PIC, teclado, PIT
- [ ] Fase 4 - Gestión de memoria física, paginación, heap
- [ ] Fase 5 - Multitarea, scheduler, drivers
- [ ] Fase 6 - Filesystem, modo usuario, shell
- [ ] Fase 7 - Pruebas, GDB, CI
