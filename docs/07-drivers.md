# MyOS — Drivers

Archivos: `kernel/drivers/*.c|h`, `kernel/pic.c`, `kernel/panic.c`,
`kernel/kprint.c`.

## Interfaz general

- **Puertos**: helpers inline en `kernel/io.h` (`inb/outb/inw/outw/inl/
  outl`).
- **Pic 8259** (`kernel/pic.c`): remapea IRQ0-15 a vectores 32-47 (fase de
  init). `pic_send_eoi`, `pic_ack_irq`.

## VGA texto (`vga.c`)

- Buffer 0xB8000 (80×25, atributo 2B/char).
- `vga_putc` / scroll suave; usado solo en modo texto (antes de VBE) y
  como duplicado en serial. En modo gráfico el kernel usa `vgafx`.

## Serial (`serial.c`)

- COM1 (0x3F8), PIO 16550. `serial_putc/puts/read_char`.
- `serial_read_char` con poll de LSR bit 0 (sin IRQ), usado por la shell
  como **fuente de entrada headless** (QEMU `-serial stdio`).
- `SYS_PRINT`/`SYS_WRITE` escriben aquí; tope de reintentos THRE para no
  colgar por backpressure del chardev.

## PIT (`timer.c`)

- IRQ0, ~100 Hz configurable (`timer_init`), base del scheduler
  round-robin y de `Sleep`/`GetTickCount` (shim kernel32).

## Teclado PS/2 (`keyboard.c`)

- Puerto 0x60/0x64 (8042). Scancode → caracter (layout US básico,
  shift/capslock).
- **Inicialización**: escribir 0xAE (habilitar) **+ config byte bit 0**
  (`0x20` → `0x60`, `config|0x01`) para habilitar IRQ1 (bug #3 Fase 17:
  sin el bit 0, los scancodes quedaban en la cola del controlador y el
  driver "no veía" nada).
- En IRQ1: encola el caracter en el buffer circular de la shell **y** un
  `EV_KEY` (ventana de teclado vía kprint / cola de eventos gráficos).

## Ratón PS/2 (`mouse.c`)

- Init del 8042: 0xA8 (activar aux), config byte **bit 1** (IRQ12),
  borrar bit 5 (translate), 0xD4+0xF6 (defaults), 0xD4+0xF4 (stream);
  drenar los dos acks 0xFA (asíncronos vía IRQ12).
- Paquete de 3 bytes: byte0 = L/M/R (bits 0-2), sync bit3 (=1), signos y
  overflow X/Y (bits 4-7); byte1 = delta X; byte2 = delta Y (**positivo =
  abajo: `y -= dy`**). Overflow descarta el paquete.
- Posición saturada a [0,799]×[0,599], inicia centrado (400,300).
- **Cursor 8x8 con save/restore** de la zona pisada (`cursor_save[64]`):
  se restaura antes de dibujar en la nueva posición. El WM redibuja el
  cursor tras cada composición (el blit lo pisa).
- Genera eventos gráficos: en cada paquete válido → `EV_MOVE`; cambios de
  botones → `EV_BUTTON_DOWN`/`EV_BUTTON_UP` en las coordenadas actuales.

## ATA (`ata.c`)

- PIO (canal 0, maestro). `ata_read_sector(lba, buf)` usado por MEFS en
  modo disco. Sin escritura (solo lectura, roadmap).

## VBE (`vbe.c`) — ver 08-gui.md

## Panic (`panic.c`)

- `kpanic(...)`: imprime estado (registros, EIP, CR2/err si #PF), halt
  infinito. `kpanic_page_fault` captura #PF del kernel con CR2 + err.
- Los #PF de usuario no panican: se matan (ver 04-multitarea.md).

## kprint (`kprint.c`)

- mini-printf del kernel (%d/%x/%s/...): a serial + vgafx (si el modo
  gráfico está activo y sin ventanas del WM).

## Bugs históricos

- **ISR stub (Fase 3)**: ds/es pusheados después de pusha → slot del
  arg → handler leía el valor de ds como `registers_t*`. Fix: `push eax`
  explícito + `-fno-optimize-sibling-calls`.
- **IRQ1 nunca habilitada (Fase 17)**: ver arriba — config byte bit 0.
- **Ratón**: el driver drena acks asíncronos para no corromper el primer
  paquete.