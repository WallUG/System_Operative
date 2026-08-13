# MyOS — Filesystem MEFS

Archivos: `kernel/fs/mefs.c|h`, `tools/makefs.py`, `boot/boot.asm`
(`FS_SECTORS`), `Makefile` (`FS_SECTORS`).

## Formato v2 (512 B/sector, Fase 20-A)

| Sector | Contenido |
|--------|-----------|
| 0 | **Superbloque** (512 B, ver offsets abajo) |
| 1..+dir | **Directorio**: `MEFS_MAX_FILES` (64) entradas de 32 B |
| +bitmap | **Bitmap de bloques libres** (1 bit por sector de datos) |
| +data | **Datos**: bloques asignados vía bitmap |

Superbloque (offsets):
- `0` magic `"MEFS02\n\0"`
- `8` `uint32 num_files`
- `12` `uint32 dir_lba` (absoluto)
- `16` `uint32 dir_size`
- `20` `uint32 bitmap_lba`
- `24` `uint32 bitmap_sectors`
- `28` `uint32 data_start`
- `32` `uint32 fs_capacity` (sectores de la región FS del disco)

Entrada de directorio (32 B): `name[16]`, `size`, `lba`, `flags`, `parent`.
- `flags` bit0 = `IS_DIR`.
- `parent` = índice de la entrada del directorio padre; `0xFFFFFFFF` = raíz.

- Nombres: hasta 15 caracteres (16 B con NUL); case-sensitive.
- Hasta `MEFS_MAX_FILES` (64) entradas (raíz + subdirectorios).
- **Escritura** (Fase E): en modo disco (ATA) los archivos se pueden crear,
  sobrescribir y eliminar, y los cambios **persisten** en el disco. En modo
  CD (imagen RAM) el FS es ROM y `mefs_writable()` devuelve 0.
- **Bitmap** (Fase 20-A): `mefs_write` libera los bloques viejos del archivo
  y asigna un run contiguo de bloques libres vía `bm_alloc`; `mefs_delete`
  los libera con `bm_free`. `mefs_format` escribe un superbloque limpio
  (num_files=0) y un bitmap todo a 0 (todo libre).
- **Subdirectorios** (Fase 20-A): `mefs_mkdir(name, parent)` crea una entrada
  `IS_DIR` con el índice del padre; `mefs_ls(parent, idx, ...)` enumera los
  hijos de un directorio; `mefs_lookup`/`mefs_parent`/`mefs_name` navegan.
  La API de la raíz (`mefs_list`/`mefs_dir_get`/`SYS_DLIST`) sigue listando
  solo archivos de la raíz, para compatibilidad con metapad.

El FS empieza en el LBA absoluto **129** (`MEFS_FS_START` = 1 boot + 128
kernel). El directorio (2048 B) y el bitmap (1-4 sectores) se alinean tras
el superbloque; los datos empiezan en `data_start`.

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
| `mefs_dir_get(idx, name)` | Nombre del archivo raíz i-ésimo |
| `mefs_size(name)` | Tamaño o -1 |
| `mefs_create(name)` | Crea un archivo vacío en la raíz (Fase E) |
| `mefs_write(name, buf, len)` | Sobrescribe el archivo (asigna bloques vía bitmap) |
| `mefs_delete(name)` | Elimina el archivo (libera sus bloques) |
| `mefs_flush()` | Persiste superbloque + directorio al disco |
| `mefs_writable()` | 1 si escribe a disco (ATA), 0 en CD/RAM |
| `mefs_ls(parent, idx, name, &size, &flags)` | Enumerar hijo idx-ésimo de `parent` (Fase 20-A) |
| `mefs_mkdir(name, parent)` | Crea un subdirectorio |
| `mefs_lookup(parent, name)` | Índice de una entrada bajo `parent`, o -1 |
| `mefs_parent(idx)` | Índice del padre de `idx` |
| `mefs_name(idx, name)` | Nombre de la entrada `idx` |
| `mefs_format(capacity)` | Formatea el disco: superbloque limpio + bitmap a 0 |

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

`tools/makefs.py` empaqueta los archivos lista en el orden dado, genera el
superbloque v2 + bitmap (marcando usados los bloques de cada archivo) y el
directorio con `parent=raíz`. El Makefile pasa `-c $(FS_SECTORS)` (**1400**
hoy) como `fs_capacity` y rellena a `FS_SECTORS` con `truncate` para que
`boot.asm` pueda copiar el FS entero en el arranque por CD. La Fase E subió
de 1130 a 1400 para que el FS base (que llegaba a ~1208 sectores relativos
con 29 archivos) no se truncara al añadir `writetest.exe`.

## Comandos de shell (Fase 20-A)

Además de `ls`/`cat`/`touch`/`write`/`rm`/`flush`, la shell soporta:

- `mkdir <dir>` — crea un subdirectorio en el directorio actual.
- `cd <dir>` / `cd ..` — cambia de directorio.
- `pwd` — muestra el directorio actual.
- `format` — formatea el disco (borra todo; superbloque limpio + bitmap a 0).
- `ls [dir]` — lista el directorio actual o el subdirectorio dado; los
  directorios se marcan con `/`.

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