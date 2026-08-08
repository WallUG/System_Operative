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
| 0x00000500-0x00006FFF | RAM libre (estructuras BIOS efímeras)       |
| 0x00007000           | **boot_info** (20 B, bootloader → kernel, Fase 7) |
| 0x00007C00-0x00007DFF | **Bootloader** (512 bytes, temporal)        |
| 0x00007E00-...        | Buffer **E820** (contador + hasta 32 entradas de 20 B) |
| 0x00010000-0x0001FFFF | **Kernel** (64 KB = 128 sectores). Tope actual: 64 KB |
| 0x00020000 | **Bitmap PMM** (1 bit/frame 4KiB, Fase 4) |
| 0x00050000-0x00057FFF | **Imagen MEFS en RAM** (32 KB, arranque por CD; fs_source de boot_info) |
| 0x00090000-0x0009FFFF | **Pila del kernel** (crece hacia abajo desde 0x90000) |
| 0x000B8000-0x000BFFFF | **VGA texto** (80x25, attr 2 bytes/char)    |
| 0x000C0000-0x000FFFFF | ROMs adaptador - reservado                 |
| 0x00100000-0x00101FFF | **PD de paginación** (1 frame, Fase 4)     |
| 0x00200000-0x0023FFFF | **Heap del kernel** (4 MiB, Fase 4)        |
| 0x00100000+           | RAM alta (PMM: resto de frames libres)     |
| 0x80000000-0x8000FFFF | **Espacio de usuario** (ELF32 ring 3, PD aislado, Fase 7) |

Límite práctico del kernel antes de chocar con la pila: 0x90000 − 0x10000 = 512 KB.

## Layout de la imagen de disco (formato raw) y del CD

`os-image.bin` = boot + kernel + FS encadenados y es la imagen de disco
(`make run`) y la "boot image" del ISO (`make iso`):

| LBA | Contenido                                  |
|-----|--------------------------------------------|
| 0   | Bootloader `boot.bin` (512 bytes, firma 0xAA55) |
| 1..128 | Kernel (pad a 128 sectores = 64 KB)     |
| 129..192 | Imagen MEFS `fs.bin` (pad 64 sectores = 32 KB) |

Arranque por CD (Fase 7, `make test_cd`): `tools/makeiso.py` genera un
ISO9660 con El Torito no-emulation (PVD + Boot Record + Boot Catalog).
La BIOS carga la **imagen completa** (boot + kernel + FS, load segment
0x07C0 → RAM en 0x7C00) sin int 0x13 por parte del bootloader. El
bootloader detecta el modo por `dl >= 0xE0`:
- **Disco** (`dl < 0xE0`): lee el kernel por int 0x13/0x42 a 0x10000;
  el FS queda en disco (ATA, `mefs_init`).
- **CD**: la BIOS ya cargó todo; se copia el kernel 0x7E00 → 0x10000 y
  la imagen MEFS (FS_RAM_SRC 0x17E00) → 0x50000 con `rep movsd`
  (detalles y corrección crítica en la bitácora de Fase 7).

## ABI y convenciones

- **Llamada**: cdecl i386 - argumentos en la pila (push de derecha a izquierda), retorno en `eax`, `ecx`/`edx` volatiles, `ebx`/`esi`/`edi`/`ebp` callee-saved.
- **Flags del kernel (C)**: `-m32 -ffreestanding -fno-stack-protector -fno-pic -fno-pie -fno-builtin -Wall -Wextra`.
- **Entrada del kernel**: `kernel/entry.asm` (org 0x10000) es el punto de entrada desde el bootloader; recibe en `[esp]` el puntero a `boot_info` (Fase 7), y lo preserva al fijar la pila de `.bss` antes de llamar a `kmain(boot_info)`.
- **Stack**: arranca en 0x90000 (16 KiB creciendo hacia abajo); 16 bits → PM sin reconfigurar, layout independiente del modo. Las tareas de usuario usan `esp0` del TSS como pila de kernel al interrumpir desde ring 3.

## Comunicación del bootloader → kernel (Fase 7)

`kernel/bootinfo.h` define `bootinfo_t`, escrita por el bootloader en la
dirección física fija **0x7000** con magic `'MYOS'` (0x4D594F53):

