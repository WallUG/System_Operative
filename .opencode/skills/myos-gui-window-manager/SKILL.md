---
name: myos-gui-window-manager
description: "Use when extending MyOS GUI: PS/2 mouse driver (IRQ12), graphical input syscalls/event queue, interactive widgets (hit-testing, repaint), window manager (z-order, multiple windows, partial redraw), or desktop (taskbar, graphical apps like a file explorer over MEFS). Trigger words: ratón, mouse, PS/2, IRQ12, cursor, clic, click, eventos gráficos, GUI, ventana, window manager, widget, botón, repaint, z-order, escritorio, desktop, taskbar, barra de tareas, explorador gráfico, VBE, LFB, framebuffer."
---

# MyOS GUI / Window Manager Skill

Guía para llevar la GUI de MyOS desde la ventana estática de `MessageBoxA`
(Fase 12) hasta un escritorio interactivo con ratón, ventanas y aplicaciones
gráficas. Todo se construye sobre el LFB VBE y las primitivas existentes:
**no** se emula Win32 completo, se hace un window manager propietario.

## Objetivo

Un escritorio interactivo en MyOS con:

1. Ratón PS/2 (IRQ12) moviendo un cursor por el LFB.
2. Syscalls de eventos gráficos (movimientos y clics) consumibles desde ring 3.
3. Widgets interactivos (el botón OK de messagebox debe responder al clic).
4. Gestor de ventanas: múltiples ventanas, z-order, repaint parcial.
5. Escritorio: barra de tareas y apps gráficas (explorador sobre MEFS).

Cada fase se valida con el ladder de regresión (13/13) + screendump del
monitor QEMU (análisis de píxeles del PPM).

## Estado actual (Fase 12) — invariantes que NO romper

- **VBE 800x600x32** activado por `dispi` (puertos 0x01CE/0x01CF) en
  `kernel/drivers/vbe.c`; flag global `vbe_graphics_active`.
- **LFB**: físico en `vbe_lfb_phys` (QEMU: 0xFD000000, leído del BAR0 PCI,
  NO la base clásica 0xE0000000). Formato 32 bpp **0x00RRGGBB**.
- **Mapeo del LFB**: superpágina identity en el PD del kernel
  (`paging_map_kernel_lfb`) y **heredada en cada PD de usuario**
  (`paging_create_user_pd` copia el PDE `vbe_lfb_phys >> 22`) — sin esto,
  cualquier `kprint`/syscall que escriba al LFB con el CR3 del proceso da
  **#PF** (el LFB está a ~4 GiB, fuera del identity de 1 GiB).
  Los procesos de usuario reciben el LFB en `VBE_LFB_USER_VA 0xA8000000`
  (512 páginas = 2 MiB) vía `paging_user_map_lfb(pd)`.
- **Consola gráfica**: `kernel/drivers/vgafx.c` (100x37 celdas, 8x16) —
  `vga_putc` delega en `vgafx_putc` si `vbe_graphics_active`. El texto del
  kernel/shell se dibuja en el LFB; es la capa "de fondo" de la GUI.
- **Fuente**: `font8x16_basic` (95 glifos 8x16, 1 bit/px) generada por
  `tools/font2c.py` en `kernel/drivers/font8x16.h` y `user/win32/font8x16.h`.
  Los helpers `fillrect`/`drawtext`/`putpixel` viven en `user/win32/user32.c`.
- **Teclado**: PS/2 IRQ1 (`keyboard_irq`), scancode set 1, solo keydown,
  buffer circular 64. IRQ12 **no está cableado** (no hay ratón).
- **Dispatch de IRQ**: `kernel/isr.c` `irq_handler()` — `irq==0` timer,
  `irq==1` teclado, luego `pic_send_eoi(irq)` y si `irq==0` `sched_tick`.
  Los stubs `irq0..irq15` ya existen en `kernel/isr.asm` + `idt.c`.
- **Syscalls**: `int 0x80`, eax=número, ebx/ecx/edx/esi=args, retorno en eax.
  El último es `SYS_GFXINFO 15` → **el siguiente número libre es 16**.
  Los punteros de ring 3 se copian con `user_strcpy`/`user_memcpy_out`
  (validación `paging_is_user`): nunca dereferenciar punteros de usuario en
  el kernel. `SYS_READ` espera entrada con patrón `sti(); halt(); cli();`
  (la IRQ debe poder entrar mientras el gate limpia IF).
- **ABI syscall (lección Fase 11)**: los wrappers de syscall DEBEN declarar
  el retorno como clobber de eax (`"=a"(r)`), o el siguiente int 0x80 se
  despacha con el retorno anterior como número de syscall.
