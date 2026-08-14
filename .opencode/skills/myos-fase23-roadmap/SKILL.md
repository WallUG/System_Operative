---
name: myos-fase23-roadmap
description: "Use when planning or implementing MyOS Fase 23 — the compatibility roadmap that prioritizes stability over new surface. Covers: (A) render performance (region-based composition, fps in metapad, no flicker), explorer scrollbar + End/Home, focus/event routing between apps, and the Windows-style installer (dialog that copies files from the MEFS to a persistent directory structure); (B) Win32 surface only after A is stable: real modal dialogs (DialogBoxParamA/EndDialog over menu_modal), real message loop (GetMessageA/DispatchMessageA + accelerators), extended CreateWindowExA (WS_OVERLAPPEDWINDOW/WS_CHILD, WM_CREATE, WM_SIZE/WM_MOVE), more DLLs (OLE32/SHLWAPI/WINSPOOL); (C) data layer: binary/text modes (_O_BINARY/_O_TEXT), per-process heap (HeapAlloc/HeapFree), explorer rewritten as a real Win32 .exe with comctl32 listview. Golden rule: every item = build + QEMU headless test + full regression (20a-d, faseE, fase22) + commit; revert if metapad or the desktop breaks. Trigger words: Fase 23, fase23, roadmap 23, instalador, installer, dialogos modales, DialogBoxParamA, message loop, GetMessageA, heap por proceso, HeapAlloc, listview, parpadeo, flicker, renderizado, composicion por region, scrollbar, OLE32, SHLWAPI, WINSPOOL, _O_BINARY, per-process heap."
---

# Fase 23 — Compatibilidad sin sacrificar estabilidad

Guía accionable para la Fase 23 de MyOS: consolidar la estabilidad del
sistema base (renderizado, foco/eventos, instalador) y SOLO DESPUÉS abrir
más superficie Win32. Todo ítem debe mantener metapad.exe, el escritorio
y el explorer funcionando (regresión completa por ítem).

Referencia de estado: la Fase 22 (arranque tipo Windows: bootscreen,
autoboot con flag `bootgui`, shell viva por debajo) está COMPLETA y los
fixes recientes (scroll del explorer, `wm_update` por regiones sin
parpadeo, `mouse_event_flush` al crear ventana) están en main.

## Regla de oro (innegociable)

1. Cada ítem = compilar + test QEMU headless + **regresión completa** +
   un solo commit.
2. Regresión = `tools/test_fase20a.py` (8/8), `20b` (3/3), `20c` (7/7),
   `20d` (6/6), `test_faseE.py` (6/6), `test_fase22.py` (7/7). Si
   cualquiera falla, el ítem NO está terminado.
3. Si un ítem de B/C rompe metapad o el escritorio, revertir y rehacer
   con menor alcance.
4. NO tocar boot/autoboot/bootscreen salvo que un ítem lo exija (ya es
   estable). Los tests de autoboot usan el helper `cancel_autoboot()`
   (espera "Autoboot:" y envía una tecla que el kernel descarta).

## Arquitectura de referencia (mapa de archivos)

- **WM (kernel)**: `kernel/winmgr.c/h` — composición del LFB, z-order,
  snapshots, `wm_compose` (completo), `wm_update` (ahora por región vía
  `wm_update_rect`), `wm_route` (enrutado de eventos por PD), colas por
  app (`wm_event_deliver`/`wm_event_claim`), `wm_create` (flushea la cola
  global con `mouse_event_flush`), `wm_layout` (cliente según título/
  menú/toolbar), `wm_draw_one` (marco+título+menú+toolbar+X+`wm_blit_client`).
- **Eventos**: `kernel/drivers/mouse.c` (cola FIFO global EV_*,
  `mouse_event_dequeue`, `mouse_event_flush`), `kernel/drivers/keyboard.c`
  (set 1, VK especiales 0x100+).
