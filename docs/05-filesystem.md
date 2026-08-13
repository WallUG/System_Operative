# MyOS — Filesystem MEFS

Archivos: `kernel/fs/mefs.c|h`, `tools/makefs.py`, `boot/boot.asm`
(`FS_SECTORS`), `Makefile` (`FS_SECTORS`).

## Formato (512 B/sector)

| Sector | Contenido |
|--------|-----------|
| 0 | **Superbloque**: magic `"MEFS01\n\0"` (8 B), `uint32 num_files`, `uint32 dir_lba` (absoluto), `uint32 dir_size`, `uint32 next_free_lba` (Fase E, offset 20) |
| 1.. | **Directorio**: N entradas de 32 B: `name[16]`, `size`, `lba` (absoluto), `unused` |
| ... | **Datos**: archivos en sectores contiguos |

`next_free_lba` es el primer sector libre (allocator "bump" de la Fase E);
`makefs.py` lo fija al primer sector tras los datos, y `mefs_write` lo
avanza al asignar sectores. `mefs_flush` vuelve a escribir el superbloque
(con `next_free_lba`) y el directorio al disco.

- Nombres: hasta 15 caracteres (16 B con NUL); case-sensitive.
 - Hasta `MEFS_MAX_FILES` (32) archivos.
 - **Escritura** (Fase E): en modo disco (ATA) los archivos se pueden crear,
   sobrescribir y eliminar, y los cambios **persisten** en el disco. En modo
   CD (imagen RAM) el FS es ROM y `mefs_writable()` devuelve 0.

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

Para escritura, `fs_write_sector(lba, buf)`:

- **Disco**: `ata_write_sector` (comando ATA `0x30`, PIO, Fase E).
- **CD/RAM**: devuelve -1 (imagen de solo lectura).

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
| `mefs_create(name)` | Crea un archivo vacío (Fase E) |
| `mefs_write(name, buf, len)` | Sobrescribe el archivo (reasigna sectores) |
| `mefs_delete(name)` | Elimina el archivo |
| `mefs_flush()` | Persiste superbloque + directorio al disco |
| `mefs_writable()` | 1 si escribe a disco (ATA), 0 en CD/RAM |

## Syscalls de FS

| Syscall | Nº | Semántica |
|---------|----|-----------|
| `SYS_FSIZE` | 8 | `ebx=nombre` → tamaño o -1 |
| `SYS_FREAD` | 9 | `ebx=nombre, ecx=buf, edx=input` → bytes o -1 |
| `SYS_DREAD` | 12 | `ebx=nombre, ecx=buf, edx=off, esi=max` (posicional) |
| `SYS_DLIST` | 13 | `ebx=idx, ecx=name[16], edx=&size` (enumerar) |
| `SYS_FCREATE` | 26 | `ebx=nombre` → crea archivo vacío (Fase E) |
| `SYS_FWRITE` | 27 | `ebx=nombre, ecx=buf, edx=len` → sobrescribe |
| `SYS_FDELETE` | 28 | `ebx=nombre` → elimina |
| `SYS_FLUSH` | 29 | persiste superbloque + directorio al disco |

## Construcción de la imagen

`tools/makefs.py` empaqueta los archivos lista en el orden dado; el
Makefile rellena a `FS_SECTORS` (**1400** hoy) con `truncate` para que
`boot.asm` pueda copiar el FS entero en el arranque por CD. La Fase E
subió de 1130 a 1400 para que el FS base (que llegaba a ~1208 sectores
relativos con 29 archivos) no se truncara al añadir `writetest.exe`.

> **ADVERTENCIA (bug #6 Fase 17)**: si el contenido natural del FS supera
> `FS_SECTORS`, el `truncate` lo **corta** y los últimos archivos quedan
> ilegibles ("error leyendo archivo"). Si se añaden apps grandes, hay que
> recalcular: `python3 tools/makefs.py ... | grep sectores` y subir
> `FS_SECTORS` en Makefile + boot.asm (+ reserva PMM en pmm.c).

## Contenido actual (20 archivos)

`hello.elf`, `mouseinfo.elf`, `win_demo.elf`, `win_two.elf`, `desktop.elf`,
`explorer.elf`, `fork.exe`, `exec.exe`, `console.exe`, `winapi.exe`,
`quick.exe`, `kernel32.elf`, `user32.elf`, `ntdll.elf`, `msvcrt.elf`,
`gdi32.elf`, `comctl32.elf`, `comdlg32.elf`, `advapi32.elf`, `shell32.elf`,
`hello_win.exe`, `dir.exe`, `proc.exe`, `messagebox.exe`, `gdidemo.exe`,
`writetest.exe`, `metapad.exe`, `readme.txt`.