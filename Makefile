# Makefile MyOS - Fase 2 (i386, kernel en C freestanding)
# Toolchain: gcc/ld del host en modo freestanding -m32 (ver DESIGN.md).

ASM       = nasm
CC        = gcc
LD        = ld
OBJCOPY   = objcopy
QEMU      = qemu-system-i386
KERNEL_SECTORS = 128            # tamano de kernel en sectores (1 = 512 bytes)
FS_SECTORS     = 64             # fs.bin rellenado a 64 sectores (boot.asm usa el mismo valor)

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
            $(BUILD)/mem/mmap.o \
            $(BUILD)/mem/pmm.o \
            $(BUILD)/mem/paging.o \
            $(BUILD)/mem/heap.o \
            $(BUILD)/task/task.o \
            $(BUILD)/task/switch.o \
            $(BUILD)/gdt.o \
            $(BUILD)/gdt_asm.o \
            $(BUILD)/syscall.o \
            $(BUILD)/elf.o \
            $(BUILD)/shell.o \
            $(BUILD)/fs/mefs.o \
            $(BUILD)/drivers/vga.o \
            $(BUILD)/drivers/serial.o \
            $(BUILD)/drivers/timer.o \
            $(BUILD)/drivers/keyboard.o \
            $(BUILD)/drivers/ata.o \
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

# Lo mismo para kernel/task/: task.c y switch.asm comparten nombre base.
$(BUILD)/task/task.o: kernel/task/task.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $< -o $@

$(BUILD)/task/switch.o: kernel/task/switch.asm
	@mkdir -p $(dir $@)
	$(ASM) -f elf32 $< -o $@

# Fase 7: programas de usuario como ELF32 ET_EXEC enlazados en
# 0x80000000 (region de usuario del kernel). El kernel los mapea en un
# PD aislado (proteccion de memoria ring 3).
USER_TEXT = 0x80000000
USER_SRCS = user/hello.c user/fork.c user/exec.c
USER_ELFS = $(patsubst user/%.c,$(BUILD)/user/%.elf,$(USER_SRCS))

$(BUILD)/user/%.elf: user/%.c tools/user.ld
	@mkdir -p $(dir $@)
	$(CC) -m32 -ffreestanding -fno-pic -fno-stack-protector -fno-builtin \
	      -fno-asynchronous-unwind-tables -Wall -Wextra -O2 -c $< -o $(BUILD)/user/$*.o
	$(LD) -m elf_i386 -nostdlib -T tools/user.ld -o $@ $(BUILD)/user/$*.o

# Filesystem MEFS: superbloque + directorio + datos (solo lectura).
# Se rellena a FS_SECTORS para que boot.asm pueda copiarlo entero a RAM
# en el arranque por CD (la imagen os-image.bin lo lleva en LBA 129..).
$(BUILD)/fs.bin: $(USER_ELFS)
	@mkdir -p $(dir $@)
	python3 tools/makefs.py $(USER_ELFS) -o $@
	@truncate -s $$(( $(FS_SECTORS) * 512 )) $@

kernel.elf: $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

$(BUILD)/kernel.bin: kernel.elf
	$(OBJCOPY) -O binary $< $@
	@truncate -s $$(( $(KERNEL_SECTORS) * 512 )) $@

os-image.bin: $(BUILD)/boot.bin $(BUILD)/kernel.bin $(BUILD)/fs.bin
	cat $^ > $@
	@echo "OK: os-image.bin = $$(wc -c < $@) bytes (boot+kernel+MEFS)"

run: os-image.bin
	$(QEMU) -drive format=raw,file=$<

test: os-image.bin
	$(QEMU) -display none -monitor none -serial stdio -no-reboot \
	        -drive format=raw,file=$<

# Fase 7: ISO9660 booteable (El Torito no-emulation, Python puro).
iso: os-image.bin
	python3 tools/makeiso.py $< $(USER_ELFS) -o $(BUILD)/myos.iso

boot_cd: iso
	$(QEMU) -cdrom $(BUILD)/myos.iso -boot order=d

test_cd: iso
	$(QEMU) -display none -monitor none -serial stdio -no-reboot \
	        -cdrom $(BUILD)/myos.iso -boot order=d

debug: os-image.bin
	$(QEMU) -s -S -drive format=raw,file=$< &
	@echo "Conecta con: gdb -ex 'target remote localhost:1234'"
	@sleep 1

clean:
	rm -rf $(BUILD) os-image.bin kernel.elf

.PHONY: all run test debug clean
