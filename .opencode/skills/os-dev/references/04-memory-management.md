# Fase 4 — Gestión de Memoria

## Capas a construir, en orden

1. **Mapa de memoria física** — obtenido de la BIOS (`int 0x15, eax=0xE820`) antes de salir de modo real, o del Multiboot2 info si se usa GRUB. Registra qué rangos son usables, reservados, ACPI, etc.
2. **Physical Memory Manager (PMM)** — asigna/libera frames físicos de tamaño fijo (típicamente 4 KB).
3. **Paginación (virtual memory)** — tablas de página x86_64 de 4 niveles (PML4 → PDPT → PD → PT), mapea memoria virtual a física, separa espacio de kernel y de usuario.
4. **Heap del kernel** — `kmalloc`/`kfree` construidos sobre el PMM + paginación, para asignaciones dinámicas dentro del kernel.

## 1. Mapa de memoria (E820)

```nasm
; en modo real, antes de pmode
get_memory_map:
    mov di, MMAP_BUFFER
    xor ebx, ebx
    mov edx, 0x534D4150     ; "SMAP"
.loop:
    mov eax, 0xE820
    mov ecx, 24
    int 0x15
    jc .done
    add di, 24
    cmp ebx, 0
    jne .loop
.done:
    ret
```
Cada entrada trae base, longitud y tipo (1 = usable, otros = reservado/ACPI/etc). Guarda esta tabla en una zona de memoria fija que el kernel en C pueda leer luego.

## 2. Physical Memory Manager — bitmap allocator (simple y recomendado para empezar)

```c
#define PAGE_SIZE 4096
static uint8_t* bitmap;
static size_t total_frames;

void pmm_set_used(size_t frame)  { bitmap[frame/8] |=  (1 << (frame%8)); }
void pmm_set_free(size_t frame)  { bitmap[frame/8] &= ~(1 << (frame%8)); }
int  pmm_test(size_t frame)      { return bitmap[frame/8] & (1 << (frame%8)); }

void* pmm_alloc_frame(void) {
    for (size_t i = 0; i < total_frames; i++) {
        if (!pmm_test(i)) { pmm_set_used(i); return (void*)(i * PAGE_SIZE); }
    }
    return NULL;   // sin memoria física disponible
}

void pmm_free_frame(void* addr) {
    pmm_set_free((size_t)addr / PAGE_SIZE);
}
```
Inicializa marcando como "usado" todo lo reservado según el mapa E820, y como "libre" los rangos usables. Un allocator tipo *buddy system* es una mejora natural posterior si se necesita mejor rendimiento/fragmentación, pero el bitmap es suficiente para un primer OS.

## 3. Paginación x86_64 (4 niveles)

Jerarquía: **PML4** (512 entradas, cada una cubre 512 GB) → **PDPT** (cada entrada 1 GB) → **PD** (cada entrada 2 MB) → **PT** (cada entrada 4 KB, la página final).

```c
typedef uint64_t pte_t;
#define PAGE_PRESENT  (1ULL << 0)
#define PAGE_RW       (1ULL << 1)
#define PAGE_USER     (1ULL << 2)

void map_page(uint64_t* pml4, uint64_t virt, uint64_t phys, uint64_t flags) {
    size_t pml4_i = (virt >> 39) & 0x1FF;
    size_t pdpt_i = (virt >> 30) & 0x1FF;
    size_t pd_i   = (virt >> 21) & 0x1FF;
    size_t pt_i   = (virt >> 12) & 0x1FF;

    uint64_t* pdpt = get_or_create_table(pml4, pml4_i);
    uint64_t* pd    = get_or_create_table(pdpt, pdpt_i);
    uint64_t* pt    = get_or_create_table(pd, pd_i);

    pt[pt_i] = (phys & ~0xFFFULL) | flags | PAGE_PRESENT;
    __asm__ volatile ("invlpg (%0)" :: "r"(virt) : "memory");
}
```

`get_or_create_table` pide un frame nuevo al PMM si la entrada correspondiente aún no está presente, lo limpia a cero, y lo enlaza.

**Regla de oro**: nunca desmapear la región donde vive el propio código que está ejecutando el `map_page` (te "cortas la rama" — el siguiente `mov`/`ret` haría page fault sin handler capaz de recuperarse limpiamente en las primeras fases).

## 4. Heap del kernel (`kmalloc`/`kfree`)

Diseño simple recomendado para empezar: un allocator de lista enlazada de bloques libres (first-fit), operando sobre una región virtual reservada (p. ej. `0xFFFFFFFF80000000` en adelante) que se expande pidiendo frames al PMM y mapeándolos según se necesite.

```c
typedef struct block_header {
    size_t size;
    int free;
    struct block_header* next;
} block_header_t;

void* kmalloc(size_t size) {
    block_header_t* curr = heap_start;
    while (curr) {
        if (curr->free && curr->size >= size) { curr->free = 0; return (void*)(curr + 1); }
        curr = curr->next;
    }
    return expand_heap(size);   // pide más frames vía PMM + map_page
}

void kfree(void* ptr) {
    block_header_t* header = (block_header_t*)ptr - 1;
    header->free = 1;
    // (mejora posterior: coalescing de bloques libres adyacentes)
}
```

## Separación kernel/usuario

Convención común en x86_64: mitad superior del espacio virtual (`0xFFFF800000000000` en adelante) para el kernel, mitad inferior para cada proceso de usuario. Cada proceso tiene su propio PML4, pero todas comparten el mismo mapeo de la mitad superior (el kernel siempre visible, aunque solo accesible en ring 0 gracias al bit `PAGE_USER` ausente en esas entradas).

## Checklist antes de avanzar a Fase 5
- [ ] El PMM reporta correctamente memoria total/usada/libre coincidiendo con el mapa E820
- [ ] `map_page`/`unmap_page` verificados con una prueba: mapear una dirección virtual arbitraria a un frame físico conocido y confirmar lectura/escritura correcta
- [ ] `kmalloc`/`kfree` probados con múltiples asignaciones y liberaciones sin corromper el heap (agregar una función de "walk the heap" para debug)
- [ ] Page fault handler (excepción 14) implementado y probado deliberadamente (acceder a una dirección no mapeada debe dar diagnóstico, no triple fault)