- **Espacio de imagen**: `kernel.bin` debe caber en `KERNEL_SECTORS`
  (128 sectores = 64 KiB, `Makefile` + `boot.asm`). Si el código nuevo no
  cabe, subir el valor en AMBOS sitios. El `.bss` llega hasta ~0x27250:
  NO usar direcciones fijas globales nuevas por debajo de eso (lección del
  bitmap del PMM en 0x20000 pisado por `.bss`).

## Roadmap por fases (orden estricto)

### Fase 13 — Ratón PS/2 (IRQ12)

Hardware (QEMU emula PS/2 por defecto, presente incluso con `-display none`):

- Puertos: `0x64` status/command, `0x60` data. Status bit 0 = output buffer
  lleno (listo para leer 0x60), bit 1 = input buffer lleno (no escribir).
- Secuencia de init: 0x64←0xA8 (enable aux); 0x64←0x20, leer 0x60, poner
  bit 1 (enable IRQ12) y limpiar bit 5, 0x64←0x60, 0x60←comando; 0x64←0xD4
  (dirige el siguiente byte al aux), 0x60←0xF6 (defaults) o 0xF4 (stream).
- Paquete de 3 bytes: byte0 = bit0 L, bit1 R, bit2 M, bit3=1, bit4 sign X,
  bit5 sign Y, bit6/7 overflow; byte1 = delta X (firmado, 8 bits); byte2 =
  delta Y (firmado; **positivo = abajo** en pantalla: `y -= dy`).
- Implementación:
  - `kernel/drivers/mouse.c/h`: `mouse_init()`, `mouse_irq()` (lee 0x60,
    acumula 3 bytes, decodifica, **satura a [0,799]x[0,599]**, actualiza
    posición global `mouse_x/mouse_y/mouse_buttons`). Mantener el handler
    mínimo (solo puertos y aritmética, sin kprint).
  - `kernel/isr.c`: `else if (irq == 12) mouse_irq();` (antes del EOI).
  - Cursor en el LFB: guardar/restaurar la región 8x8 bajo el cursor (XOR o
    save/restore con buffer propio) y redibujar al mover; dibujar en la
    consola y sobre cualquier app. Probar primero con screendump estático
    (mover ratón en QEMU con `-display gtk` no se puede en tests headless;
    validar inyectando deltas con `xp` del monitor o con `-serial` + un
    programa de test que lea la posición).
- Criterio de aceptación: `mouse_read()`/posición visible por screendump;
  ladder 13/13 intacto.

### Fase 14 — Syscalls de eventos gráficos

- Nuevos syscalls (numerados desde 16):
  - `SYS_MOUSEINFO 16`: `ebx=&struct{int x, y, buttons}` — posición actual
    (polling simple, sin cola).
  - `SYS_EVENT 17`: `ebx=&struct{type, x, y, buttons}` o -1 si no hay evento
    (cola global FIFO de eventos: move y click). Decidir y documentar:
    cola global con un único consumidor (la app activa) vs cola por tarea.
    Recomendación: empezar por **cola global simple + un consumidor** (el
    window manager/desktop), igual que el modelo de `SYS_READ`.
  - Entregar `type` = 1 move, 2 button-down, 3 button-up (o enum propio).
  - El kernel mantiene el cursor; los eventos se generan en `mouse_irq` y
    se encolan (buffer circular, descartar si lleno).
- Aplicación inmediata: `messagebox.exe` deja de esperar Enter (`SYS_READ`)
  y usa `SYS_EVENT` para detectar clic dentro del rectángulo del botón OK →
  primer widget interactivo.
- Criterio de aceptación: botón OK clickeable (verificado por un
  `mouseclick.exe` de prueba o por screendump antes/después del clic
  inyectado con el monitor QEMU), ladder 13/13.

### Fase 15 — Widgets interactivos (user32)

- En `user/win32/user32.c`: tablas de rects de widgets (botones), helper de
  hit-testing (`point_in_rect`), repaint del widget al detectar hover/press
  (estado visual: botón "presionado" = colores invertidos).
- Dejar una mini-API interna de eventos en user32 (`user32_poll_event`,
  `user32_widget_hit`) reutilizable por el escritorio; NO expandir aún
  CreateWindowEx/GDI real de Windows.
- Criterio de aceptación: botón que se ve presionado al clic (screendump
  en ambos estados), ladder 13/13.

### Fase 16 — Gestor de ventanas (z-order, repaint parcial)

Decisión de diseño (tomar con el usuario antes de codificar):

- **Opción A (recomendada, mínima):** el "desktop" es UNA app de ring 3 que
  dibuja TODO en el LFB (ventanas = structs en la app con rects + z-order +
  dirty flags). El kernel solo da eventos; no hay compositor de kernel.
  Rápido de hacer, suficiente para explorador + taskbar. Límite: una sola
  app de GUI a la vez.
