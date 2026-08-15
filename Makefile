# Makefile MyOS - Fase 2 (i386, kernel en C freestanding)
# Toolchain: gcc/ld del host en modo freestanding -m32 (ver DESIGN.md).

ASM       = nasm
CC        = gcc
LD        = ld
OBJCOPY   = objcopy
QEMU      = qemu-system-i386
KERNEL_SECTORS = 128            # tamano de kernel en sectores (1 = 512 bytes)
FS_SECTORS     = 2000           # fs.bin rellenado a 2000 sectores (1 MB; apps + margen Fase E para escrituras; boot.asm usa el mismo valor)

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
            $(BUILD)/mem/uheap.o \
            $(BUILD)/task/task.o \
            $(BUILD)/task/switch.o \
            $(BUILD)/gdt.o \
            $(BUILD)/gdt_asm.o \
            $(BUILD)/syscall.o \
            $(BUILD)/winmgr.o \
            $(BUILD)/elf.o \
            $(BUILD)/pe.o \
            $(BUILD)/win32.o \
            $(BUILD)/bootscreen.o \
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
USER_SRCS = user/hello.c user/fork.c user/exec.c user/console.c user/winapi.c user/quick.c user/desktop.c user/explorer.c user/inject.c user/installer.c user/b6inj.c
USER_ELFS = $(patsubst user/%.c,$(BUILD)/user/%.elf,$(USER_SRCS))
USER_EXES = $(patsubst user/%.c,$(BUILD)/user/%.exe,$(USER_SRCS))

# Conjunto minimo que se incluye en fs.bin (Fase 12): solo lo que usa la
# escalera. Los ELF nativos restantes se siguen compilando con
# compat_suite pero no ocupan sectores del FS (que mide 512 sectores).
FS_USER_ELFS = $(BUILD)/user/hello.elf $(BUILD)/user/mouseinfo.elf \
               $(BUILD)/user/win_demo.elf $(BUILD)/user/win_two.elf \
               $(BUILD)/user/desktop.elf $(BUILD)/user/explorer.elf \
               $(BUILD)/user/inject.elf \
               $(BUILD)/user/installer.elf \
               $(BUILD)/user/b6inj.elf
FS_USER_EXES = $(filter %quick.exe %winapi.exe %fork.exe %exec.exe %console.exe,\
                       $(USER_EXES))

WIN32_REGION = 0xB0000000
DLL_SRCS  = user/win32/kernel32.c user/win32/user32.c user/win32/ntdll.c \
            user/win32/msvcrt.c user/win32/gdi32.c user/win32/comctl32.c \
            user/win32/comdlg32.c user/win32/advapi32.c user/win32/shell32.c \
            user/win32/ole32.c user/win32/shlwapi.c user/win32/winspool.c
DLL_ELFS  = $(patsubst user/win32/%.c,$(BUILD)/user/win32/%.elf,$(DLL_SRCS))

# Fase 9/11/12: .exe compilado con la toolchain REAL de Windows (CRT de
# mingw-w64). Se usan como pruebas incluidas en fs.bin/ISO.
MINGW32   = i686-w64-mingw32-gcc
WIN_APPS  = $(BUILD)/user/win32/hello_win.exe $(BUILD)/user/win32/dir.exe \
            $(BUILD)/user/win32/proc.exe
