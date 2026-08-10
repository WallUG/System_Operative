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
| 0x00100000-0x00101FFF | **PD de paginación, kernel** (1 frame, Fase 4) |
| 0x00100000-0x00107FFF | **Banda baja reservada** (Fase 8): PD/PT del kernel, pilas de kernel de las tareas de arranque y tablas tempranas; el PMM no la reutiliza |
| 0x00200000-0x0023FFFF | **Heap del kernel** (4 MiB, Fase 4)        |
| 0x00100000+           | RAM alta (PMM: resto de frames libres)     |
| 0x80000000-0xBFFFFFFF | **Espacio de usuario** (PD aislado, Fases 7-8): exe ELF/PE en 0x80000000, heap de usuario 0x90000000-0xA0000000 (bump SYS_MALLOC), **TIB Win32 en 0x84000000** (Fase 9), modulos Win32 fijos 0xB0000000-0xB3FFFFFF (1 MiB por DLL: kernel32/user32/ntdll/msvcrt), pila de usuario en 0xC0000000 (1 página, crece hacia abajo) |

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
- [x] Fase 8 - Soporte Windows: `.exe` PE32 (cabecera MZ) con imports `.idata`, modulos Win32 ring 3 fijos (kernel32.dll/user32.dll/ntdll.dll en 0xB0000000), shell `run` detecta MZ vs ELF, `SYS_EXEC` dual
- [x] Fase 9 - `.exe` reales mingw-w64: loader PE con **tabla de imports estándar** (IMAGE_IMPORT_DESCRIPTOR + IAT) y resolución case-insensitive, shim `msvcrt.dll` (35 exports) + ampliación de `kernel32.dll` (17 exports), TIB del CRT (FS ring 3), `run hello_win.exe` end-to-end en QEMU
- [x] Fase 10 - **APIs de ficheros de Win32 en MyOS**: syscalls `SYS_DREAD` (lectura posicional, 12) y `SYS_DLIST` (enumerar directorio, 13) + helpers MEFS (`mefs_read_off`, `mefs_dir_get`); shim `kernel32.dll` con `CreateFileA`/`ReadFile`/`GetFileSize`/`CloseHandle`/`FindFirstFileA`/`FindNextFileA`/`FindClose` reales (tabla de handles abiertos con nombre+posición+size); `printf` con anchos/flags en msvcrt; **`dir.exe` real de mingw-w64** (listado + cat de archivo) incluido en `fs.bin`/ISO con `readme.txt`; `FS_SECTORS` ampliado a 448
- [x] Fase 11 - **APIs de proceso de Win32**: syscall `SYS_SELFNAME` (14, nombre del exe actual, campo `exe_name[32]` por tarea); `kernel32.dll` con `GetCurrentProcessId` real (SYS_GETPID, antes devolvía 1 fijo) y `GetModuleFileNameA` (devuelve el nombre con que la shell/SYS_EXEC lanzó el exe); **linea de comandos real por tarea en el TIB** (`GetCommandLineA`/`__getmainargs` → `argc/argv` con la línea de `run`, quoting incluido); **`proc.exe` real de mingw-w64** (PID + nombre + argc/argv + `return 7` → `exit:7`) incluido en `fs.bin`/ISO. **Escalera de compatibilidad 11/11 PASS** en disco y CD
- [x] Fase 12 - **GUI: VBE 800x600x32 + LFB por PCI + consola gráfica + `messagebox.exe`**: modo grafico via interfaz dispi (puertos 0x01CE/0x01CF), LFB leido del BAR0 PCI (0xFD000000 en QEMU 10.x, no la base clasica 0xE0000000), mapeado identity en el PD del kernel y en `VBE_LFB_USER_VA` 0xA8000000 para ring 3; consola del kernel sobre el LFB (`vgafx`, fuente 8x16 compartida con user32); `MessageBoxA` real dibuja una ventana (marco, titulo, texto, boton OK) en el LFB. **Escalera 13/13 PASS** en disco y CD
- [x] Fase 13 - **Raton PS/2 (IRQ12) + cursor**: `kernel/drivers/mouse.c/h` (init 0xA8/0x20/config/0xD4+0xF6/0xF4, drenado de acks, paquete de 3 bytes con sync bit3, deltas firmados, `y -= dy`, saturacion a la pantalla); dispatch IRQ12 en `isr.c`; cursor 8x8 en el LFB con save/restore de la zona pisada. Validado con `mouse_move` del monitor QEMU + screendump (deltas exactos y restauracion sin residuos). **Escalera 13/13 PASS** en disco y CD

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
- **Fase 7 (bug crítico 'ELF invalido', corregido)**: el bucle E820 de `get_mmap` (boot.asm) no re-declaraba **EAX=0xE820 en cada iteración**. La BIOS devuelve `EAX='SMAP'` tras cada llamada; al reentrar en `int 0x15` con EAX=0x534D4150 la BIOS no reconoce la función (CF=1, `jc .done`) y el mapa se corta en **una sola entrada (0x0-0x9FC00, ~0.6 MB usable)** → PMM: 0 frames libres → `pmm_alloc_frame()=0` → `paging_create_user_pd()=0` → `elf_load` devuelve -1 → "ELF invalido". Fix en `boot/boot.asm` (get_mmap): (1) `mov eax, 0xE820` re-escrito al inicio de **cada** iteración (la BIOS no deja eax intacto); (2) el contador se guarda como **dword** (`movzx eax, bp; mov [MMAP_ADDR], eax`), no como word — en modo CD los bytes altos tenían residuos de la imagen del kernel y el kernel leía `0xBC240001` → "E820: 32 entradas" fantasma; (3) compactación del código para volver a caber en los 510 B. Verificado: "E820: 6 entradas, RAM usable ~127 MiB", "PMM: frames libres = 31456", PD en 0x00100000 y `run hello.elf` en ring 3 con salida 0-9 y SYS_EXIT.
- **Fase 7 (shell, corregido)**: `ver` imprimía cada nibble como kprint_hex32 completo (`0x0000000F`) en vez de bytes de 2 dígitos → nuevo helper `hex_byte()` (kernel/shell.c). Backspace: `read_line` solo reconocía `\b` (0x08), no 0x7F (Del, lo que envía PS/2) y `vga_putc` no tenía caso `'\b'` (pintaba glifo 0x08) → ahora `vga_putc` decrementa columna y `read_line` acepta ambos. Verificado `echo dxy` + backspace + `x` → `dxx`, y dump hex de hello.elf con `7F 45 4C 46` correcto.
- **Fase 7 (shell, entrada por serial)**: el kernel solo leía PS/2 (IRQ1) — con QEMU headless (`-serial stdio`) todo lo que se teclea va a COM1 y no pasaba nada. Ahora `serial_read_char()` (serial.c, poll de LSR bit 0, sin IRQ) es fallback en `read_line` del shell y en la ventana de teclado de kmain: permite operar la shell desde la consola serial. Nota: `sendkey` del monitor QEMU no genera IRQ1 en este entorno; con serial la shell se prueba con `timeout N qemu ... -serial stdio` escribiendo el stdin.
- **Fase 7 (demo silenciada)**: las tareas de prueba del scheduler (T-A/T-B) imprimían indefinidamente e invadían la consola durante el uso del shell. `demo_print` (kmain.c) se pone a 0 justo antes de `shell_loop()`: las tareas siguen corriendo pero dejan de imprimir.
- **Fase 7 (pruebas/regresión)**: `make test` (disco raw) y `make test_cd` (ISO/CD) headless con serial; en gdb, break en `*0x7ced` (primer `int 0x15`) + dump de 0x10000 (`8b 04 24 bc`), 0x50000 (`MEFS…`) e IVT (53 ff 00 f0) confirma las copias del modo CD.
- **Fase 8**: `.exe` PE32 generados desde el ELF32 de usuario con `tools/makepe.py` (magia MZ + tabla de imports `.idata`: nombre DLL + función + dirección resuelta). El shell `run` y `SYS_EXEC` detectan el formato por la magia (MZ → `pe_load`/`pe_load_into`, `7F ELF` → `elf_load`). La tabla `.idata` vive en la VA fija 0x82000000 (`tools/user.ld`); `kernel/pe.c` la recorre y resuelve cada import contra `win32_resolve()`.
- **Fase 8**: modulos Win32 ring 3 fijos (decisión de diseño): kernel32.dll/user32.dll/ntdll.dll se compilan como ELF32 enlazados a las bases fijas 0xB0000000/0xB1000000/0xB2000000 (`tools/dll32.ld`) con una tabla `.exports` (nombre → VA absoluta). El kernel los lee del FS en `win32_init()` y los mapea a **cada** PD de usuario en `win32_map_all()` → ningún `.exe` necesita relocaciones: la resolución de imports es escribir en el slot `.idata` la VA fija ya conocida.
- **Fase 8 (bug crítico, corregido)**: el viejo `map_module` mapeaba el binario de la DLL **linealmente** (`VA = base + file_offset`): la cabecera ELF quedaba en 0xB0000000 y el código real en 0xB0001000; un export apuntaba a bytes de cabecera y el `.exe` "ejecutaba" ese ruido (`addb %dh,0x34(%eax)` con `eax=&w` → 0xBFFFFFF0+0x34 = 0xC0000024 no mapeado → `USER #PF` con eip=0xB000001A). Fix: mapear los segmentos **PT_LOAD** con su `p_vaddr`/`p_offset`/`p_filesz` (verificado con `objdump -h`: LOAD a offset 0x1000 → vaddr base).
- **Fase 8 (bug crónico del scheduler, corregido)**: la GPF intermitente en el `iret` de `sched_switch` (marco de la tarea entrante "todo ceros", dependiente del timing) resultó ser una **doble asignación de frames** del PMM: la banda 0x100000-0x108000 (PD/PT del kernel + pilas de las tareas de arranque T-A/T-B) no estaba reservada, y la primera tarea de usuario recibía en `cr3` un frame que ya era la pila de kernel de la tarea demo B (`stack_base=0x103000` = `cr3=0x103000`); el `memset` del PD nuevo borraba la pila de B y su `iret` posterior restauraba el marco a ceros → `#GP`. Diagnóstico con gdb: dump de `tasks[]` (frames) + contenido del frame/PD. Fix: `pmm_reserve_range(0x100000, 0x80000)` en `pmm_init()`.
- **Fase 8 (validación final)**: `run winapi.exe` imprime `A1`/`A2` y `BB` (import `kernel32.WriteFile` resuelto y llamada vía la DLL fija, cola a `SYS_WRITE`) y sale limpio; `quick.exe` (2 prints), `fork.exe` (padre/hijo) y `hello.exe` (0-9 + exit) pasan 2 ciclos completos y 7/7 runs sin panics.

