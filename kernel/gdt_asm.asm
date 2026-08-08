; MyOS - kernel/gdt_asm.asm
; Carga de la GDT del kernel y del registro TR (TSS).

[bits 32]

section .text

global gdt_load
gdt_load:
    mov eax, [esp + 4]
    lgdt [eax]
    ; Recargar los segmentos de datos y el CS con la GDT nueva.
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    jmp 0x08:.flush
.flush:
    ret

global tss_load
tss_load:
    mov ax, [esp + 4]
    ltr ax
    ret
