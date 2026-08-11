# MyOS — Filesystem MEFS

Archivos: `kernel/fs/mefs.c|h`, `tools/makefs.py`, `boot/boot.asm`
(`FS_SECTORS`), `Makefile` (`FS_SECTORS`).

## Formato (512 B/sector)

| Sector | Contenido |
|--------|-----------|
| 0 | **Superbloque**: magic `"MEFS01\n\0"` (8 B), `uint32 num_files`, `uint32 dir_lba` (absoluto), `uint32 dir_size` |
| 1.. | **Directorio**: N entradas de 32 B: `name[16]`, `size`, `lba` (absoluto), `unused` |
| ... | **Datos**: archivos en sectores contiguos |

- Nombres: hasta 15 caracteres (16 B con NUL); case-sensitive.
- Hasta `MEFS_MAX_FILES` (32) archivos.
- **Solo lectura** (por diseño del roadmap; escritura = trabajo futuro).

El FS empieza en el LBA absoluto **129** (`MEFS_FS_START` = 1 boot + 128
kernel). El directorio puede ocupar 1+ sectores; los datos se alinean
después.

## Fuentes de sector transparentes

`fs_read_sector(lba, buf)`:

- **Disco**: `ata_read_sector` (PIO).
- **CD/RAM**: copia de la imagen MEFS (`mefs_init_mem(image, size)`): el
  sector LBA se traduce restando `MEFS_FS_START` y se copia desde
  `fs_ram + (lba - MEFS_FS_START) * 512`, validando contra
  `fs_ram_sectors`.

`kmain` elige según `boot_info`: `BOOTINFO_MODE_CD` → `mefs_init_mem`
(RAM); si no, `mefs_init` (ATA). Sin boot_info válido → ATA (fallback).

## API del kernel

| Función | Descripción |
|---------|-------------|
| `mefs_init()` | Carga superbloque + directorio desde ATA |
| `mefs_init_mem(image, size)` | Ídem desde imagen RAM (CD) |
| `mefs_file_count()` | Nº de archivos |
| `mefs_read(name, buf, max)` | Lee un archivo completo, entrada por entrada |
| `mefs_read_off(name, buf, off, max)` | Lectura posicional (salta `off%512` del primer sector, avanza `lba += off/512`) |
| `mefs_dir_get(idx, name)` | Nombre del archivo i-ésimo |
| `mefs_size(name)` | Tamaño o -1 |

## Syscalls de FS

| Syscall | Nº | Semántica |
|---------|----|-----------|
| `SYS_FSIZE` | 8 | `ebx=nombre` → tamaño o -1 |
| `SYS_FREAD` | 9 | `ebx=nombre, ecx=buf, edx=input` → bytes o -1 |
| `SYS_DREAD` | 12 | `ebx=nombre, ecx=buf, edx=off, esi=max` (posicional) |
| `SYS_DLIST` | 13 | `ebx=idx, ecx=name[16], edx=&size` (enumerar) |

## Construcción de la imagen

`tools/makefs.py` empaqueta los archivos lista en el orden dado; el
Makefile rellena a `FS_SECTORS` (560 hoy) con `truncate` para que
`boot.asm` pueda copiar el FS entero en el arranque por CD.

> **ADVERTENCIA (bug #6 Fase 17)**: si el contenido natural del FS supera
> `FS_SECTORS`, el `truncate` lo **corta** y los últimos archivos quedan
> ilegibles ("error leyendo archivo"). Si se añaden apps grandes, hay que
> recalcular: `python3 tools/makefs.py ... | grep sectores` y subir
> `FS_SECTORS` en Makefile + boot.asm (+ reserva PMM en pmm.c).

## Contenido actual (20 archivos)

`hello.elf`, `mouseinfo.elf`, `win_demo.elf`, `win_two.elf`, `desktop.elf`,
`explorer.elf`, `fork.exe`, `exec.exe`, `console.exe`, `winapi.exe`,
`quick.exe`, `kernel32.elf`, `user32.elf`, `ntdll.elf`, `msvcrt.elf`,
`hello_win.exe`, `dir.exe`, `proc.exe`, `messagebox.exe`, `readme.txt`.