# messagebox.exe ademas importa USER32.dll (MessageBoxA), Fase 12.
WIN_APPS  += $(BUILD)/user/win32/messagebox.exe
# gdi_demo.exe importa USER32+GDI32 (dibujo), Fase 18 / Hito B slice 2.
# El binario se llama gdidemo.exe (sin '_': el driver PS/2 de MyOS no
# mapea shift+'-' a '_').
WIN_APPS  += $(BUILD)/user/win32/gdidemo.exe
# writetest.exe: escritura Win32 (CreateFileA WRITE + WriteFile), Fase E.
WIN_APPS  += $(BUILD)/user/win32/writetest.exe
WIN_APPS  += $(BUILD)/user/win32/dlgtest.exe
WIN_APPS  += $(BUILD)/user/win32/wintwo.exe
WIN_APPS  += $(BUILD)/user/win32/movetest.exe
# dlltest.exe: OLE32/SHLWAPI/WINSPOOL (Fase 23-B8).
WIN_APPS  += $(BUILD)/user/win32/dlltest.exe
# txtmode.exe: modos binario/texto _O_BINARY/_O_TEXT (Fase 23-C9).
WIN_APPS  += $(BUILD)/user/win32/txtmode.exe
# heaptest.exe: heap por proceso _O_BINARY (Fase 23-C10).
WIN_APPS  += $(BUILD)/user/win32/heaptest.exe
# listview.exe: SysListView32 de comctl32 (Fase 23-C11).
WIN_APPS  += $(BUILD)/user/win32/listview.exe

$(BUILD)/user/win32/listview.exe: user/win32/listview.c
	@mkdir -p $(dir $@)
	$(MINGW32) -m32 -O1 -Wl,--image-base,0x80000000 \
	           -Wl,--subsystem,console -s -o $@ $< -luser32 -lcomctl32

# ctldemo.exe: toolbar/statusbar/trackbar/treeview (Fase 24-P2.1).
WIN_APPS  += $(BUILD)/user/win32/ctldemo.exe

$(BUILD)/user/win32/ctldemo.exe: user/win32/ctldemo.c
	@mkdir -p $(dir $@)
	$(MINGW32) -m32 -O1 -Wl,--image-base,0x80000000 \
	           -Wl,--subsystem,console -s -o $@ $< -luser32 -lcomctl32

# iconres.exe: recursos PE .rsrc (Fase 24-P1.1): LoadIconA + icono.
WIN_APPS  += $(BUILD)/user/win32/iconres.exe

$(BUILD)/user/win32/iconres.exe: user/win32/iconres.c user/win32/iconres.rc \
                                 user/win32/iconres.ico
	@mkdir -p $(dir $@)
	$(MINGW32)-windres user/win32/iconres.rc -O coff -o build/iconres.res.o || \
	i686-w64-mingw32-windres user/win32/iconres.rc -O coff -o build/iconres.res.o
	$(MINGW32) -m32 -O1 -Wl,--image-base,0x80000000 \
	           -Wl,--subsystem,console -s -o $@ $< build/iconres.res.o -luser32

# regtest.exe: registro persistente advapi32 (Fase 24-P1.3).
WIN_APPS  += $(BUILD)/user/win32/regtest.exe

$(BUILD)/user/win32/regtest.exe: user/win32/regtest.c
	@mkdir -p $(dir $@)
	$(MINGW32) -m32 -O1 -Wl,--image-base,0x80000000 \
	           -Wl,--subsystem,console -s -o $@ $< -luser32 -ladvapi32

# bootpaths.exe: kernel32 de arranque (Fase 24-P1.4).
WIN_APPS  += $(BUILD)/user/win32/bootpaths.exe

$(BUILD)/user/win32/bootpaths.exe: user/win32/bootpaths.c
	@mkdir -p $(dir $@)
	$(MINGW32) -m32 -O1 -Wl,--image-base,0x80000000 \
	           -Wl,--subsystem,console -s -o $@ $< -luser32 -lkernel32

# thrtest.exe: CreateThread preemptivo (Fase 24-P2.2).
WIN_APPS  += $(BUILD)/user/win32/thrtest.exe

$(BUILD)/user/win32/thrtest.exe: user/win32/thrtest.c
	@mkdir -p $(dir $@)
	$(MINGW32) -m32 -O1 -Wl,--image-base,0x80000000 \
	           -Wl,--subsystem,console -s -o $@ $< -luser32 -lkernel32

$(BUILD)/user/win32/heaptest.exe: user/win32/heaptest.c
	@mkdir -p $(dir $@)
	$(MINGW32) -m32 -O1 -Wl,--image-base,0x80000000 \
	           -Wl,--subsystem,console -s -o $@ $< -luser32 -lkernel32

$(BUILD)/user/win32/txtmode.exe: user/win32/txtmode.c
	@mkdir -p $(dir $@)
	$(MINGW32) -m32 -O1 -Wl,--image-base,0x80000000 \
	           -Wl,--subsystem,console -s -o $@ $< -luser32 -lkernel32

$(BUILD)/user/win32/dlltest.exe: user/win32/dlltest.c
	@mkdir -p $(dir $@)
	$(MINGW32) -m32 -O1 -Wl,--image-base,0x80000000 \
	           -Wl,--subsystem,console -s -o $@ $< -luser32 -lshlwapi -lole32 -lwinspool

$(BUILD)/user/win32/%.exe: user/win32/%.c
	@mkdir -p $(dir $@)
	$(MINGW32) -m32 -O1 -Wl,--image-base,0x80000000 \
	           -Wl,--subsystem,console -s -o $@ $<

$(BUILD)/user/win32/messagebox.exe: user/win32/messagebox.c
	@mkdir -p $(dir $@)
	$(MINGW32) -m32 -O1 -Wl,--image-base,0x80000000 \
	           -Wl,--subsystem,console -s -o $@ $< -luser32

# movetest.exe: MoveWindow/GetWindowRect/WM_MOVE/WM_SIZE (Fase 23-B7).
$(BUILD)/user/win32/movetest.exe: user/win32/movetest.c
	@mkdir -p $(dir $@)
	$(MINGW32) -m32 -O1 -Wl,--image-base,0x80000000 \
	           -Wl,--subsystem,console -s -o $@ $< -luser32

# wintwo.exe: message loop real con 2 ventanas top-level (Fase 23-B6).
$(BUILD)/user/win32/wintwo.exe: user/win32/wintwo.c
	@mkdir -p $(dir $@)
	$(MINGW32) -m32 -O1 -Wl,--image-base,0x80000000 \
	           -Wl,--subsystem,console -s -o $@ $< -luser32

# dlgtest.exe: DialogBoxParamA/EndDialog con recurso RT_DIALOG (Fase 23-B5).
$(BUILD)/user/win32/dlgtest.exe: user/win32/dlgtest.c user/win32/dlgtest.rc
	@mkdir -p $(dir $@)
	$(MINGW32)-windres user/win32/dlgtest.rc -O coff -o build/dlgtest.res.o || \
	i686-w64-mingw32-windres user/win32/dlgtest.rc -O coff -o build/dlgtest.res.o
	$(MINGW32) -m32 -O1 -Wl,--image-base,0x80000000 \
	           -Wl,--subsystem,console -s -o $@ $< build/dlgtest.res.o -luser32

# dlgtest2.exe: dialogos con EDIT control (Fase 24-P1.2).
WIN_APPS  += $(BUILD)/user/win32/dlgtest2.exe

$(BUILD)/user/win32/dlgtest2.exe: user/win32/dlgtest2.c user/win32/dlgtest2.rc
	@mkdir -p $(dir $@)
	$(MINGW32)-windres user/win32/dlgtest2.rc -O coff -o build/dlgtest2.res.o || \
	i686-w64-mingw32-windres user/win32/dlgtest2.rc -O coff -o build/dlgtest2.res.o
	$(MINGW32) -m32 -O1 -Wl,--image-base,0x80000000 \
	           -Wl,--subsystem,console -s -o $@ $< build/dlgtest2.res.o -luser32

