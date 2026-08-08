; MyOS - kernel/isr.asm
; Stubs de interrupciones: la CPU invoca la etiqueta global (gate de la
; IDT), el stub preserva el estado completo y despacha al handler en C.
;
; Las excepciones con error code (8, 10-14, 17, 21) dejan ese codigo en la
; pila; el resto no. Las macros unifican el layout de registers_t:
;   [error code dummy] [int_no] -> pusha -> (eip, cs, eflags) de la CPU.

[bits 32]

section .text

extern isr_handler
extern irq_handler

%macro ISR_NOERR 1
global isr%1
isr%1:
    push dword 0        ; dummy err_code
    push dword %1       ; int_no
    jmp isr_common_stub
%endmacro

%macro ISR_ERR 1
global isr%1
isr%1:
    push dword %1       ; la CPU ya dejo el error code en la pila
    jmp isr_common_stub
%endmacro

%macro IRQ 2
global irq%1
irq%1:
    push dword 0        ; dummy err_code
    push dword %2       ; vector (32 + n)
    jmp irq_common_stub
%endmacro

isr_common_stub:
    pusha
    push es
    push ds                 ; ds al offset mas bajo: coincide con registers_t
    mov ax, 0x10            ; DATA_SEG plano de la GDT
    mov ds, ax
    mov es, ax
    mov eax, esp            ; eax = puntero al frame (registers_t)
    push eax                ; argumento cdecl para el handler en C
    call isr_handler
    add esp, 4              ; descartar el argumento
    pop ds
    pop es
    popa
    add esp, 8              ; descarta int_no + err_code
    iret

irq_common_stub:
    pusha
    push es
    push ds
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov eax, esp
    push eax
    call irq_handler
    add esp, 4
    pop ds
    pop es
    popa
    add esp, 8
    iret

; lidt (cdecl): idt_load(void* ptr) -> lidt [ptr]
global idt_load
idt_load:
    mov eax, [esp + 4]
    lidt [eax]
    ret

; --- Excepciones de CPU (Intel) ---
ISR_NOERR 0    ; division by zero
ISR_NOERR 1    ; debug
ISR_NOERR 2    ; NMI
ISR_NOERR 3    ; breakpoint
ISR_NOERR 4    ; overflow
ISR_NOERR 5    ; BOUND range exceeded
ISR_NOERR 6    ; invalid opcode
ISR_NOERR 7    ; device not available
ISR_ERR   8    ; double fault
ISR_NOERR 9    ; coprocessor segment overrun
ISR_ERR   10   ; invalid TSS
ISR_ERR   11   ; segment not present
ISR_ERR   12   ; stack-segment fault
ISR_ERR   13   ; general protection fault
ISR_ERR   14   ; page fault
ISR_NOERR 15   ; reserved
ISR_NOERR 16   ; x87 FP error
ISR_ERR   17   ; alignment check
ISR_NOERR 18   ; machine check
ISR_NOERR 19   ; SIMD FP exception
ISR_NOERR 20   ; virtualization exception
ISR_ERR   21   ; control protection
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29
ISR_NOERR 30
ISR_NOERR 31

; --- Syscall: int 0x80 (Fase 6) ---
ISR_NOERR 128

; --- IRQs del PIC remapeadas a 32-47 ---
IRQ 0, 32
IRQ 1, 33
IRQ 2, 34
IRQ 3, 35
IRQ 4, 36
IRQ 5, 37
IRQ 6, 38
IRQ 7, 39
IRQ 8, 40
IRQ 9, 41
IRQ 10, 42
IRQ 11, 43
IRQ 12, 44
IRQ 13, 45
IRQ 14, 46
IRQ 15, 47