## Bitácora de la Fase 9 (mingw CRT en MyOS)

- **Fase 9 (toolchain)**: `hello_win.exe` se compila con la toolchain REAL de Windows (`i686-w64-mingw32-gcc -m32 -static -O2 -Wl,--image-base,0x80000000 -Wl,--subsystem,console`). Sus imports son la tabla PE estándar: 17 de KERNEL32.dll (DeleteCriticalSection, GetCPInfo, GetProcAddress, LoadLibraryA, MultiByteToWideChar, SetUnhandledExceptionFilter, Sleep, TlsGetValue, VirtualProtect, VirtualQuery, WideCharToMultiByte...) y 35 de msvcrt.dll (__getmainargs, __p__iob, __lc_codepage, __p___initenv, __p___mb_cur_max, __p__commode, __p__fmode, __set_app_type, __setusermatherr, _amsg_exit, _cexit, _errno, _initterm, _lock/_unlock, atexit, abort, calloc, exit, fflush, fprintf, fputc, free, localeconv, malloc, memcpy, putchar, puts, setvbuf, signal, strerror, strlen, strncmp, vfprintf, wcslen).
- **Fase 9 (bucle de la IAT, bug crítico corregido)**: la resolución de imports se recorre por RVA (`dir_rva + i*20`, `oft + j*4`), pero los RVAs de `.idata` de un exe mingw **superan el tamaño del archivo** (las secciones no llegan pegadas al EOF): la guarda `oft + j*4 < size` cortaba el bucle a 0 imports → el IAT quedaba con los RVAs del hint/name → `call *0x8000e144` saltaba a RVAs de datos (p.ej. `0xE332`) → `#PF`. Además el acceso a la tabla se hacía `buf[pe_rva_to_off(buf, oft) + j*4]` (offset de la base + j*4 en vez de por entrada). Fix: convertir **cada** slot con `pe_rva_to_off(buf, oft + j*4)` y validar contra `size`.
- **Fase 9 (escritura de la IAT)**: `paging_user_frame()` exige vaddr alineado a página → los slots de la IAT (p.ej. 0x8000E114) devolvían frame 0 ("IAT fuera de seccion"). Fix: alinear `va & ~0xFFF` y escribir en `frame + (va & 0xFFF)`.
- **Fase 9 (TIB del CRT, `%fs:0x18`)**: el CRT mingw lee `mov %fs:0x18,%eax; [eax+4]=StackBase` antes de main. Se añade la entrada 6 de la GDT (selector 0x33, base = `WIN32_TIB_VA` 0x84000000, limit 0xFFFFF, present ring 3) y `win32_tib_map()` rellena una página USER por tarea (SEH=–1, StackBase/StackLimit de la pila de usuario, Self). FS se carga con `movw $0x33,%ax; movw %ax,%fs` (un inmediato directo a FS no ensambla).
- **Fase 9 (retorno del CRT)**: `mainCRTStartup` guarda `[esp]` inicial y al final hace `lea -0x4(%ecx),%esp; ret` → en `[USER_ESP0_INIT]` debe estar la VA de `msvcrt!_crt_ret` (lee `eax` = return de main y hace `sys_exit(eax)`). Se fija `USER_ESP0_INIT = USER_ESP0_TOP - 8` (paging.h) y `win32_crt_ret_init()` escribe la VA por `paging_user_frame` en `task_create_user` y en el path de `SYS_EXEC`. Verificado: `exit:42` con `return 42` de main.
- **Fase 9 (cabecera PE de la imagen)**: el runtime relocator del mingw inspecciona la página base de la imagen (`cmpw $0x5A4D, 0x80000000`, `e_lfanew`...): si ninguna sección ocupa la base (RVA 0, como en los ELF de makepe.py), el loader mapea esa página **después** del recorrido de secciones con los primeros bytes del archivo (no antes: pisa la sección 0 de los exe MyOS con RVA 0 → "PE invalido").
- **Fase 9 (`.bss` de los módulos)**: `map_module` mapeaba solo `p_filesz`; `.bss` (p.ej. `unhandled_filter` en kernel32) quedaba sin mapear → `#PF`. Fix: mapear `p_memsz` (copiar solo `p_offset..p_offset+p_filesz`, el resto a 0, comparando contra `p_offset + p_filesz` y no `p_filesz`).
- **Fase 9 (números de syscall)**: `SYS_MALLOC` es 10 en `syscall.h` pero el shim msvcrt usaba 11 (= `SYS_FREE` no-op) → `malloc` devolvía NULL y el CRT abortaba con `_amsg_exit(8)`. Fix: `#define SYS_MALLOC 10` en msvcrt.c.
- **Fase 9 (`__getmainargs`)**: el CRT comprueba los punteros devueltos y `envp=NULL` se consideraba error → `_amsg_exit(8)`. El shim devuelve `envp` como lista vacía (`envp[0]=0`).
- **Fase 9 (validación final)**: `run hello_win.exe` imprime `Hello from a REAL Windows-CRT exe!`, `argc = 1`, `malloc: 10 bytes = 'heap works'` y `bye`, y termina con `exit:42` (el `return 42` de `main` recorre las funciones del CRT → `_crt_ret` → `SYS_EXIT`). `winapi.exe` (A1/A2/BB), `quick.exe` (Q1/T2) y `hello.elf` (0-9 + exit) siguen pasando sin regresiones (el `pe_resolve_imports_myos` se mantiene como fallback cuando no hay tabla estándar).

