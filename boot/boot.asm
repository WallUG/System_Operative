; Bootloader MyOS - Fase 1
; 512 bytes, modo real 16 bits, cargado por la BIOS en 0x7C00.
; Flujo: mensaje BIOS -> carga del kernel por LBA (int 0x13/0x42)
;        -> A20 (8042 + verificacion) -> GDT -> modo protegido -> kernel en 0x10000.
;
; Mapa de memoria relevante (ver DESIGN.md):
;   0x7C00  bootloader   |  0x10000 kernel |  0x90000 pila PM
;   VGA texto en 0xB8000 (solo desde modo protegido)

[org 0x7C00]
[bits 16]

KERNEL_LOAD_SEG  equ 0x1000        ; segmento:0x1000 -> fisica 0x10000
KERNEL_OFFSET    equ 0x0000
KERNEL_SECTORS   equ 64            ; 64 sectores = 32 KB (kernel pad a ese tamano)
CODE_SEG         equ gdt_code - gdt_start
DATA_SEG         equ gdt_data - gdt_start
MMAP_ADDR        equ 0x7E00        ; buffer E820: dword contador + entradas de 20 B
MMAP_ENTRIES     equ 32

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00              ; pila justo debajo del bootloader
    mov [BOOT_DRIVE], dl        ; BIOS pasa la unidad de arranque en dl: guardar YA
    sti

    mov si, msg_boot
    call print_string

    call load_kernel            ; lee KERNEL_SECTORS sectores a 0x10000
    mov si, msg_kernel_ok
    call print_string

    call get_mmap               ; E820: mapa de memoria de la BIOS -> 0x7E00

    call enable_a20             ; metodo 8042 (portable); QEMU lo soporta
    call check_a20
    cmp al, 1
    je .a20_ok
    mov si, msg_a20_fail
    call print_string
    jmp halt
.a20_ok:
    mov si, msg_a20_ok
    call print_string

    cli
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 1
    mov cr0, eax                ; PE=1 -> modo protegido
    jmp CODE_SEG:init_pm        ; far jump: vacia pipeline y carga CS

halt:
    hlt
    jmp halt

; ------------------------------------------------------------------
; Carga el kernel usando int 0x13 extensiones LBA (funcion 0x42).
; DS debe ser 0; el DAP es una estructura de 16 bytes (ver abajo).
; ------------------------------------------------------------------
load_kernel:
    mov si, dap
    mov ah, 0x42
    mov dl, [BOOT_DRIVE]
    int 0x13
    jc disk_error
    test ah, ah
    jnz disk_error
    ret

disk_error:
    mov si, msg_disk_err
    call print_string
    jmp halt

; ------------------------------------------------------------------
; Recoge el mapa de memoria E820 de la BIOS (int 0x15, EAX=0xE820).
; Buffer: dword contador en 0x7E00 + hasta MMAP_ENTRIES entradas de
; 20 bytes (base_low, base_high, len_low, len_high, type) en 0x7E04.
; Lo parsea el kernel en Fase 4 (kernel/mem/mmap.c).
; ------------------------------------------------------------------
get_mmap:
    mov di, MMAP_ADDR + 4
    xor ebx, ebx                ; continuacion de iteracion (empezar en 0)
    xor bp, bp                  ; contador de entradas
    mov es, bx                  ; es = 0 (bx ya es 0)
    mov eax, 0xE820
    mov edx, 0x534D4150         ; 'SMAP'
    mov ecx, 20
.next_entry:
    int 0x15
    jc .done                    ; error: usar lo recogido hasta ahora
    cmp eax, 0x534D4150         ; BIOS debe responder con 'SMAP' en eax
    jne .fail
    inc bp
    add di, 20                  ; siguiente entrada
    test ebx, ebx               ; ebx=0 -> no hay mas entradas
    jz .done
    cmp bp, MMAP_ENTRIES        ; limite del buffer
    jge .done
    mov eax, 0xE820
    mov edx, 0x534D4150
    mov ecx, 20
    jmp .next_entry
.done:
    mov [MMAP_ADDR], bp         ; ds=0 -> contador en 0x7E00
    ret
.fail:
    xor bp, bp
    jmp .done

