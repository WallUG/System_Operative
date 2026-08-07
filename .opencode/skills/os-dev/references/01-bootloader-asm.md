# Fase 1 — Bootloader en Ensamblador

## Objetivo de la fase
Un sector de arranque de exactamente 512 bytes, terminado en la firma `0x55AA`, que la BIOS carga en `0x7C00` y ejecuta en modo real de 16 bits. Debe: imprimir un mensaje (prueba de vida), cargar el resto del kernel desde disco, habilitar la línea A20, cargar una GDT, y saltar a modo protegido de 32 bits (y opcionalmente long mode de 64 bits).

## 1. Sector de arranque mínimo (modo real, 16 bits)

```nasm
; boot.asm — NASM, sintaxis Intel
[org 0x7C00]
[bits 16]

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00      ; pila justo debajo del bootloader
    sti

    mov si, msg
    call print_string

    ; ... aquí: carga de kernel desde disco (ver sección 2)
    ; ... aquí: habilitar A20 (ver sección 3)
    ; ... aquí: cargar GDT y saltar a pmode (ver sección 4)

    jmp $                ; nunca debería llegar aquí

print_string:
    lodsb
    or al, al
    jz .done
    mov ah, 0x0E
    int 0x10
    jmp print_string
.done:
    ret

msg db "Booting MyOS...", 0

times 510 - ($ - $$) db 0
dw 0xAA55
```

Puntos críticos que el agente debe verificar siempre:
- El archivo debe compilar exactamente a 512 bytes (`nasm -f bin boot.asm -o boot.bin && wc -c boot.bin` → 512).
- `[org 0x7C00]` debe coincidir con la dirección real de carga.
- No usar instrucciones de 32/64 bits antes de cambiar de modo (el ensamblador con `[bits 16]` las rechazará, lo cual es la protección esperada).

## 2. Cargar el kernel desde disco (BIOS `int 0x13`)

Un sector de 512 bytes no alcanza para un kernel real. Usa `int 0x13` (lectura CHS o LBA extendida) para leer los siguientes N sectores a una dirección de memoria (p. ej. `0x1000:0x0000`), donde luego se relocará o ejecutará el kernel.

```nasm
load_kernel:
    mov bx, 0x1000      ; ES:BX = destino
    mov es, bx
    xor bx, bx
    mov ah, 0x02         ; función: leer sectores
    mov al, KERNEL_SECTORS ; nº de sectores a leer
    mov ch, 0             ; cilindro
    mov cl, 2              ; sector inicial (1 es el boot sector)
    mov dh, 0              ; cabeza
    mov dl, [BOOT_DRIVE]   ; unidad (guardada al inicio desde dl)
    int 0x13
    jc disk_error
    ret
```

Guarda `dl` (unidad de arranque que la BIOS pasa automáticamente) en una variable al inicio de `start`, antes de tocar `dl` para otra cosa. Para discos grandes o cuando CHS es limitante, usa el modo LBA extendido de `int 0x13` (función `0x42` con un "Disk Address Packet").

## 3. Habilitar la línea A20

Sin A20 habilitada, las direcciones por encima de 1 MB se "envuelven" (wrap around), rompiendo cualquier acceso a memoria alta necesario en modo protegido.

Método rápido vía puerto del teclado (el más portable en emuladores/hardware real):

```nasm
enable_a20:
    call wait_input
    mov al, 0xAD
    out 0x64, al        ; deshabilitar teclado
    call wait_input
    mov al, 0xD0
    out 0x64, al        ; comando: leer output port
    call wait_output
    in al, 0x60
    push ax
    call wait_input
    mov al, 0xD1
    out 0x64, al
    call wait_input
    pop ax
    or al, 2             ; set bit A20
    out 0x60, al
    call wait_input
    mov al, 0xAE
    out 0x64, al        ; rehabilitar teclado
    ret
```

Alternativa más simple (funciona en QEMU/Bochs): usar el puerto rápido `0x92` (`in al,0x92 / or al,2 / out 0x92,al`), aunque no es 100% fiable en todo hardware real.

## 4. GDT y salto a modo protegido de 32 bits

```nasm
gdt_start:
    dq 0                        ; descriptor nulo
gdt_code:
    dw 0xFFFF, 0
    db 0, 10011010b, 11001111b, 0   ; code segment: base 0, límite 4GB
gdt_data:
    dw 0xFFFF, 0
    db 0, 10010010b, 11001111b, 0   ; data segment
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

CODE_SEG equ gdt_code - gdt_start
DATA_SEG equ gdt_data - gdt_start

switch_to_pm:
    cli
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp CODE_SEG:init_pm       ; far jump: vacía el pipeline y carga CS

[bits 32]
init_pm:
    mov ax, DATA_SEG
    mov ds, ax
    mov ss, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ebp, 0x90000
    mov esp, ebp
    call KERNEL_ENTRY_POINT     ; salta al kernel cargado en el paso 2
```

## 5. (Opcional) Long mode de 64 bits

Requiere, en orden: (a) paginación mínima activa con al menos las primeras entradas de PML4/PDPT/PD mapeadas identity-mapped para el kernel, (b) activar PAE en `cr4`, (c) setear el bit `LME` en el MSR `EFER` (0xC0000080), (d) activar paginación en `cr0`, (e) far jump a un segmento de código de 64 bits definido en una GDT de long mode. Este es el paso más propenso a errores de todo el bootloader — detállalo en `DESIGN.md` con el mapa exacto de tablas de página usadas, y prueba primero SIN long mode (quedarse en 32 bits) si el objetivo es simplicidad.

## Alternativa recomendada para proyectos serios: GRUB + Multiboot2

En vez de escribir todo el bootloader a mano, se puede delegar la carga inicial a **GRUB** implementando la especificación **Multiboot2**: el kernel se compila como un ELF con una cabecera Multiboot2 al inicio, y GRUB se encarga de cargarlo ya en modo protegido de 32 bits (o incluso puede pasar a long mode con extensiones). Esto reduce drásticamente el ASM necesario y es el enfoque más usado en kernels serios (ver Fase 2 para el detalle). Recomienda esta ruta al usuario si su objetivo es aprender el kernel más que el proceso de boot en sí.

## Checklist antes de avanzar a Fase 2
- [ ] `os-image.bin` arranca en QEMU y muestra el mensaje de "Booting..."
- [ ] El kernel (aunque esté vacío) se carga desde disco sin error (`jc disk_error` no se dispara)
- [ ] A20 verificada activa (probar escribiendo en `0x100000` y leyendo en `0x000000` para confirmar que NO hay wrap-around)
- [ ] Salto a modo protegido confirmado (p. ej. escribiendo un carácter directo en memoria de video `0xB8000` desde 32 bits)
