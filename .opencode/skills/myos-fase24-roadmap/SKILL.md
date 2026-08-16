# Skill: myos-fase24-roadmap

# Fase 24 — Subir el porcentaje de compatibilidad Win32

Guía accionable para la Fase 24 de MyOS: **aumentar el % de apps Win32
clásicas que corren** (hoy ~35% para apps tipo metapad/Notepad), sin
sacrificar la estabilidad ya lograda en la Fase 23 (metapad.exe, el
escritorio y el explorer ELF deben seguir funcionando).

El objetivo NO es correr apps modernas (Chrome etc.), es **correr apps
clásicas Win32** (samples de SDK, Notepad-clones, utilidades) y subir el
porcentaje de forma medible. Prioridad por retorno (% por esfuerzo):
recursos PE, diálogos reales, registro, kernel32 de arranque; después
controles comctl32, threads, GDI blits; por último UX (desktop iconos,
explorer Win32).

## Regla de oro (innegociable, heredada de la Fase 23)

1. Cada ítem = compilar + test QEMU headless + **regresión completa** +
   un solo commit.
2. Regresión = `tools/test_fase20a.py` (8/8), `20b` (3/3), `20c` (7/7),
   `20d` (6/6), `test_faseE.py` (6/6), `test_fase22.py` (7/7),
   `test_fase23a2-a4`, `23b5-b8`, `23c9-c11` (108 checks en total).
   Si cualquiera falla, el ítem NO está terminado.
3. Si un ítem rompe metapad o el escritorio, revertir y rehacer con menor
   alcance.
4. NO tocar boot/autoboot/bootscreen salvo que un ítem lo exija.

## Arquitectura de referencia (mapa de archivos)

- **PE loader**: `kernel/pe.c/h` — carga el PE, resuelve imports contra los
  shims (DLL_ELFS). `tools/makepe.py` convierte ELF ring-3 a PE.
- **Capa Win32 (shims ELF ring-3 fijos)**: `user/win32/` —
  kernel32.c, user32.c, gdi32.c, msvcrt.c, comdlg32.c, comctl32.c, ole32.c,
  shlwapi.c, winspool.c, shell32.c, advapi32.c, ntdll.c.
  Cada uno es un ELF fijo (DLL_BASE por Makefile) con una tabla de exports.
- **Apps .exe de prueba (mingw)**: `user/win32/{hello_win,messagebox,dir,
  proc,writetest,dlltest,txtmode,heaptest,movetest,wintwo,dlgtest,
  gdi_demo,listview}.c`. `metapad.exe` (real, mingw) es la app de referencia.
- **FS**: `kernel/fs/mefs.c/h` — superbloque MEFS v2 (bitmap, subdirs,
  flag boot_gui en offset 36).
- **Tests**: `tools/test_fase2X.py` — patrón QEMU headless (monitor unix
  socket + serial stdio + screendumps + sendkey). Los clics se inyectan
  con `SYS_MOUSE_INJECT` (el monitor QEMU NO inyecta PS/2 fiable).

## Bloque P1 — Máximo % por esfuerzo

### P1.1. Recursos PE (.rsrc): parser menú/diálogo/icono/version

**Estado: PARCIAL (menú y strings ya existian; icono completado).**
`find_resource` (árbol .rsrc) + `LoadMenuA` (RT_MENU) + `LoadStringA`
(RT_STRING) ya estaban en user32 desde Fase 18. Este ítem añadió
**`LoadIconA` real** (RT_GROUP_ICON → RT_ICON, extrae el bitmap 32bpp
ARGB y devuelve un handle válido; antes siempre 0). Test
`tools/test_fase24p11.py` (iconres.exe con icono via windres) 4/4 PASS.
FALTA (opcional): exponer RT_VERSION (GetFileVersionInfoA) y renderizar
el icono (título/escritorio) — el render es de P2.3 (GDI blits).

**Tarea**: añadir en el loader/kernel32:
- Parsear la sección `.rsrc` (tabla de recursos en árbol
  type/name/language → data RVA+size).
- Exponer lectura de recursos al ring 3: menú (RT_MENU), diálogo
  (RT_DIALOG), icono (RT_GROUP_ICON), versión (RT_VERSION), string
  (RT_STRING).
- user32: `LoadMenuA`, `LoadStringA`, `LoadIconA` leen del .rsrc del
  proceso actual; `GetMenu`/`SetMenu` usan el menú cargado.

**Por qué**: desbloquea B5 (diálogos reales) y hace que cualquier .exe con
recursos cargue su menú/icono (hoy muchos piden LoadMenu/LoadString y
mueren o muestran menú vacío).

**Riesgo**: medio (formato de recursos del PE es anidado pero bien
documentado). Validar con un .exe mingw que embeba un menú y un
RT_STRING, y verificar que LoadMenuA/LoadStringA los devuelven.

**Test**: test_fase24p11.py — .exe mingw con recurso menú + string +
icono; verificar LoadMenuA (GetMenuItemCount), LoadStringA (texto),
LoadIconA (handle no-0); screendump del menú dibujado.

### P1.2. DialogBoxParamA real con RT_DIALOG

**Estado: COMPLETADO.** B5 ya tenía el parser RT_DIALOG + STATIC/BUTTON +
WM_INITDIALOG + EndDialog (basado en `menu_modal`). Este ítem añadió los
**controles EDIT**: foco en el primer EDIT, input de teclado
(caracteres + backspace, dibujo con cursor), y **GetDlgItemTextA /
SetDlgItemTextA / GetDlgItem** reales sobre los controles del diálogo.
También se **drena la cola de eventos al abrir** el diálogo (descarta las
teclas de la shell que lanzaron la app; sys_event devuelve 0=evento,
-1=vacío). Test `tools/test_fase24p12.py` (dlgtest2.exe: EDITTEXT + OK,
escribe 'abc', GetDlgItemTextA, EndDialog(IDOK)) 4/4 PASS. NOTA: los
tests inyectan teclas en minúscula (sendkey 'A' mayúscula no genera
EV_KEY).

**Tarea**: implementar `DialogBoxParamA` sobre el patrón de `menu_modal`
pero con el template RT_DIALOG del .rsrc (P1.1):
- Parsear el template RT_DIALOG (DLGTEMPLATE + controles DLGITEMTEMPLATE:
  STATIC/BUTTON/EDIT).
- El modal drena eventos, enruta WM_COMMAND/close al DlgProc.
- `WM_INITDIALOG`, controles dentro del modal, `EndDialog` sale con ID.

**Por qué**: abre la puerta a decenas de .exe con diálogos propios.

**Riesgo**: medio-alto (template binario del PE). Empezar con el parser de
template + un diálogo con 1 botón OK que devuelve IDOK (patrón test b5).

**Test**: test_fase24p12.py — .exe mingw con RT_DIALOG (1 botón OK);
verificar WM_INITDIALOG, EndDialog(IDOK), screendump del diálogo.

### P1.3. Registro stubs → .ini persistente

**Estado: COMPLETADO.** advapi32 (RegCreateKeyExA/RegOpenKeyExA/
RegSetValueExA/RegQueryValueExA/RegCloseKey + RegDeleteValueA) pasó de
stubs a un registro real en memoria sembrado desde `registry.ini` del
MEFS. `RegSetValueExA` vuelca el archivo (SYS_FCREATE/FWRITE/FLUSH),
`RegQueryValueExA` lee; formato `[clave]` + `name=sz:valor` /
`dword:n` / `bin:hex`. Claves por handle (hkey>=0x80000000 = raíz,
si no handle = índice+1, concatenación de subkey). Test
`tools/test_fase24p13.py` (regtest.exe, dos ejecuciones: round-trip en
memoria + persistencia entre procesos leyendo el .ini del disco) 5/5.
El .ini queda verificado en el disco (os-persist.bin).

**Tarea**: stubs reales en advapi32 que persisten en un archivo
`registry.ini` del FS (o memoria):
- `RegOpenKeyExA`, `RegCreateKeyExA`, `RegSetValueExA`, `RegQueryValueExA`,
  `RegCloseKey`, `RegDeleteValue`, `RegEnumValueA`.
- Clave → sección `[HKCU\\Software\\<app>]`, valor → `name=value`.
- Valores REG_SZ/REG_DWORD como texto.

**Por qué**: barato y elimina un punto de muerte masivo al arrancar apps.

**Riesgo**: bajo (los .exe que fallan sin registro solo necesitan que no
devuelva error fatal). Empezar con RegOpenKey/RegSetValue/RegQueryValue.

**Test**: test_fase24p13.py — app que escribe/lee una clave y verifica
round-trip + persistencia al reiniciar.

### P1.4. kernel32 de arranque (directorios + versión)

**Estado: COMPLETADO.** Añadidos en kernel32:
- `GetTempPathA`/`GetWindowsDirectoryA`/`GetSystemDirectoryA` → rutas
  virtuales fijas del FS (`C:\\TEMP`, `C:\\WINDOWS`,
  `C:\\WINDOWS\\System32`; la raíz MEFS se mapea a `C:\`).
- `GetVersionExA` → OSVERSIONINFOA 6.1 (Win7) + `GetVersion`.
- `GetFileInformationByHandle` → BY_HANDLE_FILE_INFORMATION con el
  tamaño del handle abierto (open_files), volumen "MYOS".
Test `tools/test_fase24p14.py` (bootpaths.exe) 6/6. NOTA: orden de args
de GetWindows/SystemDirectoryA es `(buf, n)` (buffer primero); GetTemp
es `(n, buf)`.

**Tarea**: añadir a kernel32:
- `GetTempPathA`/`GetWindowsDirectoryA`/`GetSystemDirectoryA` → rutas
  virtuales del FS (`C:\\` mapeado a la raíz MEFS).
- `GetVersionExA` → OSVERSIONINFOA (versión 6.1 para engañar a apps que
  comprueban la versión).
- `GetFileInformationByHandle` → info del archivo abierto.

**Por qué**: los .exe llaman estos al arrancar; sin ellos fallan o eligen
path de trabajo incorrecto.

**Riesgo**: bajo (son getters con valores fijos).

**Test**: test_fase24p14.py — app que imprime GetTempPath/GetWindowsDir/
GetVersionEx y verifica los valores en el serial.

## Bloque P2 — Siguiente bloque

### P2.1. comctl32: toolbar real + statusbar + trackbar + treeview

**Estado: COMPLETADO.** Añadidos en user32 (hijos virtuales, patrón del
ListView de C11):
- **toolbar** (ToolbarWindow32): TB_BUTTONSTRUCTSIZE/ADDBUTTONS/
  BUTTONCOUNT, botones con label, paint.
- **statusbar** (msctls_statusbar32): SB_SETPARTS/SETTEXT/GETTEXT
  (soporta parte ancho -1 = resto de la barra), paint por partes.
- **trackbar** (msctls_trackbar32): TBM_GETPOS/RANGEMIN/MAX/SETPOS/
  SETRANGEMIN/MAX/SETRANGE (MAKELONG min,max) + teclado flechas.
- **treeview** (SysTreeView32): TVM_INSERTITEMA/GETCOUNT/GETNEXTITEM/
  SELECTITEM/EXPAND, nodos con profundidad y caja +/-, teclado.
Test `tools/test_fase24p21.py` (ctldemo.exe, verifica los 4 por
SendMessageA) 6/6.
**Cambios de soporte**: FS_SECTORS 1760→2000 (el FS de arranque se
llenó con las apps nuevas; boot.asm y la capacidad del test a2 se
actualizaron) y checks del thumb del a2 hechos robustos al nº de
archivos (cada app nueva rompía el rango absoluto de Y).
NOTA: los bucles de paint de los controles deben acotarse a
`wnd_ch[parent]` (alto del cliente), no `cw` (ancho) — un hijo cerca
del borde inferior escribía fuera del buffer del padre (page fault).

### P2.2. Threads (CreateThread con scheduler leve)

**Estado: COMPLETADO.** CreateThread pasa de llamar a `fn` síncrono a
crear un **hilo real** que comparte el PD del proceso (multitarea
preemptiva del scheduler por timer):
- Syscall `SYS_THREADCREATE`/`SYS_THREADEXIT`; `task_create_thread`
  fabrica un task ring-3 con el mismo cr3, pila de usuario propia y
  EIP = fn; la pila se inicializa con `[ret]=kernel32._thread_ret`,
  `[+4]=param` (fn llamada como fn(param)).
- `ExitThread` → SYS_THREADEXIT. Si fn retorna, vuelve a `_thread_ret`
  (exportado) que llama ExitThread(0).
- El heap es compartido: `sched_user_heap*` derivan a la tarea principal
  del PD (is_thread=0); syscalls atómicos (gate deshabilita IF).
- `sched_kill_current`: un hilo muere sin liberar el PD (libera su pila
  de kernel); la tarea principal al morir mata sus hilos y libera el PD.
Test `tools/test_fase24p22.py` (thrtest.exe: hilo incrementa un contador
global, main busy-wait; solo retorna si el timer preempta) 4/4.

### P2.3. GDI blits (BitBlt/StretchBlt) + clipboard

**Estado: COMPLETADO.** gdi32: `BitBlt`/`StretchBlt` (SRCCOPY, entre DCs
de ventana y de memoria), `GetPixel` (devuelve el valor crudo del buffer
en formato px_disp), `CreateCompatibleDC`/`CreateCompatibleBitmap` +
`SelectObject(bitmap)` (memoria DC). user32: clipboard funcional
(OpenClipboard/EmptyClipboard/SetClipboardData/GetClipboardData/
CloseClipboard/IsClipboardFormatAvailable, CF_TEXT en un buffer global).
**FillRect**: vive en USER32 (Windows) pero la lógica (color de brushes
creados, DC de memoria de gdi32) está en gdi32 → user32 delega en
gdi32.FillRect (resuelto por SYS_DLLBASE+SYS_GETPROC y llamado stdcall).
msvcrt: añadidos `strcpy`/`strcmp` (antes el app crasheaba al llamarlos).
Test `tools/test_fase24p23.py` (blitclip.exe) 6/6.
NOTAS: `GetPixel` devuelve px_disp (no COLORREF) — los valores empíricos
son rojo=0x00FF0000, verde=0x0000FF00. `Rectangle` dibuja un borde con
el pen negro por defecto (muestrear el interior, no la esquina).
`FillRect` usa el brush pasado como argumento, no el del DC.

## Bloque P3 — UX / referencia

### P3.1. Desktop con iconos (taskbar ya existe)

**Estado: COMPLETADO.** `desktop.c` renderiza iconos en el escritorio
+ doble clic para lanzar:
- **Iconos desde el .rsrc**: `pe_icon_load` lee el `.exe` del MEFS
  (`sys_fsize`+`sys_dread`), camina el árbol tipo/id/lang y extrae el
  primer icono 32x32 (preferente 32bpp; soporta 4/8bpp con paleta +
  AND mask) del `RT_GROUP_ICON` → `RT_ICON`. NOTA FORMATO: los
  offsets de los DIRECTORIOS del árbol son relativos a la sección
  `.rsrc`, pero `OffsetToData` de los datos es un **RVA absoluto de
  imagen** (`img + raw_off + (val - dir_rva)`). Sin eso se lee fuera
  del buffer (PF). Fallbacks procedurales por tipo de app
  (carpeta/burbuja/pantalla).
- **Clic simple** → selección (resaltado + borde, blit por región con
  `sys_winupdate_rect`); **doble clic** (mismo icono, 2 UPs en <300 ms
  vía `SYS_TICKS 41` — nuevo syscall de `timer_get_ticks`, 100 Hz)
  → fork+exec. Los eventos inyectados (SYS_MOUSE_INJECT) recorren
  wm_route como reales.
- **Tests headless**: teclas `i`/`d` en el desktop inyectan clic
  simple/doble en el icono 1 (metapad); tras el lanzamiento el
  desktop inyecta clics periódicos en el botón X (676,43) cada 2.5 s
  (X_PERIOD=250 ticks, X_TRIES=14) porque el teclado ya va a metapad
  (foco). El monitor QEMU no inyecta PS/2 fiable.
- **3 bugs reales arreglados en el camino** (todos pre-existentes):
  (1) `ata_wait_ready` sin salida de éxito: cada escritura de disco
  giraba los 4 M de intentos (~0.7 s/sector) y el cierre de metapad
  (registro → FWRITE+FLUSH, decenas de sectores) congelaba el sistema
  minutos; (2) `DestroyWindow` no entregaba `WM_DESTROY` → las apps
  que cierran limpio nunca hacen PostQuitMessage y quedan vivas sin
  ventana (la shell no recupera el teclado); (3) `val_enc` (advapi32)
  escribía sin cota en `line[256]` → un REG_SZ sin NUL o un REG_BINARY
  de 128 B desbordaba la pila (eip=0x30303030).
Test `tools/test_fase24p31.py` **10/10 PASS** (iconos renderizados y
colores verificados en PPM — NOTA: el PPM de QEMU muestra el color
lógico 0x00RRGGBB como (G,B,R) para el path de wl_putpixel; el
bootscreen (sin swap) aparece como (B,G,R), ojo al comparar).

### P3.2. Explorer Win32 completo (listview ya validado en C11)

**Estado: COMPLETADO.** `explorer.exe` (Win32) sustituye al explorer ELF:
- **SysListView32** (comctl32) con columnas Nombre(300)/Tam(90); filas
  del MEFS vía SYS_DLISTDIR inline; subdirectorios con "/" en la col
  Tam (col 1 negativa = marcador, parse `neg ? (v>0 ? -v : -1) : v`).
- **Navegación**: ".." virtual al tope (si no es la raíz), Enter/`b`/
  doble clic en subdir → SYS_DLOOKUP+cd, `b` → SYS_DPARENT; `.exe/.elf`
  → fork+exec; resto → **visor** (GDI: GetDC/FillRect/TextOutA en el
  cliente del top-level, RichEdit NO — el listview se destruye y el
  padre pinta; `b` vuelve recreando el listview).
- **Clic**: WM_LBUTTONDOWN trae coords de PANTALLA (event_to_wm no
  convierte): fila = (y - win_top - TITLE - FRAME - CHILD_Y -
  LV_HDR)/ROW_H; LVM_SETSELECTIONMARK; doble clic = misma fila en
  <300 ms (SYS_TICKS 41, patrón P3.1).
- **Scroll del listview (user32)**: lv_scroll + auto-follow de la
  selección + Home/End/PgUp/PgDn + scrollbar visual (patrón A2). El
  listview además enruta `b`/`q`/`i`/`d`/`m` al padre como WM_COMMAND
  (id<<16)|2..6.
- **Bucle de mensajes con poll**: `PeekMessageA` (no bloquea) + chequeo
  de `pending_x` (clics en el X de la app lanzada cada 2.5 s, 14
  intentos) — con GetMessageA los inyectados nunca se procesan (el
  bucle se bloquea dentro de user32).
- **Teclas de test headless**: `d` = doble clic fila 0 (lanza
  hello.elf), `m` = lanzar el ÚLTIMO .exe de la lista (metapad) — no
  depende de la posición del FS; `i` = clic simple fila 0.
- **Lecciones del test** (tools/test_fase24p32.py, 15/15): (1) la tecla
  UP del listview es FLAKY (~1/2 registra) — el test NO depende de
  flechas para alcanzar filas concretas (End+Enter + hooks); (2)
  `waitstr` del log acumulado matchea marcadores ANTIGUOS (el exit:0
  del instalador) → esperar "exp: lanzando X" y luego buscar en
  acc[mark:]; (3) el PPM del visor es (B,G,R) de 0x00101020 = (32,16,16);
  (4) metapad tarda ~10 s en cargar — los clics del X necesitan
  margen; (5) el FS de la raíz varía según los residuos de tests
  previos (registry.ini de metapad) → `make persist_disk` en cada run.

### P4. Pulido visual + drivers (completado en un bloque)

**Fase 24-P4 (bugs visuales)**:
- **SYS_REDRAW_RECT 42** = `wm_redraw_rect` en coords de PANTALLA
  (restaura fondo + ventanas). Los diálogos modales (comdlg32,
  dialog_modal de user32, MessageBoxA) se dibujan directo al LFB y al
  cerrar dejaban pixeles — ahora repintan su rect.
- **Aceleradores**: el recurso RT_ACCELERATOR NO tiene DWORD count y
  usa entradas de 8 bytes {fFlags,wAnsi,wId,pad} (768 B = 96 en
  metapad). El parse con stride 6 rompía Ctrl+S (Guardar).
- **Barra de menú**: el flat que recibe el kernel iba con '&' crudo →
  los labels dibujados y el hit-test derivaban 8 px por menú (clic
  abría el popup equivocado). El flat ahora sale limpio (menu_label).
- **Botones de título**: X (rojo, glifo ×), minimizar (—) y
  maximizar/restaurar (□/❐) dibujados por el kernel; clic → ocultar
  (SYS_WINVIS 43) / full-screen toggle (rect guardado + blit acotado
  a saved_cw/ch) / EV_WINCLOSE. SYS_WINFIND 44 = id de ventana por
  pid (el taskbar del escritorio restaura minimizadas; hook 'r').

**Fase 24-P4.5 (drivers QEMU)**:
- **AC'97**: PCI clase 0x0401 → NAM/NABM; reset del codec en el
  registro RESET del NAM (offset **0x00**, no 0x7C); 48 kHz; DMA con
  BD de 8 B (len = halfwords, sin -1). Beep de arranque.
- **RTL8139** (10EC:8139): RX ring 8K+16 (RCR bits size en 11-12),
  TX con descriptores rotativos (el QEMU transmite el descriptor
  actual), ARP + ICMP contra 10.0.2.2 (user-net). El reply ICMP llega
  al netdev (filter-dump) pero la 2ª recepción del ring no entrega
  (quirk); el ARP hace el round trip completo.
- **REGLA DE ORO del DMA en QEMU**: sin el bit BUS MASTER (0x04 bit 2)
  del PCI command register, el DMA devuelve CEROS silenciosamente.
  Además los arrays estáticos de la BSS del kernel se leen como 0x00
  por DMA: usar kmalloc (heap) para los buffers de DMA.

## Orden sugerido de ejecución

1. P1.1 (recursos PE .rsrc) — desbloquea P1.2.
2. P1.2 (DialogBoxParamA con RT_DIALOG).
3. P1.3 (registro stubs).
4. P1.4 (kernel32 de arranque).
5. P2.1 (comctl32 controles).
6. P2.2 (threads).
7. P2.3 (GDI blits + clipboard).
8. P3.1 (desktop iconos).
9. P3.2 (explorer Win32).
10. P4 (pulido: diálogos/menús/botones) — COMPLETADO.
11. P4.5 (audio AC'97 + red RTL8139) — COMPLETADO.

Cada ítem termina con: compilación OK, test QEMU del ítem, regresión
completa, y un commit.

## Patrones de test (QEMU headless)

- Lanzar: `qemu-system-i386 -display none -monitor
  unix:/tmp/opencode/qmon.sock,server,nowait -serial stdio -no-reboot
  -no-shutdown -drive format=raw,file=build/os-persist.bin` (persist_disk).
- Los tests deben regenerar `build/os-persist.bin` con `make persist_disk`
  (os-image.bin está en la RAIZ, no en build/).
- `cancel_autoboot()`: esperar "Autoboot:" y enviar una tecla (el kernel
  la descarta).
- Esperar `exit:0` de la app anterior antes de lanzar la siguiente
  (`sched_user_busy`).
- Clics: inyectar EV_BUTTON_DOWN/UP sintéticos (SYS_MOUSE_INJECT); el
  monitor no inyecta PS/2 fiable.
- Screendumps: `screendump /tmp/opencode/x.ppm`; el PPM muestra los bytes
  del LFB (swap BGR — patrón `wl_px_disp`).
- Medir colores en el PPM con contador por muestreo (grid de 4 px).

## Checklist de commit

- [ ] `make -j$(nproc)` limpio (solo el warning RWX del linker).
- [ ] `make persist_disk` OK.
- [ ] Test del ítem (tools/test_fase24pNN.py) PASS.
- [ ] Regresión completa: 20a 8/8, 20b 3/3, 20c 7/7, 20d 6/6, faseE 6/6,
      fase22 7/7 + fase23 completo (108 checks).
- [ ] README.md y esta skill actualizados.
- [ ] Un solo commit con mensaje descriptivo del ítem.

Base directory: .opencode/skills/myos-fase24-roadmap