- **syscalls**: `kernel/syscall.c` (SYS_WINUPDATE 21 con rect opcional en
  ecx, SYS_WINCREATE 18, SYS_EVENT 17, SYS_MOUSEINFO 16, SYS_WININFO 22...).
- **Apps ELF nativas**: `user/desktop.c` (taskbar + fork+exec de apps),
  `user/explorer.c` (lista MEFS con scroll, visor de texto, lanzador).
- **Capa Win32**: `user/win32/` — user32.c (ventanas, menús, `menu_modal`
  para TrackPopupMenuEx, `event_to_wm`), kernel32.c, gdi32.c, msvcrt,
  comdlg32.c, comctl32.c. Modulos ELF ring-3 fijos (kernel32.dll etc.).
- **FS**: `kernel/fs/mefs.c/h` — superbloque MEFS v2 (bitmap, subdirs,
  flag boot_gui en offset 36).
- **Tests**: `tools/test_fase2X.py` con el patrón QEMU headless (monitor
  unix socket + serial stdio + screendumps + sendkey). El ratón del
  monitor HMP/QMP NO inyecta eventos PS/2 de forma fiable: para probar
  clics se inyectan eventos sintéticos en la cola del kernel (patrón del
  fix del menú) o se usa teclado.

## Bloque A — Estabilidad primero (hacer antes de B/C)

### A1. Rendimiento del renderizado (composición por región)

**Estado: COMPLETADO (commit f94aa56 + 23-A1).** `wm_update(id)` ya usa
`wm_update_rect`; se extrajo `wm_redraw_rect(rx,ry,rw,rh)` (coords de
pantalla: restaura el rect del fondo + repinta las ventanas que lo
intersectan en orden z + fijas) y se usa en `wm_create`, `wm_close`,
`wm_cleanup_pd`, `wm_move`, el drag de `wm_route`, `wm_set_title`
(solo la franja del título), `wm_set_menu`/`wm_set_toolbar` (rect de la
ventana). El único `wm_compose()` completo restante es la restauración
de la consola al quedar sin ventanas (wm_recompute) — correcto.
FALTA (opcional): medir fps reales con contador al serial y un test
dedicado test_fase23a1.py (hoy la validación fue visual con screendumps
en ráfaga durante crear/cerrar/mover).

### A2. Scrollbar del explorer + Home/End

**Estado: COMPLETADO.** `user/explorer.c`: teclas `VK_SPECIAL_HOME` 0x104
→ sel=0, `END` 0x105 → sel=nfiles-1, `PGUP` 0x106 / `PGDN` 0x107 →
sel ± vis (página). Scrollbar visual: franja SB_W=12 px a la derecha
del cliente (solo si nfiles > vis) con thumb proporcional a vis/nfiles
(`sb_thumb()`: th = track*vis/nfiles, ty = HDR_Y + (track-th)*scroll_off/
(nfiles-vis)); clic en la franja: encima del thumb = PgUp, debajo =
PgDn, en el thumb = salto proporcional (scroll_off = (y-ty)*(nfiles-vis)/
(track-th)); el hit de filas excluye la franja. `tools/makefs.py` ampliado
con `--dir NAME:p1,p2,...` (subdirectorios con parent=índice, formato del
kernel) para generar FS de prueba con muchos archivos. Test
`tools/test_fase23a2.py` **9/9 PASS**: thumb visible y posicionado en la
raíz, End baja/Home sube, navegación a subdir con 35 entradas,
25×down, PgDn/PgUp. Nota: con nfiles≈vis el PgUp desde el fondo casi no
mueve el thumb (pocas líneas de overflow) — es comportamiento real.
Colores en PPM: thumb=0x80A080, track=0x305030 (bytes LE del LFB).

### A3. Foco y eventos entre apps

**Estado**: `mouse_event_flush` al crear ventana evita teclas fantasma en
la app nueva. Falta: verificación sistemática del cambio de foco por clic
entre ventanas (desktop + explorer + metapad a la vez) y que las teclas
vayan a la ventana correcta.

