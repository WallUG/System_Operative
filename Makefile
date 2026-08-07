# Makefile MyOS - Fase 2 (i386, kernel en C freestanding)
# Toolchain: gcc/ld del host en modo freestanding -m32 (ver DESIGN.md).

ASM       = nasm
CC        = gcc
LD        = ld
OBJCOPY   = objcopy
QEMU      = qemu-system-i386
KERNEL_SECTORS = 64             # tamano de kernel en sectores (1 = 512 bytes)

BUILD     = build
CFLAGS    = -m32 -ffreestanding -fno-stack-protector -fno-pic -fno-pie \
            -fno-builtin -fno-asynchronous-unwind-tables \
            -fno-optimize-sibling-calls -Wall -Wextra -O2 \
            -Ikernel -Ilibc -c
LDFLAGS   = -m elf_i386 -T linker.ld -nostdlib

OBJS      = $(BUILD)/entry.o \
            $(BUILD)/kmain.o \
            $(BUILD)/idt.o \
            $(BUILD)/isr.o \
            $(BUILD)/isr_handlers.o \
            $(BUILD)/pic.o \
            $(BUILD)/panic.o \
            $(BUILD)/kprint.o \
            $(BUILD)/drivers/vga.o \
            $(BUILD)/drivers/serial.o \
            $(BUILD)/drivers/timer.o \
            $(BUILD)/drivers/keyboard.o \
            $(BUILD)/libc/string.o

# VPATH: las fuentes .c viven en kernel/ o libc/; los .o replican su ruta
VPATH     = kernel:libc

all: os-image.bin

$(BUILD)/boot.bin: boot/boot.asm
	mkdir -p $(BUILD)
	$(ASM) -f bin $< -o $@
	@test $$(wc -c < $@) -eq 512 || (echo "ERROR: boot.bin no mide 512 bytes"; exit 1)

$(BUILD)/%.o: kernel/%.asm
	@mkdir -p $(dir $@)
	$(ASM) -f elf32 $< -o $@

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $< -o $@

# kernel/isr.c no puede usar la regla patron: build/isr.o ya lo ocupa
# kernel/isr.asm (stubs). Handler en C -> isr_handlers.o.
$(BUILD)/isr_handlers.o: kernel/isr.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $< -o $@

kernel.elf: $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

$(BUILD)/kernel.bin: kernel.elf
	$(OBJCOPY) -O binary $< $@
	@truncate -s $$(( $(KERNEL_SECTORS) * 512 )) $@

os-image.bin: $(BUILD)/boot.bin $(BUILD)/kernel.bin
	cat $^ > $@
	@echo "OK: os-image.bin = $$(wc -c < $@) bytes (512 + kernel)"

run: os-image.bin
	$(QEMU) -drive format=raw,file=$<

test: os-image.bin
	$(QEMU) -display none -monitor none -serial stdio -no-reboot \
	        -drive format=raw,file=$<

debug: os-image.bin
	$(QEMU) -s -S -drive format=raw,file=$< &
	@echo "Conecta con: gdb -ex 'target remote localhost:1234'"
	@sleep 1

clean:
	rm -rf $(BUILD) os-image.bin kernel.elf

.PHONY: all run test debug clean