; ------------------------------------------------------------------
; A20 via controlador de teclado (8042): leer output port, poner bit 1,
; escribir de vuelta. No es infalible en todo hardware; la verificacion
; de check_a20 detecta fallo y se puede anadir fallback 0x92 despues.
; ------------------------------------------------------------------
enable_a20:
    call a20_wait_input
    mov al, 0xAD
    out 0x64, al                ; deshabilitar teclado
    call a20_wait_input
    mov al, 0xD0
    out 0x64, al                ; comando: leer output port
    call a20_wait_output
    in al, 0x60
    push ax
    call a20_wait_input
    mov al, 0xD1
    out 0x64, al                ; comando: escribir output port
    call a20_wait_input
    pop ax
    or al, 0x02                 ; bit A20
    out 0x60, al
    call a20_wait_input
    mov al, 0xAE
    out 0x64, al                ; re-habilitar teclado
    ret

a20_wait_input:                 ; espera buffer de comando libre (bit 2 = 0)
    in al, 0x64
    test al, 0x02
    jnz a20_wait_input
    ret

a20_wait_output:                ; espera dato disponible (bit 1 = 1)
    in al, 0x64
    test al, 0x01
    jz a20_wait_output
    ret

; ------------------------------------------------------------------
; Verifica A20: escribe 0xFF en 0x100500 y lee 0x0500. Sin A20 ambas
; direcciones se solapan (wrap-around) y se lee 0xFF.
; Retorna al=1 si A20 activa, al=0 si no.
; ------------------------------------------------------------------
check_a20:
    pushf
    push si
    push di
    push ds
    push es
    cli
    xor ax, ax
    mov es, ax                  ; es:di = 0x0000:0x0500
    mov di, 0x0500
    not ax
    mov ds, ax                  ; ds:si = 0xFFFF:0x0510 = 0x100500
    mov si, 0x0510
    mov byte [es:di], 0x00
    mov byte [ds:si], 0xFF
    cmp byte [es:di], 0xFF      ; si se lee 0xFF -> hubo wrap -> A20 OFF
    mov al, 0
    je .done
    mov al, 1
.done:
    pop es
    pop ds
    pop di
    pop si
    popf
    ret

print_string:
    lodsb
    or al, al
    jz .done
    mov ah, 0x0E                ; teletype BIOS
    mov bh, 0
    int 0x10
    jmp print_string
.done:
    ret

; ------------------------------------------------------------------
; Datos: strings, DAP de lectura LBA, GDT y descriptor.
; ------------------------------------------------------------------
BOOT_DRIVE db 0

msg_boot       db "MyOS: BIOS OK, LBA load...", 13, 10, 0
msg_kernel_ok  db "Kernel en 0x10000.", 13, 10, 0
msg_a20_ok     db "A20 OK.", 13, 10, 0
msg_a20_fail   db "ERROR: A20 fallo.", 13, 10, 0
msg_disk_err   db "ERROR: fallo de disco (int 0x13).", 13, 10, 0

dap:                            ; Disk Address Packet (int 0x13, ah=0x42)
    db 0x10                     ; tamano del paquete (16 bytes)
    db 0x00                     ; reservado
    dw KERNEL_SECTORS           ; sectores a leer
    dw KERNEL_OFFSET            ; offset destino
    dw KERNEL_LOAD_SEG          ; segmento destino
    dq 1                        ; LBA inicial (0 = boot, 1 = kernel)

; GDT: descriptor nulo + code + data, ambos flat (base 0, limite 4 GiB,
; granulosidad 4K). Un solo par code/data alcanza para este kernel.
gdt_start:
    dq 0x0000000000000000       ; descriptor nulo (obligatorio)
gdt_code:
    dw 0xFFFF, 0x0000           ; limite 15:0, base 15:0
    db 0x00                     ; base 23:16
    db 10011010b                ; present, ring0, code, non-conforming, readable
    db 11001111b                ; granularidad 4K, 32 bits, limite 19:16 = 0xF
    db 0x00                     ; base 31:24
gdt_data:
    dw 0xFFFF, 0x0000
    db 0x00
    db 10010010b                ; present, ring0, data, writable
    db 11001111b
    db 0x00
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1  ; limite de la tabla (en bytes - 1)
    dd gdt_start                ; base (ds=0, direccion fisica)

; ------------------------------------------------------------------
; Modo protegido: segmentos planos, pila nueva en 0x90000 y salto al
; kernel en 0x10000 (32 bits).
; ------------------------------------------------------------------
[bits 32]
init_pm:
    mov ax, DATA_SEG
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000            ; pila del kernel, crece hacia abajo
    mov ebp, esp
    mov eax, 0x10000            ; entry point del kernel
    call eax
    jmp $                       ; si el kernel retorna, colgar aqui

times 510 - ($ - $$) db 0
dw 0xAA55
