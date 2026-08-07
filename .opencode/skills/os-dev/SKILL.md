---
name: os-dev
description: Guía completa y accionable para diseñar, programar, compilar, depurar y probar un sistema operativo desde cero (bootloader, kernel, drivers, memoria, filesystem, shell) usando Ensamblador (NASM/GAS), C, C++ y opcionalmente C# (.NET Native AOT / runtime bare-metal tipo Cosmos). ÚSALA SIEMPRE que el usuario mencione: "sistema operativo", "SO", "OS", "kernel", "bootloader", "boot sector", "modo protegido", "modo real", "long mode", "GDT", "IDT", "paginación", "multitarea", "driver de bajo nivel", "osdev", "QEMU/Bochs para SO", o pida crear/depurar/extender un OS propio, aunque no use la palabra "skill" ni pida el proceso completo explícitamente. También aplica si piden solo una parte (p. ej. "hazme un bootloader en ASM" o "cómo hago paginación en modo protegido"). No uses esta skill para sistemas operativos ya existentes tipo Linux/Windows (administración, scripting, etc.) ni para hypervisors/VMs de propósito general sin relación con desarrollo de kernel propio.
---

# Desarrollo de un Sistema Operativo desde Cero

Esta skill convierte a un agente de IA en el "ingeniero de OS" a cargo de todo el ciclo: diseño → bootloader en ensamblador → kernel en C/C++ (o C# bare-metal) → drivers → memoria → filesystem → shell → pruebas en QEMU/Bochs → depuración con GDB. Sigue esta guía de forma disciplinada y con lujo de detalle; no omitas pasos aunque parezcan obvios, porque en desarrollo de OS los errores de bajo nivel (alineación, segmentación, ABI, endianness) rompen todo silenciosamente.

## Filosofía de trabajo (léelo primero)

1. **Nunca saltes al kernel sin bootloader funcional probado en emulador.** Cada capa debe arrancar y mostrar evidencia visible (texto en pantalla, print serial) antes de construir la siguiente.
2. **Compila y ejecuta en QEMU después de cada cambio significativo.** No acumules código sin probar: en modo real/protegido un solo byte mal alineado cuelga la máquina sin mensaje de error.
3. **Documenta el mapa de memoria y el ABI antes de escribir código.** Decide de entrada: arquitectura objetivo (recomendado: x86_64 o i386 para simplicidad, ARM64 como alternativa), convención de llamada, layout de memoria (dónde vive el kernel, la pila, el heap, el framebuffer).
4. **Usa control de versiones (git) desde el primer commit** con commits pequeños y descriptivos (p. ej. "boot: entra en modo protegido de 32 bits").
5. **Prioriza correctitud sobre features.** Un kernel minimalista que arranca, maneja interrupciones y hace print a pantalla es más valioso que uno ambicioso que no compila.

## Elección de lenguaje y arquitectura (pregunta si no está definido)

Antes de escribir una sola línea, confirma con el usuario (o asume explícitamente y dilo):

- **Arquitectura objetivo**: x86_64 (recomendada, mejor documentación/tooling), i386/x86 de 32 bits (más simple para aprender), o ARM64/RISC-V (más moderno, menos ejemplos).
- **Lenguaje del kernel**: 
  - **C** — estándar de facto en OSDev (Linux, xv6, muchos tutoriales). Recomendado por defecto.
  - **C++** — permite abstracciones (clases, templates) pero requiere desactivar excepciones/RTTI y proveer stubs de runtime (`operator new`, ABI de Itanium para constructores globales).
  - **C#** — NO se puede correr directamente sobre metal desnudo con el runtime normal de .NET. Dos caminos viables: (a) **.NET Native AOT / CoreRT** compilando a código nativo sin runtime administrado y usando un kernel "shim" en C/ASM que expone syscalls mínimas; (b) el enfoque **Cosmos (C# Open Source Managed Operating System)**, que usa IL2CPU para traducir IL a código nativo. Documenta esto explícitamente al usuario: es un camino válido pero mucho más experimental y con menos control de bajo nivel que C/C++.
  - **Ensamblador (NASM para sintaxis Intel o GAS/AT&T)** es OBLIGATORIO en cualquier caso para: el sector de arranque (512 bytes, modo real 16 bits), la transición a modo protegido/long mode, el stub de entrada del kernel, el manejo de interrupciones a bajo nivel (ISR/IRQ stubs), y el cambio de contexto (context switch) en multitarea.
- Si el usuario no especifica, **usa por defecto: x86_64, kernel en C, con ensamblador NASM para boot/interrupts/context-switch**, y menciona C++ y C# como alternativas con sus trade-offs.

## Flujo de trabajo por fases

Sigue estas fases en orden. Cada fase tiene su propio archivo de referencia con el detalle técnico completo — ábrelo cuando llegues a esa fase, no cargues todo de golpe.

### Fase 0 — Entorno y toolchain
Configura el entorno de compilación cruzada (cross-compiler), NASM/GAS, QEMU, GDB, y el Makefile/linker script base.
→ Lee `references/00-toolchain.md`

### Fase 1 — Bootloader en Ensamblador
Sector de arranque de 512 bytes en modo real 16 bits, carga del kernel desde disco, habilitación de A20, transición a modo protegido de 32 bits (GDT) y opcionalmente a long mode de 64 bits (paging mínimo + PAE).
→ Lee `references/01-bootloader-asm.md`

### Fase 2 — Entrada del kernel y freestanding C/C++
Punto de entrada en ASM que llama a `kmain()`, linker script, runtime mínimo en C/C++ sin libc estándar (freestanding), multiboot2 si se usa GRUB como alternativa al bootloader propio.
→ Lee `references/02-kernel-entry-c-cpp.md`

### Fase 3 — Interrupciones y excepciones (IDT)
Tabla de descriptores de interrupción, ISR/IRQ stubs en ASM, PIC/APIC, manejo de excepciones de CPU (page fault, GPF, double fault), teclado y timer (PIT) como primeros drivers.
→ Lee `references/03-interrupts-idt.md`

### Fase 4 — Gestión de memoria
Physical memory manager (bitmap/buddy allocator), paginación (page tables x86_64), heap del kernel (kmalloc/kfree), mapeo de memoria de usuario vs kernel.
→ Lee `references/04-memory-management.md`

### Fase 5 — Multitarea y drivers
Cambio de contexto (context switch) en ASM, scheduler (round-robin simple), modelo de drivers (VGA/framebuffer, teclado, disco ATA/AHCI, serial UART para debug).
→ Lee `references/05-multitasking-drivers.md`

### Fase 6 — Sistema de archivos y espacio de usuario
Filesystem simple (FAT32 de solo lectura o un FS propio), modo usuario (ring 3), syscalls, carga de ejecutables (formato propio o ELF mínimo), shell interactiva.
→ Lee `references/06-filesystem-userland.md`

### Fase 7 — Pruebas, depuración y CI
Cómo correr y depurar en QEMU con GDB remoto, checklist de pruebas por fase, automatización opcional en CI (GitHub Actions) que compile y arranque el OS en modo headless.
→ Lee `references/07-testing-debugging.md`

## Cómo debe operar el agente de IA en este proyecto

- **Antes de escribir código**, resume en 5-10 líneas el plan de la fase actual (qué archivos vas a crear/tocar, qué comportamiento observable debe resultar).
- **Crea siempre los archivos reales** con las herramientas de archivo (no solo los muestres en el chat): `boot.asm`, `linker.ld`, `Makefile`, `kernel/kmain.c`, etc.
- **Compila y ejecuta tras cada cambio** con el Makefile del proyecto y `qemu-system-x86_64` (o `i386`), reportando el resultado real (salida de consola/serial, o captura si aplica) — no asumas que compiló bien, ejecuta el comando y revisa el código de salida.
- **Cuando algo falle** (triple fault, kernel panic, cuelgue), usa la metodología de `references/07-testing-debugging.md` (GDB + `-d int,cpu_reset` en QEMU) antes de adivinar la causa.
- **Explica las decisiones de bajo nivel** que tomes (por qué GDT con esos límites, por qué ese layout de memoria) en comentarios dentro del código y, si el usuario lo pide, en un `DESIGN.md` del proyecto.
- **No mezcles fases**: no escribas scheduler antes de tener paginación si el diseño lo requiere; sigue las dependencias reales entre subsistemas.
- Si el usuario pide "todo de una vez", igual constrúyelo fase por fase dentro de la misma sesión, pero deja claro el checkpoint de cada fase (qué se puede probar en ese punto).

## Estructura de proyecto recomendada

```
myos/
├── boot/
│   ├── boot.asm            # sector de arranque (512 bytes)
│   └── switch_pm.asm       # transición a modo protegido/long
├── kernel/
│   ├── entry.asm           # punto de entrada llamado desde boot
│   ├── kmain.c              # función principal del kernel
│   ├── idt.c / idt.asm      # tabla de interrupciones + stubs
│   ├── mm/                  # memory management
│   ├── drivers/             # vga, keyboard, timer, disk, serial
│   ├── fs/                  # filesystem
│   └── task/                # scheduler, context switch
├── libc/                    # freestanding mini-libc (memcpy, strlen...)
├── linker.ld                 # script del linker
├── Makefile
├── grub.cfg                  # si se usa multiboot2
└── DESIGN.md
```

## Comandos base que el agente usará con frecuencia

```bash
# Toolchain (Fase 0 detalla la instalación completa)
nasm -f bin boot/boot.asm -o boot.bin
nasm -f elf64 kernel/entry.asm -o entry.o
x86_64-elf-gcc -ffreestanding -fno-stack-protector -mno-red-zone -c kernel/kmain.c -o kmain.o
x86_64-elf-ld -T linker.ld -o kernel.elf entry.o kmain.o
objcopy -O binary kernel.elf kernel.bin

# Crear imagen de disco booteable
cat boot.bin kernel.bin > os-image.bin

# Ejecutar en QEMU
qemu-system-x86_64 -drive format=raw,file=os-image.bin -serial stdio

# Depurar con GDB
qemu-system-x86_64 -s -S -drive format=raw,file=os-image.bin &
gdb -ex "target remote localhost:1234" -ex "symbol-file kernel.elf"
```

## Referencias

- `references/00-toolchain.md` — instalación de cross-compiler, NASM, QEMU, GDB, Makefile base
- `references/01-bootloader-asm.md` — boot sector, A20, GDT, modo protegido/long mode
- `references/02-kernel-entry-c-cpp.md` — entrada del kernel, freestanding C/C++, multiboot2
- `references/03-interrupts-idt.md` — IDT, ISR/IRQ, PIC/APIC, excepciones
- `references/04-memory-management.md` — physical/virtual memory, paginación, heap
- `references/05-multitasking-drivers.md` — context switch, scheduler, drivers
- `references/06-filesystem-userland.md` — filesystem, modo usuario, syscalls, shell
- `references/07-testing-debugging.md` — QEMU+GDB, checklist de pruebas, CI
- `references/08-csharp-bare-metal.md` — camino alternativo en C# (Native AOT / Cosmos)
