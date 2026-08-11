# MyOS — Arranque (bootloader + boot_info)

Archivos: `boot/boot.asm`, `kernel/entry.asm`, `kernel/bootinfo.h`,
`tools/makeiso.py`.

## Fase 1: BIOS → sector 0

La BIOS carga los primeros 512 bytes del medio (disco o imagen El Torito)
en 0x7C00 con `dl` = 0x80 (disco) o 0xE0+ (CD). El bootloader comprueba la
firma 0xAA55 (al final del sector) y salta al modo protegido:

1. **A20** habilitada (método del teclado 8042).
2. **GDT mínima** del boot (0x08 código, 0x10 datos).
3. `cr0.PE = 1` → **modo protegido** (32 bits).
4. Salto lejano a código de 32 bits (todavía identidad, sin paginación).

## Fase 2: cargar kernel (disco)

Con `dl < 0xE0` (disco):

- `int 0x13` / `AH=0x42` (LBA extendido) lee `KERNEL_SECTORS` (128) del
  LBA 1 a la dirección física 0x10000.
- El FS **no se copia**: queda en el disco y el kernel lo accede por ATA
  (`mefs_init`).

## Fase 2b: cargar kernel (CD, El Torito no-emulation)

Con `dl >= 0xE0` (CD), la BIOS ya cargó **la imagen completa**
(boot + kernel + FS) en RAM a partir de 0x7C00. El bootloader solo copia:

1. La imagen MEFS (FS_RAM_SRC = 0x17E00, `FS_SECTORS * 512` bytes) a
   **0x140000**.
2. El kernel (0x7E00, 64 KB) a 0x10000 **hacia atrás** (`std`) porque
   fuente y destino se solapan.

Prefijos críticos: `rep movsd` requiere los prefijos de tamaño de operando
(66) y de dirección (67), en ese orden (`db 0x67; rep movsd`). Sin el 67,
SI/DI son de 16 bits y las direcciones 0x17E00/0x140000 se truncan (bug
histórico que corrompía la IVT).

## Fase 3: memoria E820 y boot_info

- `get_mmap` (int 0x15, EAX=0xE820) rellena un array de hasta 32 entradas
  en 0x7E00: `{contador, base_low, base_high, len_low, len_high, type}`.
  **Bucle importante**: EAX=0xE820 debe re-declararse al inicio de cada
  iteración (la BIOS devuelve 'SMAP' en EAX; sin re-declarar, el mapa se
  corta a 1 entrada → PMM sin frames → "ELF invalido").
- `bootinfo_t` se escribe en **0x7000**:

```c
typedef struct {
    uint32_t magic;      /* 'MYOS' (0x4D594F53)               */
    uint32_t mode;       /* 0=disco, 1=CD (BOOTINFO_MODE_*)   */
    uint32_t fs_source;  /* disco: LBA del FS; CD: dir. física */
    uint32_t fs_size;    /* bytes (solo CD, múltiplo de 512)   */
    uint32_t kernel_addr;/* 0x10000 (informativo)              */
} bootinfo_t;
```

El puntero se empuja en la pila antes del salto al kernel; `entry.asm` lo
preserva y lo pasa a `kmain(boot_info)`.

## Fase 4: kernel

`kernel/entry.asm`:

1. Zeroea `.bss` (`__bss_start..__bss_end`) — crítico para el arranque CD.
2. Fija `esp` en la pila de kernel (`.bss`, 16 KB).
3. Llama a `kmain(boot_info)` (cdecl).

`kmain` (kernel/kmain.c) secuencia: serial → VGA → GDT → IDT → PIC → PIT →
PMM → paginación → heap → scheduler → syscalls → MEFS (según boot_info:
RAM/ATA) → win32_init → shell.

## Salvaguardas

- Sin `boot_info` válido (kernel desnudo desde el depurador), el kernel
  cae a `mefs_init()` (ATA) como fallback.
- La lectura del superbloque MEFS valida el magic `"MEFS01\n\0"` (8 bytes);
  falla → `MEFS: error leyendo filesystem` y sigue sin FS.