## Bitácora de la Fase 10 (APIs de ficheros Win32)

- **Fase 10 (syscalls nuevas)**: `SYS_DREAD` (12, `ebx=nombre, ecx=buf, edx=off, esi=max`) y `SYS_DLIST` (13, `ebx=idx, ecx=name[16], edx=&size`). Soporte en MEFS: `mefs_read_off()` (salta el byte `off%512` del primer sector y avanza `lba += off/512`) y `mefs_dir_get()`. Ambas validadas con `user_strcpy`/`user_memcpy_out` como el resto de syscalls (puntero inválido → -1, nunca #PF de kernel).
- **Fase 10 (shim kernel32, diseño)**: `CreateFileA` devuelve un handle `0x100+slot` (no el puntero: el CRT puede reusar el buffer del nombre); la tabla `open_files[16]` guarda **copia** del nombre, tamaño y posición de lectura. `ReadFile` hace `SYS_DREAD` con la posición y la avanza (3 syscalls para 5 APIs). `FindFirstFileA/FindNextFileA` iteran `SYS_DLIST` con un filtro de patrón (`*`/`?`) y rellenan una `WIN32_FIND_DATAA` real (atributos/times a 0, `nFileSizeLow`, `cFileName`). `FindClose` libera el slot.
- **Fase 10 (bug, corregido)**: el shim definía `INVALID_HANDLE_VALUE` como `(uint32_t)-1` y `GetFileSize` devolvía ese valor → warning/reinterpretación de puntero. Unificado con el typedef de handle.
- **Fase 10 (`printf` ampliado)**: el `vfprintf` del shim msvcrt solo entendía `%s/%d/%u/%x`, el format de `dir.exe` usaba `%-30s` y `%8lu` → ahora parsea flags (`-`), anchos y precisión (ignora `0`/espacios) y longitudes `l/z/j/h/t`; alineación izquierda/derecha con espacios. `fwrite` añadido al shim.
- **Fase 10 (tamaño FS)**: `dir.exe` sin `-static` (CRT dinámico importando de nuestro `msvcrt.dll`) pesa ~46 KB (vs 252 KB estático). Aun así FS 256 sectores (131072 B) no cabía 19 archivos → `FS_SECTORS=448` (y `boot.asm` en `FS_SECTORS equ 448`).
- **Fase 10 (validación final)**: `run dir.exe` imprime la lista completa del MEFS (19 archivos con tamaños) con `FindFirstFileA` real y `readme.txt` (leído con `CreateFileA` + `ReadFile`), terminando en `exit:0`. Verificado también desde CD (`-cdrom myos.iso -boot order=d`). Sin regresiones en hello_win/winapi/quick.

## Bitácora de la Fase 11 (APIs de proceso Win32)

- **Fase 11 (bug crítico ABI de syscall, corregido)**: el kernel escribe el valor de retorno en `eax`, pero los wrappers `asm` de las syscalls **sin salida** (p.ej. `SYS_WRITE("\n")` en el path de la consola) no lo declaraban como clobber → GCC reutilizaba `eax` y la siguiente syscall se despachaba con el retorno anterior como número (un `SYS_WRITE` se convertía en `SYS_FSIZE` → EOF/fragmentación de lectura). Fix: patrón `int r; __asm__ volatile("int $0x80" : "=a"(r) : ... : "memory"); (void)r;` en **todos** los wrappers sin salida de `user/*.c` y `user/win32/*.c`.
- **Fase 11 (bug SYS_READ congelado, corregido)**: el gate `int 0x80` es de interrupción (limpia IF); `SYS_READ` esperaba entrada con `halt()` en bucle → el sistema entero se congelaba (la entrada de la shell llega por IRQ y la IRQ no podía entrar). Fix: `sti(); halt(); cli();` en el bucle de espera.
- **Fase 11 (bug shell robando input, corregido)**: la shell (tarea idle) consumía la entrada serial destinada al programa de usuario en ejecución → `console.exe` recibía su propio prompt. Fix: `sched_user_busy()` (existe una tarea de usuario con `cr3 != paging_kernel_pd()` en READY/RUNNING) → la shell hace `halt(); continue;` cediendo la CPU y el input.
- **Fase 11 (exe_name por tarea)**: campo `task_t.exe_name[32]` (`TASK_EXE_LEN`), `sched_get_exe_name`/`sched_set_exe_name`, `task_create_user(name, exe, pd, entry)`; la shell pasa el argumento de `run`, y `sys_exec` re-nombra la tarea tras cargar el nuevo binario.
- **Fase 11 (syscall SYS_SELFNAME)**: `SYS_SELFNAME` (14, `ebx=buf, ecx=max`) copia `exe_name` del proceso actual a ring 3 validando con `user_memcpy_out` (puntero inválido → -1).
- **Fase 11 (GetCommandLineA real / argc-argv)**: la línea de comandos vive **por tarea en el TIB** (`WIN32_TIB_CMDLINE_OFF` 0x100, 128 B): el kernel la escribe al crear la tarea (`task_create_user(name, exe, cmdline, pd, entry)` — la shell separa el primer token como `exe_nm` del resto como `cmdline`, así `GetModuleFileNameA` ya no se traga los argumentos) y en `SYS_EXEC`. `GetCommandLineA` la devuelve leyendo `%fs:0x18` + offset (0 syscalls: FS 0x33 es global con base `WIN32_TIB_VA`); `__getmainargs` del shim msvcrt la parsea estilo Windows (espacios separan, comillas agrupan) en `argc/argv`. Verificado: `run proc.exe` → `argc = 1, argv[0] = 'proc.exe'`; `run proc.exe uno "dos tres" 4` → `argc = 4, argv[2] = 'dos tres'`.
- **Fase 11 (shim kernel32)**: `GetCurrentProcessId` deja de devolver 1 hardcodeado y hace `SYS_GETPID` (5) real; `GetModuleFileNameA` (hmodule=NULL → proceso actual) hace `SYS_SELFNAME`. Ambos exportados en `__exports[]`; `proc.exe` (mingw-w64 real) los importa y además `GetProcAddress`/`GetLastError`/CRT (sin `ExitProcess` directo: el `return` de main recorre el CRT → `_crt_ret` → `SYS_EXIT`).
- **Fase 11 (validación final)**: `run proc.exe` imprime `GetCurrentProcessId = 3` (PID real), `GetModuleFileNameA = 'proc.exe' (8 chars)` y termina en `exit:7` (el `return 7` de main); con argumentos imprime `argc/argv` reales. **Escalera 11/11 PASS** en disco y CD, 2/2 ejecuciones estables: hello.elf, quick.exe, winapi.exe, hello_win.exe, dir.exe, fork.exe, exec.exe, console.exe, entrada interactiva, proc.exe, proc.exe con argumentos. Sin regresiones.

## Bitácora de la Fase 12 (GUI: VBE + messagebox.exe)

- **Fase 12 (modo grafico VBE dispi)**: `kernel/drivers/vbe.c` arranca el modo 800x600x32 a traves de la interfaz dispi de QEMU (puertos 0x01CE/0x01CF: 0x0C set-index, 0x01 set-data, 0x10 set-resolution, 0x12 enable 1|0x40). No-op silencioso si no hay VGA (cualquier `inw` al bus VGA en maquinas sin VGA puede colgar: se protege leyendo primero el indice).
- **Fase 12 (LFB por PCI, no fijo)**: el LFB del VGA std/bochs-disp de QEMU 10.x **no** esta en la base clasica 0xE0000000: esta en el BAR0 del dispositivo PCI 00:02.0 (VGA compatible), en este QEMU en 0xFD000000. `vbe_init()` lee el BAR0 por config space PCI (0xCF8/0xCFC) y lo guarda en `vbe_lfb_phys` (0 si no hay). El kernel lo mapea identity como superpage supervisor (`paging_map_kernel_lfb`) y cada PD de usuario en `VBE_LFB_USER_VA` 0xA8000000 (`paging_user_map_lfb`, 512 paginas USER de 4 KiB). `paging_is_lfb_frame` evita que el PMM regale frames del LFB.
- **Fase 12 (bug critico: pila del kernel pisaba el FS en RAM del CD, corregido)**: al arrancar por CD, la imagen MEFS vivia en 0x50000-0x90000 y la **pila del kernel crece hacia abajo desde 0x90000**: cada llamada de la shell (`run` → `pe_load` → `load_sections` → ...) pisa los sectores altos del FS en RAM (0x88000-0x90000 = el final de messagebox.exe: `.idata`/`.reloc`). El exe se leia del FS RAM **despues** de cientos de llamadas → llegaba al heap con la tabla de imports corrupta → `pe_rva_to_off` fallaba silenciosamente en `no == 0` → IAT sin resolver → `call *slot=0` → `USER #PF eip=0x0` con el return address 0x80008328 (`jmp *0x8000e174` = msvcrt!`__p__iob`). Sintoma en disco (ATA): nunca, porque el FS se lee del disco. Diagnostico: `xp` del monitor QEMU en 0x8E000 mostraba strings `.rodata` del kernel (el stack) donde debia haber datos de proc.exe, y el A/B del mismo binario (floppy resuelve 52 imports, CD 0). Fix: `FS_RAM_DEST = 0x140000` en `boot/boot.asm` (dentro de la banda que el PMM ya reserva, 0x100000-0x180000, pegada al PD del kernel en 0x180000; 0x140000+0x40000 = 0x180000 exacto). Actualizado `tools/makeiso.py` y el mapa de memoria de este archivo.
- **Fase 12 (messagebox.exe real de mingw-w64)**: `-Wl,--subsystem,console` (no GUI: evitaría el mensaje del stub mingw y el CRT esperaria `WinMain`), importa USER32!MessageBoxA resuelta contra `user32.elf`; el modo grafico se prueba dibujando la ventana directamente en el LFB mapeado en el PD del usuario (`VBE_LFB_USER_VA`). `run messagebox.exe` pinta la ventana (marco, titulo 17 px, texto, boton OK) y al pulsar Enter en la consola serial el programa lee la tecla y cierra con `exit:0` (`MessageBoxA devolvio 1` = IDOK).
- **Fase 12 (validacion final)**: floppy y CD: `run messagebox.exe` dibuja la ventana (screendump PPM del monitor: fondo 32,32,64; titulo 0,0,136 filas 206-222; texto blanco; boton 136,136,136; marco 192,192,192) y termina en `exit:0`. **Escalera 13/13 PASS** en disco y 2/2 en CD (se anadio el paso messagebox.exe a la escalera). Sin regresiones en Fase 8-11.

## Bitácora de la Fase 13 (ratón PS/2 + cursor)

- **Fase 13 (driver PS/2, diseño)**: `kernel/drivers/mouse.c/h`. Init por el controlador 8042: 0x64←0xA8 (activar aux), leer config byte (0x64←0x20 / 0x60) y poner bit 1 (IRQ12) quitando bit 5 (translate), luego 0x64←0xD4 + 0x60←0xF6 (defaults) y 0x64←0xD4 + 0x60←0xF4 (modo stream). Se drenan los dos acks 0xFA (asíncronos vía IRQ12) leyendo 0x60 para que no corrompan el primer paquete. El handler IRQ12 (`mouse_irq` en `isr.c`, antes del EOI) es mínimo: solo puertos y aritmética, sin kprint.
- **Fase 13 (paquete de 3 bytes)**: byte0 = L/M/R (bits 0-2), sync (bit3 = 1; si no, se descarta), signos y overflow de X/Y (bits 4-7); byte1 = delta X firmado; byte2 = delta Y firmado (**positivo = abajo en pantalla: `y -= dy`**). Overflow (bits 6/7 del byte0) descarta el paquete. Posición saturada a [0,799]×[0,599]. Se inicia centrado (400,300).
- **Fase 13 (cursor con save/restore)**: flecha 8x8 blanca sobre el LFB (`mouse_draw_cursor`): antes de dibujar en la posición nueva restaura los 64 px guardados de la anterior (buffer `cursor_save[]`), luego salva y pinta la nueva. Verificado con `mouse_move dx dy` del monitor HMP de QEMU (existe en QEMU 10.x): `mouse_move 100 50` dibuja la flecha exacta (36 px en diagonal) en (500,350) y `mouse_move -30 -20` restaura la zona previa sin residuos (diff de screendumps: 2 clusters de 36 px). Nota: el scroll de la consola pisa el cursor si no hay movimiento; se redibuja en el siguiente evento (aceptado en esta fase).
- **Fase 13 (validación final)**: floppy y CD: **escalera 13/13 PASS** sin regresiones en Fase 8-12.