| Campo      | Significado                                    |
|------------|------------------------------------------------|
| `magic`    | 'MYOS': valida que boot_info existe            |
| `mode`     | `BOOTINFO_MODE_DISK` (0) o `BOOTINFO_MODE_CD` (1) |
| `fs_source`| Disco: LBA absoluto del sector 0 del FS (129); CD: dirección física de la imagen MEFS en RAM |
| `fs_size`  | Bytes de la imagen MEFS (solo CD, múltiplo de 512) |
| `kernel_addr` | 0x10000 (informativo)                      |

El bootloader empuja el puntero (0x7000) en la pila antes de saltar al
kernel (`jmp eax` a entry); `entry.asm` lo preserva al fijar la pila del
kernel y lo pasa como argumento cdecl a `kmain(boot_info_ptr)`. El
kernel lo usa para elegir la fuente del filesystem: `mefs_init_mem`
(imagen RAM, CD) o `mefs_init` (ATA, disco); sin magic válido cae a ATA
(fallback para arrancar el kernel desnudo desde el depurador).

## Bitágora de fases

- [x] Fase 0 - Toolchain instalada y Makefile base (i386)
- [x] Fase 1 - Bootloader: mensaje BIOS, carga LBA, A20, GDT, salto a modo protegido
- [x] Fase 2 - Kernel C freestanding (`kmain`), linker script (0x10000), drivers VGA/serial, mini-libc (`libc/string.c`)
- [x] Fase 3 - IDT, ISR/IRQ stubs, PIC 8259 remapeado, kpanic, PIT (IRQ0) y teclado (IRQ1)
- [x] Fase 4 - Gestión de memoria física, paginación, heap
- [x] Fase 5 - Multitarea, scheduler, drivers
- [x] Fase 6 - Filesystem MEFS, modo usuario (ring 3), syscalls, shell
- [x] Fase 7 - Arranque por CD (ISO9660 + El Torito), boot_info, pruebas/regresión QEMU+gdb

## Notas de implementación (bitácora)

