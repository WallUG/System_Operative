; Bootloader MyOS - Fase 1 + Fase 7 (arranque por CD)
; 512 bytes, modo real 16 bits, cargado por la BIOS en 0x7C00.
; Flujo: mensaje BIOS -> carga del kernel -> A20 (8042 + verificacion)
;        -> GDT -> modo protegido -> kernel en 0x10000.
;
; Dos origenes de arranque:
;  - Disco/floppy (dl < 0xE0): lee el kernel por LBA (int 0x13/0x42) del
;    propio disco. El FS sigue en el disco (lo lee mefs_init via ATA).
;  - CD (dl >= 0xE0, El Torito no-emulation): la BIOS carga Toda la
;    imagen os-image.bin (boot + kernel + fs.bin) en 0x7C00. El bootloader
;    copia el kernel a 0x10000 y la imagen MEFS a 0x50000 (RAM).
;
; En ambos modos escribe la estructura boot_info (ver kernel/bootinfo.h)
; en 0x7000 y se la pasa al kernel empujandola antes del call.
;
; Mapa de memoria relevante (ver DESIGN.md):
;   0x7000  boot_info | 0x7C00 bootloader | 0x7E00 E820 | 0x10000 kernel
;   0x50000 imagen MEFS (modo CD) | 0x90000 pila PM | VGA 0xB8000

[org 0x7C00]
[bits 16]

KERNEL_LOAD_SEG  equ 0x1000        ; segmento:0x1000 -> fisica 0x10000
KERNEL_OFFSET    equ 0x0000
KERNEL_SECTORS   equ 128           ; 128 sectores = 64 KB (kernel pad)
FS_SECTORS       equ 64            ; fs.bin rellenado a 64 sectores (Makefile)
CODE_SEG         equ gdt_code - gdt_start
DATA_SEG         equ gdt_data - gdt_start
MMAP_ADDR        equ 0x7E00        ; buffer E820: dword contador + entradas de 20 B
MMAP_ENTRIES     equ 32
BOOTINFO_ADDR    equ 0x7000        ; estructura boot_info (kernel/bootinfo.h)
BOOTINFO_MAGIC   equ 0x4D594F53    ; 'MYOS'
BOOTINFO_MODE_CD equ 1
MEFS_FS_LBA      equ 129           ; sector absoluto del FS (1 boot + 128 kernel)
FS_RAM_DEST      equ 0x50000       ; copia de la imagen MEFS en RAM (modo CD)
IMG_BOOT         equ 0x7C00        ; base de la imagen completa en RAM (CD)
FS_RAM_SRC       equ IMG_BOOT + 512 + KERNEL_SECTORS * 512   ; 0x17E00

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00              ; pila justo debajo del bootloader
    mov [BOOT_DRIVE], dl        ; BIOS pasa la unidad de arranque en dl: guardar YA
    ; Sin sti: IF=0 en todo el bootloader. Las llamadas int 0x10/0x13/0x15
    ; funcionan con IF=0, y las copias 32-bit (rep movsd del modo CD) son
    ; atomicas: una IRQ del timer (o un SMI) en mitad de la copia
    ; corromperia el estado guardado del rep y la IVT de bajo memoria.

    mov si, msg_boot
    call print_string

    cmp dl, 0xE0
    jae .cd_boot
    ; --- Modo disco: el kernel se lee del disco y el FS queda en el. ---
    call load_kernel            ; lee KERNEL_SECTORS sectores a 0x10000
    xor esi, esi                ; mode = BOOTINFO_MODE_DISK
    mov edx, MEFS_FS_LBA        ; fs_source = LBA base del FS en el disco
    xor ecx, ecx                ; fs_size = 0 (se lee por ATA)
    jmp .info
.cd_boot:
    ; --- Modo CD: la BIOS ya cargo la imagen completa en 0x7C00. ---
    call load_kernel_ram
    mov esi, BOOTINFO_MODE_CD   ; mode = CD
    mov edx, FS_RAM_DEST        ; fs_source = direccion fisica en RAM
    mov ecx, FS_SECTORS * 512   ; fs_size = bytes copiados
.info:
    call write_bootinfo
.loaded:
    call get_mmap               ; E820: mapa de memoria de la BIOS -> 0x7E00

    call enable_a20             ; metodo 8042 (portable); QEMU lo soporta
    call check_a20
    cmp al, 1
    je .a20_ok
    mov si, msg_a20_fail
    call print_string
    jmp halt