**Tarea**:
- Reproducir con 3 apps abiertas a la vez; clic en cada una y teclear.
- Verificar `wm_route`: EV_KEY → topmost, EV_BUTTON_* → dueño bajo el
  cursor, drag en título, X → EV_WINCLOSE.
- Si un clic en una ventana cubierta no la sube (wm_raise solo en área
  cliente), decidir: clic en cualquier parte visible debe raise.
- Probar la vuelta a la consola (cerrar todas las ventanas) y que la
  shell recibe el teclado limpio (`keyboard_flush` ya cubre la última
  tecla).

**Test**: test_fase23a3.py con 3 apps, clics sintéticos + sendkeys,
verificando por serial qué app recibe cada tecla.

### A4. Instalador tipo Windows

**Tarea**: una app Win32 real (p.ej. `installer.exe` o ELF nativo con
diálogo) que copie archivos del FS a un directorio persistente con
estructura (crear subdirectorios destino, copiar archivos con
`mefs_read`/write vía syscalls, `flush` al final). Reutiliza: diálogo
modal (si el modal real de B5 aún no existe, usar el modal de menús/
messagebox), listado de directorios (SYS_DLISTDIR 33), persistencia
(SYS_DWRITE + flush).

**Requisitos previos útiles**: A2 (navegación), B5 (dialogo modal real)
o el patrón de `GetSaveFileNameA` de comdlg32.

**Validación**: instalar archivos, verificar en el disco persistente que
la estructura de directorios y los bytes están correctos (leer el FS
desde el host con el formato de makefs.py); reboot y navegar con el
explorer al directorio instalado.

## Bloque B — Superficie Win32 (solo cuando A esté estable)

### B5. Diálogos modales reales: DialogBoxParamA/EndDialog

**Estado**: `menu_modal` en user32.c ya es un modal genérico (bucle de
eventos con sys_event hasta que se elige/cancela) usado por
TrackPopupMenuEx. Los diálogos de archivo de metapad (GetOpenFileNameA/
GetSaveFileNameA) se abren con mensajes al wndproc, NO son modales
reales.

**Tarea**: implementar `DialogBoxParamA(hinst, template, hwndParent,
DlgProc, param)` y `EndDialog` sobre el patrón de `menu_modal`:
- Template: RT_DIALOG — por ahora parsear un formato propio o el
  RT_DIALOG de mingw (recursos), reutilizando el parser RT_MENU como guía.
- El modal drena eventos, enruta WM_COMMAND/close al DlgProc, y
  `EndDialog` sale con el resultado.
- `WM_INITDIALOG`, controles STATIC/BUTTON/EDIT dentro del modal.

**Por qué**: abre la puerta a decenas de .exe con diálogos propios (no
solo los de metapad).

**Test**: test_fase23b5.py con un .exe de prueba mingw que abre un
DialogBox con un botón OK que devuelve IDOK; verificar WM_INITDIALOG y
EndDialog(IDOK).

### B6. Message loop real: GetMessageA/DispatchMessageA

**Estado**: metapad hace su loop manualmente (Sys_PollEvent + switch).
`GetMessageA`/`DispatchMessageA` existen como stubs o simplificados.

**Tarea**: implementar el bucle completo:
- `GetMessageA(msg, hwnd, min, max)` bloquea hasta que hay mensaje;
  `PeekMessageA` no bloquea; filtros por hwnd.
- `TranslateMessage` (WM_KEYDOWN → WM_CHAR, hoy directo en event_to_wm)
  y `TranslateAccelerator` (aceleradores de la tabla del menú).
- `DispatchMessageA` → wndproc por hwnd.
- Adaptar `event_to_wm` para encolar WM_* en msgq y que GetMessage los
  sirva (hoy msgq_push ya existe por wndproc).

**Por qué**: muchos .exe de mingw hacen `while (GetMessage(&msg,0,0,0))`.
**Riesgo**: alto — cambiar el loop de metapad puede romperlo. Estrategia:
implementar GetMessageA de forma que siga sirviendo el msgq actual,
verificar que metapad (que NO usa GetMessageA) no cambia, y luego probar
con un .exe que sí lo use.

### B7. CreateWindowExA extendido

**Estado**: `CreateWindowExA` maneja clases builtin (RichEdit20A/EDIT/
STATIC/BUTTON/statusbar) con hijos virtuales dibujados en el buffer del
padre. Faltan estilos y mensajes.

**Tarea**:
- Estilos: `WS_OVERLAPPEDWINDOW`, `WS_CHILD`/`WS_VISIBLE`, `WS_CAPTION`,
  `WS_BORDER`, `WS_THICKFRAME`.
- Mensajes: `WM_CREATE` (al crear, antes de mostrar), `WM_SIZE`/`WM_MOVE`
  reales al mover/redimensionar (hoy el hijo se redimensiona sin avisar),
  `WM_SETTEXT`/`WM_GETTEXT`, `WM_NOTIFY` de los controles.
- Controles: STATIC con texto multi-línea, BUTTON con focus visual,
  EDIT con caret (el RichEdit ya lo tiene).

**Por qué**: apps con múltiples ventanas/controles dependen de esto.
**Riesgo**: medio — el wnd_proc[] actual está indexado por id; los
mensajes nuevos deben respetar ese modelo.

### B8. Más DLLs: OLE32/SHLWAPI/WINSPOOL

**Tarea**: stubs y funciones de bajo costo en módulos nuevos:
- OLE32: `CoInitialize`/`CoUninitialize` (no-op), `CoCreateInstance` →
  COM simplificado (solo interfaces que las apps de referencia usen).
- SHLWAPI: `StrStrI`, `StrCmpI`, `PathFileExists`, `PathFindFileName`,
  `PathRemoveFileSpec` (strings de rutas — útiles con el instalador).
- WINSPOOL: `OpenPrinterA`/`StartDocPrinterA`/`WritePrinter` →
  volcado a un archivo .txt en el FS (impresión "a archivo").

**Por qué**: más .exe de referencia cargan si estas DLLs existen.
**Riesgo**: bajo (stubs) si las funciones nuevas se prueban una a una.

## Bloque C — Datos y persistencia

### C9. Modos binario/texto (_O_BINARY/_O_TEXT)

**Estado**: `SYS_DREAD`/`SYS_DWRITE` son crudos (bytes). Los .exe de mingw
abren archivos de texto esperando traducción CRLF↔LF según el modo.

**Tarea**: en kernel32 `_open`/`_read`/`_write`/`CreateFileA`+`ReadFile`:
- Flag `_O_BINARY` (default en Windows) = sin traducción.
- `_O_TEXT` = traducir LF→CRLF al escribir y CRLF→LF al leer (con
  buffer de estado para CR pendiente).
- `fopen` de msvcrt y el stdio de metapad deben respetarlo.

**Por qué**: archivos creados en Windows con CRLF se ven con ^M en el
visor y viceversa; metapad guarda con \n (texto) y lo re-lee bien, pero
los archivos externos sufren.
**Riesgo**: medio — metapad guarda texto: NO cambiar el default sin
verificar que metapad sigue guardando/leyendo bien.

### C10. Heap por proceso (HeapAlloc/HeapFree reales)

**Estado**: bump allocator global `SYS_MALLOC` sobre
[USER_HEAP_BASE, USER_HEAP_END) sin free (`SYS_FREE` no-op). msvcrt
`malloc/free` y kernel32 `HeapAlloc` mapean a eso.

**Tarea**: heap por proceso con split/coalesce (first-fit como el heap
del kernel `kernel/mem/heap.c`, que ya es first-fit con split):
- `HeapCreate`/`HeapDestroy`/`HeapAlloc`/`HeapFree`/`HeapReAlloc` en
  kernel32 con bloque por proceso (campos en la task o en el TIB).