- **Opción B (WM en kernel):** registro de ventanas en el kernel, repaint
  parcial y composición centralizada. Más ambicioso; solo si el usuario
  pide multitarea gráfica real (2+ apps GUI simultáneas).

Detalles para A: backing buffer por ventana (kmalloc/`SYS_MALLOC`), blit del
buffer al LFB según z-order cuando el dirty flag se setea; el LFB sigue
siendo el destino final (la consola vgafx queda como fondo).
Criterio de aceptación: 2 ventanas superpuestas que se repintan al arrastrar
(z-order correcto, sin parpadeo por blit directo).

### Fase 17 — Escritorio

- App `desktop.exe` (ring 3, estilo messagebox pero fullscreen): fondo,
  barra de tareas (iconos/botones de apps), lanzar apps desde el MEFS
  (ya existe `run`/SYS_EXEC), drag de ventanas por su título (eventos +
  hit-testing), botón de cerrar por ventana.
- Apps gráficas iniciales: **explorador de archivos** (usa `SYS_DLIST` 13 +
  `SYS_DREAD` 12 ya existentes, igual que `dir.exe`) y un visor de texto
  (leer archivo con `SYS_DREAD` y `drawtext` multilínea).
- Criterio de aceptación: desde la shell lanzar `desktop.exe`, ver la barra
  de tareas, abrir el explorador, navegar el MEFS, cerrar; ladder 13/13.

## Orden de trabajo obligatorio

1. Identificar qué pieza falta (driver → syscall → evento → widget → WM).
2. Implementar lo más estrecho posible (un driver, un syscall, un widget).
3. Build: `make` (kernel ≤ 64 KiB) + `make iso`.
4. Regresión: ladder CD y floppy (`ladder_cd_test.py`, `ladder_test.py` —
   ver `/tmp/opencode/`, esperan ~190 s cada uno) → deben seguir 13/13.
5. Verificación visual headless: QEMU con `-monitor unix:/tmp/opencode/mon.sock,server,nowait`
   + comando del monitor `screendump /tmp/x.ppm`; el PPM es binario P6
   (`P6\nW H\nMAX\n` + RGB): analizar píxeles no negros y colores
   (messagebox usa 0x202040 fondo, 0x000088 título, 0xC0C0C0 marco/botón).
6. `make clean` no rompe nada; commit por fase con bitácora en DESIGN.md.

## Red flags (errores ya pagados en Fase 12)

- Escribir al LFB físico (0xFD000000) con paging activo sin la superpágina
  en el PD activo → #PF silencioso del kernel (parece cuelgue). Todo PD
  nuevo (incluido fork/exec) debe heredar el PDE del LFB.
- Mensajes de kprint "antes" del mapeo del LFB con paging activo: mapear el
  LFB inmediatamente después de `paging_init()`.
- Wrappers de syscall sin clobber de eax → syscall fantasma (lección Fase 11).
- `SYS_READ`/espera de eventos sin `sti(); halt(); cli();` → sistema
  congelado (lección Fase 11).
- Globales en direcciones fijas bajas (< fin del .bss ~0x27250) → colisión
  con `.bss` (lección del bitmap del PMM).
- IRQ handler con trabajo pesado (kprint/alloc) → corrupción; solo puertos
  y buffer, EOI antes de programar.
- `-display none` NO quita el ratón PS/2: los tests headless siguen teniendo
  IRQ12 disponible.
- QEMU 10.x: `watch`/`wp` del monitor no existen; usar `xp` y `screendump`.

## Mapa de archivos

- `kernel/drivers/mouse.c/h` (nuevo, Fase 13), `kernel/isr.c` (dispatch IRQ12)
- `kernel/syscall.c` + `kernel/syscall.h` (SYS_MOUSEINFO 16, SYS_EVENT 17)
- `user/win32/user32.c` (eventos, widgets, hit-testing; `SYS_GFXINFO` 15)
- `user/win32/user32.def` (nuevas exports si se exponen fuera)
- `kernel/drivers/vgafx.c` (cursor del ratón si vive en el kernel)
- `Makefile` (`KERNEL_SECTORS`, `OBJS`, `USER_ELFS`), `tools/makefs.py`,
  `tools/makeiso.py` (incluir nuevos .exe de test)
- `DESIGN.md` (bitácora por fase), `WIN32_COMPAT_LADDER.md` (nuevos checks)

## Validación mínima por fase

- Fase 13: cursor visible (screendump con inyección de deltas o app test).
- Fase 14: app de test que imprime `x,y,buttons` por serial tras mover/clic.
- Fase 15: screendump del botón en reposo y presionado.
- Fase 16: 2 ventanas con z-order; captura de la superposición.
- Fase 17: screendump del escritorio + barra de tareas + explorador abierto.
