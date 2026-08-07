; Entry point del kernel - Fase 2
; Trampolin que llama a kmain() en C. El bootloader salta aqui en modo
; protegido 32 bits; el linker script ubica .text en 0x10000.
; La pila vive en .bss (16 KB alineada a 16, cdecl i386 exige ESP%16==12
; en la llamada, el ABI de GCC garantiza la alineacion correcta del tope).

[bits 32]
extern kmain
global _start

section .text
_start:
    mov esp, stack_top
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
