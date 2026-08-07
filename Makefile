# Makefile MyOS - Fase 1 (i386, bootloader + kernel stub ASM)
# El kernel en C llegara en la Fase 2 (gcc -m32 freestanding).

ASM       = nasm
QEMU      = qemu-system-i386
KERNEL_SECTORS = 64              # tamano de kernel en sectores (1 = 512 bytes)

BUILD     = build

all: os-image.bin

$(BUILD)/boot.bin: boot/boot.asm
	mkdir -p $(BUILD)
	$(ASM) -f bin $< -o $@
	@test $$(wc -c < $@) -eq 512 || (echo "ERROR: boot.bin no mide 512 bytes"; exit 1)

$(BUILD)/kernel.bin: kernel/entry.asm
	mkdir -p $(BUILD)
	$(ASM) -f bin $< -o $@
	@truncate -s $$(( $(KERNEL_SECTORS) * 512 )) $@   # pad a 64 sectores

os-image.bin: $(BUILD)/boot.bin $(BUILD)/kernel.bin
	cat $^ > $@
	@echo "OK: os-image.bin = $$(wc -c < $@) bytes (512 + kernel)"

run: os-image.bin
	$(QEMU) -drive format=raw,file=$<

# Ejecucion headless: VGA oculto, salida por COM1 a stdout.
test: os-image.bin
	$(QEMU) -display none -monitor none -serial stdio -no-reboot \
	        -drive format=raw,file=$<

# Depuracion remota: pausa QEMU a la espera de GDB en :1234
debug: os-image.bin
	$(QEMU) -s -S -drive format=raw,file=$< &
	@echo "Conecta con: gdb -ex 'target remote localhost:1234'"
	@sleep 1

clean:
	rm -rf $(BUILD) os-image.bin

.PHONY: all run test debug clean