- msvcrt `malloc/free/realloc` sobre el mismo heap.
- `SYS_FREE` deja de ser no-op.

**Por qué**: apps que hacen free() real y re-allocan (editores, parsers)
hoy agotan el heap.
**Riesgo**: medio — el bump actual es simple; un heap con split puede
tener bugs de coalesce. Empezar con HeapAlloc sobre el bump y free
marcando bloques, luego coalesce.

### C11. Explorer como .exe Win32 con listview

**Estado**: explorer.c es ELF nativo con listado propio.

**Tarea**: reescribir el explorer como .exe Win32 (mingw) usando:
- comctl32 real: `ListView`/`SysListView32` con columnas (nombre,
  tamaño) y selección por teclado/ratón (ya hay InitCommonControlsEx).
- `SYS_DLISTDIR`/`SYS_DPARENT`/`SYS_DLOOKUP` desde Win32 (wrappers en
  winlib o syscall directo).
- Doble clic/Enter abre archivos (visor) y lanza .exe/.elf.

**Por qué**: valida comctl32 listview (ítem 2 del roadmap Fase 20) y
unifica el explorador con el resto del sistema Win32.
**Riesgo**: alto (reescritura completa). Alternativa incremental: primero
un listview.exe de prueba con listado estático, después el navegador.

## Orden sugerido de ejecución

1. A1 (renderizado) — base para todo lo visual.
2. A2 (scrollbar + Home/End).
3. A3 (foco/eventos).
4. A4 (instalador) — app de referencia con persistencia real.
5. B5 (diálogos modales) — puerta a más apps.
6. B6 (message loop) — con cuidado de metapad.
7. B7 (CreateWindowExA extendido).
8. B8 (DLLs) — stubs de bajo riesgo.
9. C9 (binario/texto) — solo tras verificar metapad.
10. C10 (heap por proceso).
11. C11 (explorer Win32).

Cada ítem termina con: compilación OK, test QEMU del ítem, regresión
completa 20a-d + faseE + fase22, y un commit.

## Patrones de test (QEMU headless)

- Lanzar: `qemu-system-i386 -display none -monitor unix:/tmp/opencode/qmon.sock,server,nowait -serial stdio -no-reboot -no-shutdown -drive format=raw,file=build/os-persist.bin` (persist_disk desde el Makefile).
- Los tests deben regenerar `build/os-persist.bin` con `make persist_disk`.
- `cancel_autoboot()`: esperar "Autoboot:" en el serial y enviar una
  tecla (el kernel la descarta) para ir a la shell.
- Los tests que escriben comandos de la shell DEBEN cancelar el autoboot
  primero, y esperar `exit:0` de la app anterior antes de lanzar la
  siguiente (la shell no lee teclado con una tarea de usuario viva —
  `sched_user_busy`).
- El ratón del monitor NO inyecta eventos PS/2 fiables en headless: para
  probar clics inyectar EV_BUTTON_DOWN/UP sintéticos en la cola del
  kernel (o verificar por teclado).
- Screendumps: `screendump /tmp/opencode/x.ppm` en el monitor; el PPM
  de QEMU muestra los bytes del LFB (colores con swap BGR — verificar
  con el color real del buffer, patrón `wl_px_disp`).
- Medir colores en el PPM con el contador por muestreo (grid de 4 px).

## Checklist de commit

- [ ] `make -j$(nproc)` limpio (solo el warning RWX del linker).
- [ ] `make persist_disk` OK.
- [ ] Test del ítem (tools/test_fase23aN.py o similar) PASS.
- [ ] Regresión completa: 20a 8/8, 20b 3/3, 20c 7/7, 20d 6/6, faseE 6/6, fase22 7/7.
- [ ] README.md y esta skill actualizados.
- [ ] Un solo commit con mensaje descriptivo del ítem.
