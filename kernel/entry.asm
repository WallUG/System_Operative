; Entry point del kernel - Fase 2
; Trampolin que llama a kmain() en C. El bootloader salta aqui en modo
; protegido 32 bits; el linker script ubica .text en 0x10000.
; La pila vive en .bss (16 KB alineada a 16, cdecl i386 exige ESP%16==12
; en la llamada, el ABI de GCC garantiza la alineacion correcta del tope).
; Fase 7: el bootloader empuja el puntero a boot_info (0x7000) antes del
; call; se preserva ANTES de fijar la pila del kernel (si no, se pierde).

[bits 32]
extern kmain
extern __bss_start
extern __bss_end
global _start

section .text
_start:
    mov edx, [esp]              ; arg cdecl: puntero a boot_info (0x7000)
    mov esp, stack_top
    ; Fase 17: zeroear .bss. El linker lo coloca hasta 0x2a974, mas alla de
    ; los 64 KB de kernel.bin que el bootloader copia a 0x10000-0x20000; en
    ; modo CD la BIOS carga ahi los bytes de fs.bin (basura) y sin este
    ; borrado las variables globales de .bss quedan corruptas. En modo HD
    ; solo funcionaba porque QEMU inicializa la RAM a cero.
    cld
    mov edi, __bss_start
    mov ecx, __bss_end
    sub ecx, edi
    xor eax, eax
    rep stosb
    push edx
    call kmain
.hang:
    cli
    hlt
    jmp .hang

section .bss
align 16
stack_bottom:
    resb 16384                  ; 16 KB de pila del kernel
stack_top:
