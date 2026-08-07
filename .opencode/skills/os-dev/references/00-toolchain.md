# Fase 0 — Entorno y Toolchain

## Herramientas necesarias

| Herramienta | Propósito |
|---|---|
| `nasm` | Ensamblador (sintaxis Intel) para boot.asm, ISR stubs, context switch |
| `gcc`/`g++` cross-compiler (`x86_64-elf-gcc`) | Compilar C/C++ freestanding sin depender de la libc del host |
| `binutils` cross (`x86_64-elf-ld`, `x86_64-elf-objcopy`) | Linkear y convertir ELF a binario plano |
| `qemu-system-x86_64` / `qemu-system-i386` | Emulador para arrancar el OS |
| `gdb` | Depuración remota conectada a QEMU |
| `make` | Automatizar build |
| `xorriso` + `grub-mkrescue` (opcional) | Si se usa GRUB/multiboot2 en vez de bootloader propio |

## Por qué un cross-compiler

El compilador del sistema host (Linux/macOS) asume que vas a linkear contra su libc y generar ejecutables para ese SO. Un kernel necesita:
- Ningún runtime del host (freestanding).
- Control total del layout de memoria (linker script propio).
- Ausencia de stack protector dependiente de libc, sin red zone en x86_64, sin SIMD flotante por defecto (evita corromper FPU/SSE antes de inicializarlo).

Instalación típica (Linux, resumen — details varían por distro):

```bash
# Dependencias de build
sudo apt-get install build-essential bison flex libgmp3-dev libmpc-dev libmpfr-dev texinfo nasm qemu-system-x86 gdb xorriso grub-pc-bin

# Compilar binutils + gcc cross para x86_64-elf (ver osdev.org "GCC Cross-Compiler" para versión exacta)
export PREFIX="$HOME/opt/cross"
export TARGET=x86_64-elf
export PATH="$PREFIX/bin:$PATH"
# ... build de binutils y luego gcc con --target=$TARGET --disable-nls --without-headers
```

Si compilar el cross-compiler no es viable en el entorno (p. ej. sandbox sin red más allá de paquetes permitidos), usa como alternativa:
- Flags de freestanding con el gcc del sistema: `-ffreestanding -fno-builtin -fno-stack-protector -mno-red-zone -mcmodel=kernel -nostdlib -nostartfiles`.
- Deja explícito en el `DESIGN.md` que se usó el compilador del host en modo freestanding como fallback, y que lo ideal en producción es un cross-compiler dedicado.

## Makefile base (plantilla)

```makefile
ASM = nasm
CC = x86_64-elf-gcc
LD = x86_64-elf-ld
CFLAGS = -ffreestanding -fno-stack-protector -fno-pie -mno-red-zone -mcmodel=kernel -Wall -Wextra -c
LDFLAGS = -T linker.ld -nostdlib

OBJS = build/entry.o build/kmain.o build/idt.o build/isr_stubs.o

all: os-image.bin

build/boot.bin: boot/boot.asm
	$(ASM) -f bin $< -o $@

build/%.o: kernel/%.asm
	$(ASM) -f elf64 $< -o $@

build/%.o: kernel/%.c
	$(CC) $(CFLAGS) $< -o $@

kernel.elf: $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

kernel.bin: kernel.elf
	objcopy -O binary kernel.elf kernel.bin

os-image.bin: build/boot.bin kernel.bin
	cat build/boot.bin kernel.bin > os-image.bin

run: os-image.bin
	qemu-system-x86_64 -drive format=raw,file=os-image.bin -serial stdio

debug: os-image.bin
	qemu-system-x86_64 -s -S -drive format=raw,file=os-image.bin &
	x86_64-elf-gdb -ex "target remote localhost:1234" -ex "symbol-file kernel.elf"

clean:
	rm -rf build/*.o *.bin *.elf
```

Ajusta rutas y flags según arquitectura (i386 vs x86_64) y según se use bootloader propio o GRUB/multiboot2 (Fase 2).

## Checklist antes de avanzar a Fase 1
- [ ] `nasm -v`, `qemu-system-x86_64 --version`, `gdb --version` responden correctamente
- [ ] Cross-compiler (o fallback freestanding) compila un `.c` trivial sin errores de linking contra libc del host
- [ ] `make` corre sin errores con un Makefile mínimo vacío
- [ ] Repositorio git inicializado con `.gitignore` para `*.o *.bin *.elf build/`
