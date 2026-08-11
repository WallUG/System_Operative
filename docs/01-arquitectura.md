# MyOS — Arquitectura (visión global)

## Objetivo

Sistema operativo didáctico para PC x86 (IA-32, 32 bits) construido desde
cero: bootloader en NASM, kernel en C freestanding, multitarea preemptiva,
filesystem propio y modo usuario protegido (ring 3). Un objetivo secundario
del proyecto es ejecutar ejecutables Windows `.exe` (PE32) reales compilados
con mingw-w64, mediante shims propietarios de DLLs (`kernel32`, `user32`,
`ntdll`, `msvcrt`) en ring 3 — sin usar Windows ni emulación externa.

## Modos de ejecución

| Modo | Quién lo usa | Privilegios |
|------|--------------|-------------|
| Real (16 bits) | Bootloader (temporal) | Segmentos, sin protección |
| Protegido (32, ring 0) | Kernel + drivers | Acceso total, paginación supervisor |
| Protegido (32, ring 3) | Userland (ELFs/EXEs) | Solo su región virtual (0x80000000+) |

## Mapa de memoria física (kernel temprano)

| Rango | Uso |
|-------|-----|
| 0x0-0x3FF | IVT (BIOS) |
| 0x400-0x4FF | BDA |
| 0x500-0x6FFF | RAM libre (E820, estructuras efímeras) |
| 0x7000 | `boot_info` (20 B, bootloader → kernel) |
| 0x7C00-0x7DFF | Bootloader (512 B) |
| 0x7E00+ | Buffer E820 (contador + 32 entradas de 20 B) |
| 0x90000-0x9FFFF | Pila del kernel (crece hacia abajo) |
| 0xB8000 | VGA texto (inactivo en modo gráfico) |
| 0x100000-0x186000 | **Banda reservada** (PD/PT kernel, pilas de arranque, imagen MEFS en RAM por CD: 0x140000..0x186000) |
| 0x100000 (0x1000) | PD global del kernel (identity 0-1 GiB con PSE) |
| 0x2000000-0x23FFFFF | Heap del kernel (4 MiB, first-fit) |
| 0x80000000-0xBFFFFFFF | Espacio de usuario (PD aislado, ver 09-win32.md) |
| 0xFD000000 | LFB VBE (leído del BAR0 PCI, mapeado a `VBE_LFB_USER_VA`) |

## Layout de la imagen de disco / CD

`os-image.bin` = `boot.bin` + `kernel.bin` + `fs.bin` concatenados:

| LBA | Contenido |
|-----|-----------|
| 0 | Bootloader (512 B, firma 0xAA55) |
| 1..128 | Kernel (pad a 64 KB) |
| 129..688 | MEFS (560 sectores = 286 KB) |

- **Disco**: el bootloader lee el kernel por int 0x13; el FS queda en el
  disco (leído por ATA).
- **CD (El Torito)**: la BIOS carga la imagen completa en 0x7C00; el
  bootloader copia el kernel a 0x10000 y el FS a 0x140000 (RAM), y le pasa
  `boot_info` al kernel (modo `BOOTINFO_MODE_CD`).

El tamaño del FS se controla con `FS_SECTORS` (Makefile y boot.asm, deben
coincidir; hoy 560). Si el contenido natural del FS supera ese tamaño, el
`makefs.py` + `truncate` lo corta silenciosamente y los últimos archivos
quedan ilegibles (bug histórico, ver bitácora).

## GDT

| Selector | Uso |
|----------|-----|
| 0x00 | NULL |
| 0x08 | Código ring 0 |
| 0x10 | Datos ring 0 |
| 0x18 (RPL3: 0x1B) | Código ring 3 |
| 0x20 (RPL3: 0x23) | Datos ring 3 |
| 0x28 | TSS (`esp0` = pila de kernel para interrupciones desde ring 3) |
| 0x30 (RPL3: 0x33) | FS de usuario, base = `WIN32_TIB_VA` 0x84000000 (TIB mingw) |

## Pila de arranque (kernel)

- `_start` (entry.asm) fija `esp` en una pila de `.bss` de 16 KB y llama a
  `kmain(boot_info)`.
- `_start` también zeroea `.bss` (`__bss_start..__bss_end`): sin esto, el
  arranque por CD corrompe variables del scheduler (ver bitácora).

## Estado actual (Fases 0-17)

Hito alcanzado: **escritorio funcional** (wallpaper + taskbar + explorer +
visor de texto) sobre un gestor de ventanas en el kernel, con ratón y
teclado, y escalera de compatibilidad .exe 14/14 PASS. Ver README.md y
DESIGN.md para el detalle por fase.