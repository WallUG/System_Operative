; Kernel stub 32 bits - Fase 1 (provisional).
; Punto de entrada llamado por el bootloader (org 0x10000, modo protegido).
; Evidencia de vida: pinta VGA 0xB8000 y envia lo mismo por COM1 (serial).
; En la Fase 2 este archivo pasara a ser el trampolin que llama a kmain() en C.

[org 0x10000]
[bits 32]

KERNEL_ORIGIN equ 0x10000

start:
    call serial_init
    call screen_clear

    mov esi, msg_title
    mov edi, 0                  ; fila 0, col 0 (offset en bytes)
    call vga_print

    mov esi, msg_pm_ok
    mov edi, 160                ; fila 1
    call vga_print

    mov esi, msg_mem
    mov edi, 320                ; fila 2
    call vga_print

    mov esi, msg_title
    call serial_print
    mov esi, msg_pm_ok
    call serial_print
    mov esi, msg_mem
    call serial_print

    cli
.hang:
    hlt
    jmp .hang

; ------------------------------------------------------------------
; VGA texto: [edi] = offset dentro de 0xB8000, color 0x0A (verde claro)
; ------------------------------------------------------------------
vga_print:
    mov edx, 0xB8000
    add edx, edi
.loop:
    lodsb
    or al, al
    jz .done
    mov byte [edx], al
    mov byte [edx + 1], 0x0A
    add edx, 2
    jmp .loop
.done:
    ret

screen_clear:
    mov edx, 0xB8000
    mov ecx, 2000               ; 80 x 25 celdas
    mov eax, 0x0F20             ; espacio con atributo blanco sobre negro
.loop:
    mov dword [edx], eax
    add edx, 4
    dec ecx
    jnz .loop
    ret

; ------------------------------------------------------------------
; UART COM1 (0x3F8) inicializado a 115200 8N1. Sin init la BIOS suele
; dejar 9600 8N1; la inicializacion explicita es mas fiable.
; ------------------------------------------------------------------
serial_init:
    mov dx, 0x3F9
    xor al, al
    out dx, al                  ; sin interrupciones
    mov dx, 0x3FB
    mov al, 0x80
    out dx, al                  ; DLAB = 1
    mov dx, 0x3F8
    mov al, 1
    out dx, al                  ; divisor low = 1 -> 115200
    mov dx, 0x3F9
    xor al, al
    out dx, al                  ; divisor high = 0
    mov dx, 0x3FB
    mov al, 0x03
    out dx, al                  ; 8N1, DLAB = 0
    mov dx, 0x3FA
    mov al, 0xC7
    out dx, al                  ; FIFO 14 bytes
    mov dx, 0x3FC
    mov al, 0x0B
    out dx, al                  ; DTR + RTS + OUT2
    ret

serial_print:
    mov dx, 0x3F8
.loop:
    lodsb
    or al, al
    jz .done
    push dx
.busy:
    add dx, 5                   ; puerto LSR
    in al, dx
    test al, 0x20               ; THR vacio?
    jz .busy
    pop dx
    mov al, [esi - 1]
    out dx, al                  ; escribir byte
    jmp .loop
.done:
    ret

msg_title db "MyOS v0.0.1 - modo protegido activo (32-bit)", 13, 10, 0
msg_pm_ok  db "Bootloader OK: kernel en 0x10000, GDT cargada, A20 habilitada", 13, 10, 0
msg_mem    db "VGA 0xB8000 + COM1 115200 listos. Siguiente fase: kmain en C", 13, 10, 0
