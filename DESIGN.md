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
| 0x00100000-0x00101FFF | **PD de paginación** (1 frame, Fase 4)     |
| 0x00200000-0x0023FFFF | **Heap del kernel** (4 MiB, Fase 4)        |
| 0x00100000+           | RAM alta (PMM: resto de frames libres)     |

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
- [x] Fase 2 - Kernel C freestanding (`kmain`), linker script (0x10000), drivers VGA/serial, mini-libc (`libc/string.c`)
- [x] Fase 3 - IDT, ISR/IRQ stubs, PIC 8259 remapeado, kpanic, PIT (IRQ0) y teclado (IRQ1)
- [x] Fase 4 - Gestión de memoria física, paginación, heap
- [ ] Fase 5 - Multitarea, scheduler, drivers
- [ ] Fase 6 - Filesystem, modo usuario, shell
- [ ] Fase 7 - Pruebas, GDB, CI

## Notas de implementación (bitácora)

- **Fase 2**: se evita la división entera en C (requeriría `__udivsi3`/`__umodsi3` de libgcc, no disponible con `ld -nostdlib`). Si se necesita: implementar helpers en `libc/` o linkear `-lgcc` 32 bits. Layout verificado con `objdump -h` (`.text` VMA=LMA=0x10000).
- **Fase 2**: `kernel/entry.asm` define la pila en `.bss` (16 KB alineada); `_start` fija `esp` y llama a `kmain`. El bootloader ya no provee pila al kernel.
- **Fase 3 (corrección)**: la división entera de 32 bits en i386 se compila a `divl` **inline** (verificado con objdump); NO requiere helpers de libgcc. La nota anterior de Fase 2 era incorrecta para i386. El print decimal es nativo.
- **Fase 3 (bug crítico, corregido)**: el stub de ISR pusheaba `ds`/`es` DESPUÉS de `pusha` y esos valores caían exactamente en el slot de argumento cdecl del handler → `irq_handler` recibía el valor de ds (0x10) como puntero a `registers_t` y leía basura (memoria baja) como `int_no`. Fix doble: (1) los stubs ahora hacen `mov eax, esp; push eax` para pasar explícitamente el puntero al frame; (2) `-fno-optimize-sibling-calls` para que gcc no convierta la última llamada (`pic_send_eoi`) en tail-call, que sobrescribía el slot del argumento (donde vive el `ds` pusheado) con el valor de la IRQ → selector basura → #GP en el `pop ds`. El layout de `registers_t` (ds, es, pusha, int_no, err_code, eip, cs, eflags) coincide byte a byte con la pila.
- **Fase 3 (otro bug)**: gcc -O2 elimina `100u / zero` como código muerto si el resultado no se usa → el demo de #DE debe usar el resultado (`kprint_uint(r)`).
- **Fase 4**: memoria física vía E820 (int 0x15) → PMM bitmap (frames 4 KiB en 0x20000) → paginación identity 0-1 GiB con PSE (páginas 4 MiB, PD = 1 frame del PMM) → heap first-fit en 0x2000000 (4 MiB). Demo final: #PF intencional en 0x50000000 (PDE 320 = 0) capturado por `kpanic_page_fault` (CR2 + err code).
- **Fase 4 (bug crítico, corregido)**: `CR4_PSE` estaba mal definido (`0x4` = bit TSD en vez de `0x10` = bit 4 PSE). Sin CR4.PSE real, la CPU/QEMU interpreta cada PDE con PS=1 como puntero a tabla de páginas: con `pde & ~0xfff` = 0 la IVT actúa como PT → el kernel se mapea a la ROM BIOS y cualquier acceso a memoria > 1 GiB lee PTE=0 → #PF e=0000 → triple fault. Diagnóstico: `info tlb` del monitor QEMU (camina la memoria guest) mostraba los vectores de la IVT como páginas (`0x11000 → 0xf000f000`). Fix: `CR4_PSE 0x10`.
- **Fase 4 (nota)**: `PAGES_4MB` = 256 páginas → identity 0-1 GiB; la demo de #PF requiere que 0x50000000 quede sin mapear (PDE 320 = 0).
