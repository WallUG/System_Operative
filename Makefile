# Makefile MyOS - Fase 2 (i386, kernel en C freestanding)
# Toolchain: gcc/ld del host en modo freestanding -m32 (ver DESIGN.md).

ASM       = nasm
CC        = gcc
LD        = ld
OBJCOPY   = objcopy
QEMU      = qemu-system-i386
KERNEL_SECTORS = 128            # tamano de kernel en sectores (1 = 512 bytes)
FS_SECTORS     = 512            # fs.bin rellenado a 512 sectores (boot.asm usa el mismo valor)

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
            $(BUILD)/pe.o \
            $(BUILD)/win32.o \
            $(BUILD)/shell.o \
            $(BUILD)/fs/mefs.o \
            $(BUILD)/drivers/vga.o \
            $(BUILD)/drivers/serial.o \
            $(BUILD)/drivers/timer.o \
            $(BUILD)/drivers/keyboard.o \
            $(BUILD)/drivers/ata.o \
            $(BUILD)/drivers/vbe.o \
            $(BUILD)/drivers/vgafx.o \
            $(BUILD)/drivers/mouse.o \
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
# Soporte Windows (.exe): cada .elf se convierte ademas a PE32 con
# tools/makepe.py -> .exe (misma image base, mismo entry). El shell
# "run" detecta el formato por la magia (MZ vs ELF).
# Capa Win32 (Fase 8): modulos ring 3 fijos kernel32/user32/ntdll en
# la region WIN32_REGION_BASE (0xB0000000, tools/dll32.ld) y el
# programa de prueba con imports (user/winapi.c).
USER_TEXT = 0x80000000
USER_SRCS = user/hello.c user/fork.c user/exec.c user/console.c user/winapi.c user/quick.c
USER_ELFS = $(patsubst user/%.c,$(BUILD)/user/%.elf,$(USER_SRCS))
USER_EXES = $(patsubst user/%.c,$(BUILD)/user/%.exe,$(USER_SRCS))

# Conjunto minimo que se incluye en fs.bin (Fase 12): solo lo que usa la
# escalera. Los ELF nativos restantes se siguen compilando con
# compat_suite pero no ocupan sectores del FS (que mide 512 sectores).
FS_USER_ELFS = $(BUILD)/user/hello.elf
FS_USER_EXES = $(filter %quick.exe %winapi.exe %fork.exe %exec.exe %console.exe,\
                       $(USER_EXES))

WIN32_REGION = 0xB0000000
DLL_SRCS  = user/win32/kernel32.c user/win32/user32.c user/win32/ntdll.c \
            user/win32/msvcrt.c
DLL_ELFS  = $(patsubst user/win32/%.c,$(BUILD)/user/win32/%.elf,$(DLL_SRCS))

# Fase 9/11/12: .exe compilado con la toolchain REAL de Windows (CRT de
# mingw-w64). Se usan como pruebas incluidas en fs.bin/ISO.
MINGW32   = i686-w64-mingw32-gcc
WIN_APPS  = $(BUILD)/user/win32/hello_win.exe $(BUILD)/user/win32/dir.exe \
            $(BUILD)/user/win32/proc.exe
# messagebox.exe ademas importa USER32.dll (MessageBoxA), Fase 12.
WIN_APPS  += $(BUILD)/user/win32/messagebox.exe

$(BUILD)/user/win32/%.exe: user/win32/%.c
	@mkdir -p $(dir $@)
	$(MINGW32) -m32 -O1 -Wl,--image-base,0x80000000 \
	           -Wl,--subsystem,console -s -o $@ $<

$(BUILD)/user/win32/messagebox.exe: user/win32/messagebox.c
	@mkdir -p $(dir $@)
	$(MINGW32) -m32 -O1 -Wl,--image-base,0x80000000 \
	           -Wl,--subsystem,console -s -o $@ $< -luser32

win_hello: $(WIN_APPS)
	@echo "OK: $(WIN_APPS) (imports reales de Windows)"
	@i686-w64-mingw32-objdump -p $(BUILD)/user/win32/dir.exe | sed -n '/The Import Tables/,/^$$/p'

compat_suite: $(USER_ELFS) $(USER_EXES) $(WIN_APPS)
	@echo "OK: suite base de compatibilidad Win32 construida"
	@echo "- Fase 1/2: hello.elf"
	@echo "- Fase 3/6: quick.exe, console.exe, exec.exe, fork.exe"
	@echo "- Fase 8: winapi.exe"
	@echo "- Fase 9: hello_win.exe"
	@echo "- Fase 10: dir.exe"
	@echo "- Fase 11: proc.exe"
	@echo "- Fase 12: messagebox.exe (GUI user32)"

$(BUILD)/user/%.elf: user/%.c tools/user.ld
	@mkdir -p $(dir $@)
	$(CC) -m32 -ffreestanding -fno-pic -fno-stack-protector -fno-builtin \
	      -fno-asynchronous-unwind-tables -Wall -Wextra -O2 -c $< -o $(BUILD)/user/$*.o
	$(LD) -m elf_i386 -nostdlib -T tools/user.ld -o $@ $(BUILD)/user/$*.o

$(BUILD)/user/win32/%.elf: user/win32/%.c tools/dll32.ld
	@mkdir -p $(dir $@)
	$(CC) -m32 -ffreestanding -fno-pic -fno-stack-protector -fno-builtin \
	      -fno-asynchronous-unwind-tables -Wall -Wextra -O2 -c $< -o $(BUILD)/user/win32/$*.o
	$(LD) -m elf_i386 -nostdlib -T tools/dll32.ld \
	      -defsym=DLL_BASE=$$(case $* in \
	        kernel32) echo $(WIN32_REGION) ;; \
	        user32)   echo $$(( $(WIN32_REGION) + 0x100000 )) ;; \
	        ntdll)    echo $$(( $(WIN32_REGION) + 0x200000 )) ;; \
	        msvcrt)   echo $$(( $(WIN32_REGION) + 0x300000 )) ;; \
	        *) echo $(WIN32_REGION) ;; esac) \
	      -o $@ $(BUILD)/user/win32/$*.o

$(BUILD)/user/%.exe: $(BUILD)/user/%.elf tools/makepe.py
	python3 tools/makepe.py $< -o $@

# Filesystem MEFS: superbloque + directorio + datos (solo lectura).
# Se rellena a FS_SECTORS para que boot.asm pueda copiarlo entero a RAM
# en el arranque por CD (la imagen os-image.bin lo lleva en LBA 129..).
$(BUILD)/fs.bin: $(FS_USER_ELFS) $(FS_USER_EXES) $(DLL_ELFS) $(WIN_APPS) \
                 user/win32/readme.txt
	@mkdir -p $(dir $@)
	python3 tools/makefs.py $(FS_USER_ELFS) $(FS_USER_EXES) $(DLL_ELFS) $(WIN_APPS) \
	                        user/win32/readme.txt -o $@
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
