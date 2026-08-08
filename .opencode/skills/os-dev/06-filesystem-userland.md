# Fase 6 — Sistema de Archivos y Espacio de Usuario

## Filesystem

Opciones, de más simple a más compatible:

1. **FS propio trivial** (recomendado para aprender): un array de "archivos" con nombre fijo y datos en bloques contiguos, escrito directamente en un formato que tú diseñas y documentas en `DESIGN.md`. Ideal para el primer OS porque evita toda la complejidad de FAT32.
2. **FAT32 de solo lectura**: mucho más útil porque permite crear la imagen de disco con herramientas estándar del host (`mkfs.vfat`, montar y copiar archivos) y leerla desde el kernel. Implementar solo lectura (leer directorio raíz, seguir la cadena de clusters vía la FAT) ya es suficiente para cargar programas de usuario.
3. **ext2 minimal**: más complejo, útil si el objetivo final es compatibilidad con Linux, generalmente no recomendado como primer filesystem.

### Driver de disco (ATA PIO, base para leer el filesystem)

```c
void ata_read_sector(uint32_t lba, uint8_t* buffer) {
    outb(0x1F6, 0xE0 | ((lba >> 24) & 0x0F));
    outb(0x1F2, 1);                          // 1 sector
    outb(0x1F3, (uint8_t)lba);
    outb(0x1F4, (uint8_t)(lba >> 8));
    outb(0x1F5, (uint8_t)(lba >> 16));
    outb(0x1F7, 0x20);                        // comando READ SECTORS
    while (!(inb(0x1F7) & 0x08));             // esperar DRQ
    for (int i = 0; i < 256; i++) {
        uint16_t data = inw(0x1F0);
        buffer[i*2] = data & 0xFF;
        buffer[i*2+1] = data >> 8;
    }
}
```

## Modo usuario (ring 3)

Requiere: (a) segmentos de código/datos de usuario en la GDT (DPL=3), (b) TSS (Task State Segment) configurado con el `rsp0` correcto para que la CPU sepa a qué pila de kernel saltar al recibir una interrupción desde ring 3, (c) transición inicial vía `iretq` con los selectores y flags apropiados en la pila simulada.

```c
void enter_usermode(uint64_t entry, uint64_t user_stack) {
    // pila preparada manualmente para que iretq "regrese" a ring 3
    __asm__ volatile (
        "mov $0x23, %%ax \n"   // selector de datos usuario | RPL=3
        "mov %%ax, %%ds \n"
        "mov %%ax, %%es \n"
        "mov %%ax, %%fs \n"
        "mov %%ax, %%gs \n"
        "pushq $0x23 \n"        // SS
        "pushq %0 \n"            // RSP usuario
        "pushfq \n"               // RFLAGS
        "pushq $0x1B \n"          // CS selector código usuario | RPL=3
        "pushq %1 \n"              // RIP = entry point
        "iretq \n"
        :: "r"(user_stack), "r"(entry) : "rax"
    );
}
```

## Syscalls

Vía interrupción de software (`int 0x80`, clásico) o vía `syscall`/`sysret` (más rápido, moderno, requiere configurar los MSR `STAR`/`LSTAR`/`SFMASK`). Para un primer OS, `int 0x80` es más simple de depurar.

```c
// convención: rax = número de syscall, rbx/rcx/rdx = argumentos
void syscall_handler(registers_t* regs) {
    switch (regs->rax) {
        case SYS_WRITE: sys_write((char*)regs->rbx, regs->rcx); break;
        case SYS_EXIT:  sys_exit(regs->rbx); break;
        // ...
    }
}
```

## Carga de ejecutables

Empieza con un **formato binario plano propio** (el archivo es directamente código máquina, se copia a una dirección fija y se salta ahí) — mucho más simple que parsear ELF. Cuando el sistema madure, migra a un **parser de ELF mínimo** (leer el header, secciones `PT_LOAD`, mapearlas según sus flags) para poder compilar programas de usuario con el toolchain estándar.

## Shell interactiva mínima

Bucle: leer línea del teclado (usando el buffer de Fase 3) → parsear comando → ejecutar función interna (`ls`, `cat`, `echo`) o cargar y saltar a un binario de usuario. Es el primer punto donde el OS empieza a "sentirse" como un sistema operativo usable.

```c
void shell_loop(void) {
    char line[128];
    for (;;) {
        print("myos> ");
        read_line(line, sizeof(line));
        if (strcmp(line, "ls") == 0) fs_list_root();
        else if (strncmp(line, "cat ", 4) == 0) fs_cat(line + 4);
        else if (strlen(line) > 0) print("comando no encontrado\n");
    }
}
```

## Checklist final de la fase
- [ ] El driver de disco lee sectores correctamente (verificar contra contenido conocido escrito desde el host)
- [ ] El filesystem elegido lista archivos y lee su contenido correctamente
- [ ] Un binario de usuario mínimo (p. ej. que solo llama a `SYS_EXIT`) se carga y ejecuta en ring 3 sin crashear el kernel
- [ ] La shell responde a al menos 2-3 comandos internos y puede lanzar un binario de usuario