- **Fase 2**: se evita la división entera en C (requeriría `__udivsi3`/`__umodsi3` de libgcc, no disponible con `ld -nostdlib`). Si se necesita: implementar helpers en `libc/` o linkear `-lgcc` 32 bits. Layout verificado con `objdump -h` (`.text` VMA=LMA=0x10000).
- **Fase 2**: `kernel/entry.asm` define la pila en `.bss` (16 KB alineada); `_start` fija `esp` y llama a `kmain`. El bootloader ya no provee pila al kernel.
- **Fase 3 (corrección)**: la división entera de 32 bits en i386 se compila a `divl` **inline** (verificado con objdump); NO requiere helpers de libgcc. La nota anterior de Fase 2 era incorrecta para i386. El print decimal es nativo.
- **Fase 3 (bug crítico, corregido)**: el stub de ISR pusheaba `ds`/`es` DESPUÉS de `pusha` y esos valores caían exactamente en el slot de argumento cdecl del handler → `irq_handler` recibía el valor de ds (0x10) como puntero a `registers_t` y leía basura (memoria baja) como `int_no`. Fix doble: (1) los stubs ahora hacen `mov eax, esp; push eax` para pasar explícitamente el puntero al frame; (2) `-fno-optimize-sibling-calls` para que gcc no convierta la última llamada (`pic_send_eoi`) en tail-call, que sobrescribía el slot del argumento (donde vive el `ds` pusheado) con el valor de la IRQ → selector basura → #GP en el `pop ds`. El layout de `registers_t` (ds, es, pusha, int_no, err_code, eip, cs, eflags) coincide byte a byte con la pila.
- **Fase 3 (otro bug)**: gcc -O2 elimina `100u / zero` como código muerto si el resultado no se usa → el demo de #DE debe usar el resultado (`kprint_uint(r)`).
- **Fase 4**: memoria física vía E820 (int 0x15) → PMM bitmap (frames 4 KiB en 0x20000) → paginación identity 0-1 GiB con PSE (páginas 4 MiB, PD = 1 frame del PMM) → heap first-fit en 0x2000000 (4 MiB). Demo final: #PF intencional en 0x50000000 (PDE 320 = 0) capturado por `kpanic_page_fault` (CR2 + err code).
- **Fase 4 (bug crítico, corregido)**: `CR4_PSE` estaba mal definido (`0x4` = bit TSD en vez de `0x10` = bit 4 PSE). Sin CR4.PSE real, la CPU/QEMU interpreta cada PDE con PS=1 como puntero a tabla de páginas: con `pde & ~0xfff` = 0 la IVT actúa como PT → el kernel se mapea a la ROM BIOS y cualquier acceso a memoria > 1 GiB lee PTE=0 → #PF e=0000 → triple fault. Diagnóstico: `info tlb` del monitor QEMU (camina la memoria guest) mostraba los vectores de la IVT como páginas (`0x11000 → 0xf000f000`). Fix: `CR4_PSE 0x10`.
- **Fase 4 (nota)**: `PAGES_4MB` = 256 páginas → identity 0-1 GiB; la demo de #PF requiere que 0x50000000 quede sin mapear (PDE 320 = 0).
- **Fase 5**: scheduler round-robin preemptivo sobre IRQ0. Diseño clave: el epilogo de `irq_common_stub` (`pop ds/es`, `popad`, `add esp 8`, `iret`) ya restaura todo el estado de la tarea interrumpida; el switch (`kernel/task/switch.asm`) solo guarda el puntero al marco `registers_t` en `task->esp`, carga el de la siguiente tarea y ejecuta ese mismo epilogo. `task_create` fabrica un marco falso (eip = entry, cs = 0x8, eflags = 0x202) en un frame del PMM (pila propia, identity map). EOI del PIC se envía ANTES de `sched_tick` (el switch nunca vuelve a `irq_handler`).
- **Fase 5 (nota)**: `sched_tick` no es `noreturn` porque retorna cuando la multitarea está desactivada (antes de `sched_start`). Las tareas demo (A/B) imprimen con IF apagado para no intercalar caracteres.
- **Fase 6**: MEFS (MyOS Easy FS) solo lectura: superbloque (`"MEFS01\n"` + num_files + dir_lba + dir_size), directorio (entradas de 32 B: name[16], size, lba) y datos contiguos. La fuente de sectores es transparente (`fs_read_sector`): ATA PIO (disco) o imagen RAM (CD, `mefs_init_mem`). Shell interactiva (help/ls/cat/echo/ver/run) como tarea idle.
- **Fase 6**: userland: GDT ampliada (selectors 0x08/0x10 kernel, **0x1B/0x23 usuario DPL=3**, 0x28 TSS con `esp0`) + syscalls por **int 0x80** (gate DPL=3; eax=número, ebx/ecx/edx = args): `SYS_PRINT/EXIT/FORK/EXEC/GETPID`. Las tareas de usuario llevan PD aislado (kernel identity supervisor + páginas 4 KiB marcadas USER); los punteros de ring 3 se copian página a página validando con `paging_is_user` → un puntero inválido no produce #PF de kernel.
- **Fase 7 (bug crítico del arranque CD, corregido)**: en modo real, `rep movsd` con prefijo **66** únicamente fija el tamaño de operando (dword + contador ECX); las direcciones vienen del prefijo de address-size (67). Sin él se usan SI/DI de 16 bits y `ESI=0x17E00`/`EDI=0x50000` se truncaban a `SI=0x7E00`/`DI=0x0000`: el bootloader copiaba el **kernel sobre la IVT (0x0-0x3FF)** y dejaba 0x10000/0x50000 a ceros; el primer `int 0x15` (E820) saltaba a la IVT corrupta (`0x83FA:C389`). Diagnóstico: dump de 0x0/0x7E00/0x50000 en el punto del `int 0x15` (kernel en la IVT + fuente pisada) y verificación del binario del boot. SMM descartado con `-machine smm=off` (0 SMM, mismo fallo). Fix: emitir el prefijo **67 + 66 + f3 + a5** (`db 0x67; rep movsd` → `a32 rep movsd`). El boot sector quedó al límite exacto de 512 B.
- **Fase 7 (pruebas/regresión)**: `make test` (disco raw) y `make test_cd` (ISO/CD) headless con serial; en gdb, break en `*0x7ced` (primer `int 0x15`) + dump de 0x10000 (`8b 04 24 bc`), 0x50000 (`MEFS…`) e IVT (53 ff 00 f0) confirma las copias del modo CD.