$(BUILD)/user/win32/gdidemo.exe: user/win32/gdi_demo.c
	@mkdir -p $(dir $@)
	$(MINGW32) -m32 -O1 -Wl,--image-base,0x80000000 \
	           -Wl,--subsystem,windows -s -o $@ $< -luser32 -lgdi32

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
	      -fno-asynchronous-unwind-tables -Wall -Wextra -O2 \
	      $(if $(filter msvcrt,$*),,-mrtd) -c $< -o $(BUILD)/user/win32/$*.o
	$(LD) -m elf_i386 -nostdlib -T tools/dll32.ld \
	      -q \
	      -defsym=DLL_BASE=$$(case $* in \
	        kernel32) echo $(WIN32_REGION) ;; \
	        user32)   echo $$(( $(WIN32_REGION) + 0x100000 )) ;; \
	        ntdll)    echo $$(( $(WIN32_REGION) + 0x200000 )) ;; \
	        msvcrt)   echo $$(( $(WIN32_REGION) + 0x300000 )) ;; \
	        gdi32)    echo $$(( $(WIN32_REGION) + 0x400000 )) ;; \
	        comctl32) echo $$(( $(WIN32_REGION) + 0x500000 )) ;; \
	        comdlg32) echo $$(( $(WIN32_REGION) + 0x600000 )) ;; \
	        advapi32) echo $$(( $(WIN32_REGION) + 0x700000 )) ;; \
	        shell32)  echo $$(( $(WIN32_REGION) + 0x800000 )) ;; \
	        ole32)    echo $$(( $(WIN32_REGION) + 0x900000 )) ;; \
	        shlwapi)  echo $$(( $(WIN32_REGION) + 0xA00000 )) ;; \
	        winspool) echo $$(( $(WIN32_REGION) + 0xB00000 )) ;; \
	        *) echo $(WIN32_REGION) ;; esac) \
	      -o $@ $(BUILD)/user/win32/$*.o

$(BUILD)/user/%.exe: $(BUILD)/user/%.elf tools/makepe.py
	python3 tools/makepe.py $< -o $@

# Filesystem MEFS: superbloque + directorio + datos (solo lectura).
# Se rellena a FS_SECTORS para que boot.asm pueda copiarlo entero a RAM
# en el arranque por CD (la imagen os-image.bin lo lleva en LBA 129..).
$(BUILD)/fs.bin: $(FS_USER_ELFS) $(FS_USER_EXES) $(DLL_ELFS) $(WIN_APPS) \
                 user/win32/readme.txt tests/metapad.exe
	@mkdir -p $(dir $@)
	python3 tools/makefs.py $(FS_USER_ELFS) $(FS_USER_EXES) $(DLL_ELFS) $(WIN_APPS) \
	                        user/win32/readme.txt tests/metapad.exe \
	                        -c $(FS_SECTORS) -o $@
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

# --- Fase E: persistencia en disco separado -------------------------------
# El disco os-image.bin solo tiene ~190 sectores libres tras los datos del
# FS. Para Guardar con margen se genera un disco de trabajo ampliado
# (os-image + relleno a PERSIST_SECTORS) sobre el que el FS (modo ATA)
# escribe y persiste. El disco vive en el cwd del proceso (QEMU no arranca
# desde /tmp). 'persist_disk' regenera el disco limpio desde os-image.
PERSIST_SECTORS = 16384            # 8 MiB: boot+kernel+FS + ~15k sectores libres
PERSIST_DISK    = build/os-persist.bin

persist_disk: os-image.bin
	@mkdir -p $(dir $(PERSIST_DISK))
	@cp os-image.bin $(PERSIST_DISK)
	@truncate -s $$(( $(PERSIST_SECTORS) * 512 )) $(PERSIST_DISK)
	@echo "OK: disco de persistencia $(PERSIST_DISK) = $(PERSIST_SECTORS) sectores (fs_capacity = FS_SECTORS, escrituras sobreviven al reinicio)"

run_persist: persist_disk
	$(QEMU) -drive format=raw,file=$(PERSIST_DISK)

# Headless (serial + monitor QMP por socket) para automatizar/persistir.
test_persist: persist_disk
	$(QEMU) -display none -monitor none -serial stdio -no-reboot \
	        -drive format=raw,file=$(PERSIST_DISK)

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

.PHONY: all run test debug clean persist_disk run_persist test_persist
