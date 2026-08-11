# MyOS — Gestión de memoria

Archivos: `kernel/mem/pmm.c|h`, `kernel/mem/mmap.c|h`,
`kernel/mem/paging.c|h`, `kernel/mem/heap.c|h`, `kernel/entry.asm`,
`linker.ld`.

## 1. Mapa de memoria física: E820

`mmap.c` parsea el buffer E820 que dejó el bootloader (contador + 32
entradas de 5 dwords). Con QEMU: 6 entradas, ~127 MiB usables. Los tipos
se filtran a `usable`; la RAM de 0 a 1 MiB se marca como reservada.

## 2. PMM (Physical Memory Manager)

- **Bitmap** en 0x20000 (1 bit por frame de 4 KiB; `PMM_BLOCK_SIZE`).
- `PMM_FIRST_FREE` = 1 MiB: nunca se entrega memoria < 1 MiB (kernel,
  PD/PT, pilas, BIOS).
- `pmm_reserve_range(base, size)` marca frames como usados (se llama al
  init para la banda baja y para el LFB: `paging_is_lfb_frame`).
- API: `pmm_alloc_frame()` / `pmm_free_frame()` / `pmm_free_count()` /
  `pmm_reserve_range()`.

### Reservas actuales (pmm_init)

| Rango | Motivo |
|-------|--------|
| < 1 MiB | Kernel, IVT, BIOS, VGA |
| 0x100000..0x186000 | PD/PT del kernel, pilas de tareas de arranque, **imagen MEFS en RAM (CD)**: el FS en RAM vive en 0x140000..0x186000 (560 sectores) y jamás debe entregarse |

## 3. Paginación

- **PD global del kernel**: identity map 0-1 GiB con **páginas de 4 MiB**
  (PSE, `CR4_PSE = 0x10`), supervisor (sin bit USER). Sin mapeo ≥ 1 GiB.
- `paging_switch(cr3)` conmuta el espacio (`mov cr3, addr`): **esto
  flushea la TLB**, requisito tras `exec` (bug #2 de la Fase 17).
- PDs de usuario: copian los PDEs del kernel y mapean regiones de 4 KiB
  USER/RW en los PDEs 512..767 (0x80000000-0xBFFFFFFF):

| VA | Uso |
|----|-----|
| 0x80000000 | Imagen del programa (ELF o PE) |
| 0x84000000 | TIB Win32 (1 página USER; `%fs:0x18`) |
| 0x90000000-0xA0000000 | Heap de usuario (bump `SYS_MALLOC`) |
| 0xA8000000 | LFB VBE mapeado (512 páginas USER) |
| 0xB0000000-0xB3FFFFFF | DLLs Win32 fijas (4 × 1 MiB) |
| 0xC0000000 | Cima de la pila de usuario (1 página, crece abajo) |

- `paging_user_frame(pd, vaddr)` devuelve el frame físico de una página
  USER/RW (validación por página) o 0 — la base para leer buffers de ring 3
  desde el kernel (blits del WM, copias de syscalls, escritura de la IAT).
- `paging_is_user(pd, va)` valida punteros antes de copiar.

### Reglas de uso (seguridad)

- Todo puntero de ring 3 se copia por página validando con
  `paging_is_user`/`paging_user_frame` → un puntero inválido nunca produce
  #PF de kernel, devuelve -1/0.
- `user_memcpy_out` (kernel → usuario) y `user_memcpy_in` (usuario →
  kernel) son las únicas vías de copia curzada.

## 4. Heap del kernel

- First-fit sobre `0x2000000`, 4 MiB (`heap_init`/`kmalloc`/`kfree`).
- Usado por: snapshot del fondo del WM (800×600×4 ≈ 1.9 MiB),
  buffers de ejecución de binarios (lectura completa del .exe/.elf antes
  de cargar), etc.

## Bugs históricos relevantes

- `CR4_PSE` mal definido (0x4 en vez de 0x10) → la CPU interpretaba PDEs
  como punteros a PT y el kernel se "mapeaba" a la ROM → #PF triple.
- Doble asignación de frames (banda baja no reservada) → tarea demo B con
  el PD de una tarea de usuario encima de su pila → GPF en `iret`.
- TLB sin flushear tras `sys_exec` → primer fetch del entry con
  traducción obsoleta → #PF err=5 cr2=0 (Fase 17, ver DESIGN.md).