.a20_ok:

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
; Escribe la estructura boot_info en 0x7000 (ver kernel/bootinfo.h).
; esi = mode, edx = fs_source, ecx = fs_size.
; ------------------------------------------------------------------
write_bootinfo:
    mov di, BOOTINFO_ADDR
    mov dword [di], BOOTINFO_MAGIC
    mov dword [di + 4], esi
    mov dword [di + 8], edx
    mov dword [di + 12], ecx
    ret

; ------------------------------------------------------------------
; Modo CD (El Torito no-emulation): la BIOS cargo la imagen completa
; (boot + kernel + fs.bin) en 0x7C00. Mapa en RAM:
;   0x7C00 boot (512 B) | 0x7E00 kernel (KERNEL_SECTORS*512 = 64 KB)
;   0x17E00: imagen MEFS (FS_SECTORS*512 = 32 KB)
; Copias con rep movsd: el 66 fija operando (movsd + ECX) y el 67
; (db 0x67) ESI/EDI de 32 bits; sin el 67 se usarian SI/DI de 16
; bits (truncado). Orden: primero el FS, luego el kernel hacia ATRAS
; (std) porque las fuentes se solapan con los destinos.
; ------------------------------------------------------------------
load_kernel_ram:
    ; 1) imagen MEFS -> 0x50000 (FS_RAM_DEST), 0x8000 bytes
    mov esi, FS_RAM_SRC
    mov edi, FS_RAM_DEST
    mov ecx, FS_SECTORS * 512 / 4
    db 0x67
    rep movsd
    ; 2) kernel 0x7E00 -> 0x10000, hacia atras (0x10000..0x17E00 se
    ;    solapa con la fuente; destino > fuente)
    std
    mov ecx, KERNEL_SECTORS * 512 / 4
    mov esi, IMG_BOOT + 512 + KERNEL_SECTORS * 512 - 4  ; fin de la fuente
    mov edi, 0x10000 + KERNEL_SECTORS * 512 - 4         ; fin del destino
    db 0x67
    rep movsd
    cld
    ret

; ------------------------------------------------------------------
; Recoge el mapa de memoria E820 de la BIOS (int 0x15, EAX=0xE820).
; Buffer: dword contador en 0x7E00 + hasta MMAP_ENTRIES entradas de
; 20 bytes (base_low, base_high, len_low, len_high, type) en 0x7E04.
; Lo parsea el kernel en Fase 4 (kernel/mem/mmap.c).
; ------------------------------------------------------------------
get_mmap:
    mov di, MMAP_ADDR + 4
    xor bx, bx                  ; continuacion de iteracion (empezar en 0)
    xor bp, bp                  ; contador de entradas
    mov es, bx                  ; es = 0 (bx ya es 0)
    mov eax, 0xE820
    mov edx, 0x534D4150         ; 'SMAP'
    xor ecx, ecx
    mov cl, 20
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
    ret                         ; no se re-habilita el teclado: ya no se usa

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
; Retorna al=1 si A20 activa, al=0 si no. Solo preserva ds (los demas
; registros no se usan despues en modo real).
; ------------------------------------------------------------------
check_a20:
    push ds
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
    pop ds
    ret

print_string:
    push es
    push ax
    push bx
    mov bx, 0xB800
    mov es, bx
    xor bx, bx                  ; cursor VGA: fila 0, columna 0
.loop:
    lodsb
    or al, al
    jz .done
    cmp al, 13                  ; ignorar CR y LF (una sola linea)
    je .skip
    cmp al, 10
    je .skip
    mov ah, 0x07                ; atributo: gris claro sobre negro
    mov [es:bx], ax
    add bx, 2
.skip:
    jmp .loop
.done:
    pop bx
    pop ax
    pop es
    ret

; ------------------------------------------------------------------
; Datos: strings, DAP de lectura LBA, GDT y descriptor.
; ------------------------------------------------------------------
BOOT_DRIVE db 0

msg_boot       db "MyOS boot", 13, 10, 0
msg_a20_fail   db "A20 FAIL", 13, 10, 0
msg_disk_err   db "DISK FAIL", 13, 10, 0

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
    push 0x7000                 ; arg cdecl: puntero a boot_info
    mov eax, 0x10000            ; entry point del kernel
    jmp eax                     ; el kernel no retorna

times 510 - ($ - $$) db 0
dw 0xAA55
