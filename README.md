# MyOS - Sistema operativo didáctico (x86)

Kernel propio desde cero para PC x86 (IA-32): bootloader en NASM, kernel en
C freestanding, estándar + multitarea, filesystem propio y modo usuario.
Incluye un cebo: **ejecutar `.exe` de Windows** (PE32) con el CRT real de
mingw-w64 en ring 3, gracias a shims de `kernel32.dll` y `msvcrt.dll`.

## Qué hay implementado

| Fase | Contenido |
|------|-----------|
| 0-1  | Toolchain i386, bootloader (LBA, A20, GDT, modo protegido) |
| 2    | Kernel C freestanding, drivers VGA/serial, mini-libc |
| 3    | IDT, ISR/IRQ, PIC 8259, PIT (tick), teclado, kpanic |
| 4    | PMM (E820 + bitmap), paginación (PSE 4 MiB + 4 KiB), heap |
| 5    | Multitarea preemptiva (scheduler round-robin) |
| 6    | Filesystem MEFS, userland ring 3, syscalls `int 0x80`, shell |
| 7    | Arranque por CD (ISO9660 + El Torito), boot_info, pruebas QEMU/gdb |
| 8    | PE32 (.exe), modulos Win32 fixed ring 3, run dual MZ/ELF |
| 9    | **`.exe` mingw real**: imports PE estándar + shims kernel32/msvcrt → `run hello_win.exe` imprime y devuelve `exit:42` |
| 10   | **APIs de fichero Win32**: syscalls DREAD/DLIST, `CreateFileA`/`ReadFile`/`FindFirstFileA` en kernel32 + `dir.exe` real incluido en el ISO |
| 11   | **APIs de proceso Win32**: `SYS_SELFNAME` (exe_name por tarea), `GetCurrentProcessId`/`GetModuleFileNameA` reales, **`GetCommandLineA` real (argc/argv de la línea de `run`)** + `proc.exe` real incluido en el ISO. Escalera de compatibilidad 11/11 |
| 12   | **GUI: VBE 800x600x32** (dispi + LFB del BAR0 PCI, consola gráfica `vgafx`) y `MessageBoxA` real dibujando una ventana con botón OK |
| 13   | **Ratón PS/2 (IRQ12) + cursor** en el framebuffer (driver 8042, paquete de 3 bytes, save/restore) |
| 14   | **Syscalls de eventos gráficos** (`SYS_MOUSEINFO` 16, `SYS_EVENT` 17, cola FIFO global) y **primer widget interactivo**: el botón OK de `MessageBoxA` responde al clic (estado presionado) |
| 15   | **Widgets interactivos en user32**: mini-API de widgets (`MyOS_PollEvent`/`MyOS_DrawButton`/`MyOS_WidgetHit`/`MyOS_ButtonFeed`) con botones con hover y press, lista para el escritorio |
| 16   | **Gestor de ventanas en el kernel** (winmgr): `SYS_WINCREATE` 18-`SYS_WININFO` 22, composición centralizada con marco/título/botón X, z-order con snapshot del fondo, arrastre por la barra de título |
| 17   | **Escritorio completo + enrutado por PD + limpieza al morir**: colas FIFO por app (`wm_route`), `EV_KEY` → ventana superior, clic/drag → dueño bajo el cursor; `wm_cleanup_pd` al morir la tarea; **escritorio funcional** (`desktop.c`: wallpaper, taskbar, botón EXPLORADOR; `explorer.c`: lista MEFS, selección, visor de texto; `win_two.c`: visor); **fixes críticos**: TLB flush en `exec`, IRQ1 teclado, z-order del WM, blit cliente corregido, FS truncado (512→560 sectores). Escalera **14/14 PASS** (disco e ISO). |
| 21   | **Escritorio lanzador + explorador con subdirectorios**: `desktop.c` con taskbar multi-botón (EXPLORADOR/METAPAD/MENSAJE/DEMO) que hace fork+exec de apps **Win32 reales (.exe)** y ELFs nativos (teclas 1-4 o clic); `explorer.c` navegando subdirectorios MEFS (`SYS_DLISTDIR` 33/`SYS_DPARENT` 34/`SYS_DLOOKUP` 35, `cd` con Enter, `..` con b, dirs con `/`), abre `.exe`/`.elf` (fork+exec) y `.txt` (visor); **fix**: `sys_winupdate` con `ecx=0` explícito (basura en ecx hacia fallar el blit y dejaba la ventana en negro). Validado en QEMU headless: desktop compone wallpaper+taskbar, explorer lanza y navega, metapad.exe abre desde el escritorio. |
| 23   | **Fase 23-A1: renderizado por región** (inicio Fase 23): se extrajo `wm_redraw_rect(rx,ry,rw,rh)` (restaura el rect del fondo + repinta las ventanas que lo intersectan en orden z + fijas) y se usa en `wm_create`, `wm_close`, `wm_cleanup_pd`, `wm_move`, el drag de `wm_route`, `wm_set_title` (solo la franja del título) y `wm_set_menu`/`wm_set_toolbar` (rect de la ventana) — el único `wm_compose()` completo restante es la restauración de la consola al quedar sin ventanas. **Sin parpadeo al crear/cerrar/mover ventanas** (screendumps en ráfaga estables). Regresión: 20a 8/8, 20b 3/3, 20c 7/7, 20d 6/6, faseE 6/6, fase22 7/7. Roadmap completo de la Fase 23 (A: estabilidad → B: superficie Win32 → C: datos) en `.opencode/skills/myos-fase23-roadmap/`. |
| 23   | **Fase 23-A2: scrollbar del explorer + Home/End/PgUp/PgDn**: franja de 12 px a la derecha del cliente (solo si hay más entradas que filas visibles) con thumb proporcional (`sb_thumb`: th = track·vis/nfiles, ty según scroll_off); clic en el thumb salta proporcional, encima/debajo = PgUp/PgDn; el hit de selección de filas excluye la franja. Teclas `VK_SPECIAL_*` 0x104-0x107 (Home/End/PgUp/PgDn) mueven `sel` al inicio/fin/página. `tools/makefs.py` ampliado con `--dir NAME:p1,p2,...` para generar subdirectorios en la imagen (parent = índice, formato del kernel). Test `tools/test_fase23a2.py` **9/9 PASS** (thumb visible en la raíz, End baja/Home sube, navegación a subdir de 35 entradas, 25×down, PgDn/PgUp). Regresión completa PASS. |
| 23   | **Fase 23-A3: foco/eventos entre apps + fix del raise**: bug real arreglado — el `wm_raise` del clic cambiaba el z-order sin repintar, así que la ventana subida quedaba oculta por la de encima; ahora `wm_raise` + `wm_redraw_rect`. Se añadió `SYS_MOUSE_INJECT 36` (inyección sintética de ratón, ya que el monitor de QEMU no inyecta PS/2 fiable en headless), `mouse_set_pos`/`mouse_event_push` públicos, y `user/inject.c` (`inject.elf`) que inyecta clics con marcadores serial. Test `tools/test_fase23a3.py` **5/5 PASS**: teclas→topmost (metapad), clic en explorer tapado lo sube y recibe teclas, vuelta a la consola limpia. Regresión completa PASS. (Nota: el metapad bajo el explorer se renderiza como fondo — bug pre-existente del RichEdit, no de enrutado.) |
| 23   | **Fase 23-A4: instalador tipo Windows**: `user/installer.c` (`installer.elf`) crea `installed/` (subdir persistente), copia `readme.txt` → `installed/readme_inst.txt` (SYS_DREAD + FCREATE_IN + FWRITE) y escribe `version.txt`, con `flush()` al final. Para ello se añadieron `mefs_create_in(parent,name)` al FS, los syscalls `SYS_MKDIR 37` y `SYS_FCREATE_IN 38`, y wrappers en winlib.h. Test `tools/test_fase23a4.py` **5/5 PASS**: el instalador persiste; desde el HOST se parsea el disco (MEFS LBA 129) y se verifica que installed/ es un dir con readme_inst.txt == readme.txt y version.txt; tras REBOOT el explorer navega a installed/ y lo lista. Regresión completa PASS. |
| 24   | **Fase 24-P3.1: escritorio con iconos + doble clic**: el `desktop.c` extrae el primer icono 32x32 (preferentemente 32bpp) del `.rsrc` de cada `.exe` (`RT_GROUP_ICON` → `RT_ICON`, lee el archivo del MEFS con `sys_dread` y camina el árbol tipo/id/lang; los offsets de los datos de recursos son RVAs absolutos, los de los directorios relativos a la sección) y dibuja iconos procedurales de reserva (carpeta/burbuja/pantalla). Los iconos se renderizan en el wallpaper con etiqueta; **un clic selecciona** (resaltado + borde, blit por región `sys_winupdate_rect`) y **doble clic lanza** la app (ventana de 300 ms vía `SYS_TICKS 41`, nuevo syscall que expone `timer_get_ticks`). **3 bugs reales arreglados en el camino**: (1) `ata_wait_ready` no tenía salida de éxito — cada escritura de disco giraba los 4 M de intentos (~0.7 s/sector) y el cierre de metapad (registro → FLUSH, decenas de sectores) congelaba el sistema; (2) `DestroyWindow` no entregaba `WM_DESTROY` — las apps que cierran limpio nunca hacían PostQuitMessage y quedaban vivas sin ventana; (3) `val_enc` (advapi32) escribía sin cota en `line[256]` — un REG_SZ sin NUL o un REG_BINARY de 128 B desbordaban la pila (eip=0x30303030). Test `tools/test_fase24p31.py` **10/10 PASS** (iconos renderizados, selección, doble clic lanza metapad, cierre con el X vía inyección periódica del propio escritorio, q→shell). Regresión completa PASS (20a-d, faseE, fase22, fase23 completo, fase24 p11-p23). |
| 24   | **Fase 24-P3.2: explorer Win32 con SysListView32**: el `explorer.exe` (real .exe mingw) sustituye al explorer ELF: ventana top-level con un **SysListView32** (comctl32) de columnas Nombre/Tam sobre el MEFS (SYS_DLISTDIR inline), subdirectorios con "/" en la col Tam (col 1 negativa), entrada virtual ".." para subir, **Enter o doble clic** (misma fila en <300 ms vía SYS_TICKS 41, selección con LVM_SETSELECTIONMARK; WM_LBUTTONDOWN trae coords de PANTALLA) navega/lanza (.exe/.elf con fork+exec)/ve — **visor de texto** pintado con GDI (GetDC/FillRect/TextOutA, el listview se destruye y `b` lo recrea). El **listview de user32** gana scroll: lv_scroll con auto-follow de la selección, Home/End/PgUp/PgDn y scrollbar visual (patrón A2), y enruta `b`/`q`/`i`/`d`/`m` como WM_COMMAND (subir/cerrar/hooks de test: `d` doble clic fila 0, `m` lanza el último .exe). Bucle de mensajes con **PeekMessageA** + poll del botón X de la app lanzada (inyección periódica cada 2.5 s, como P3.1). Test `tools/test_fase24p32.py` **15/15 PASS** (listado con scrollbar, doble clic lanza hello.elf, cd a installed/, visor con fondo oscuro, vuelta a la lista y subida con b, lanzamiento y cierre de metapad con el X, q→exit:0). Regresión completa PASS (20a-d, faseE, fase22, fase23 completo, fase24 p11-p23). |
| 24   | **Fase 24-P4: pulido Win32 — diálogos repintan, aceleradores, menús y botones de título**: (1) **SYS_REDRAW_RECT 42** (`wm_redraw_rect` en coords de pantalla): los diálogos modales (comdlg32, DialogBoxParamA, MessageBoxA) se dibujan directo al LFB y al cerrar dejaban pixeles hasta mover la ventana — ahora el rect se restaura (fondo + ventanas). (2) **Aceleradores corregidos**: el recurso RT_ACCELERATOR se parseaba con stride de 6 bytes (el real: entradas de 8 bytes sin count; 768 B = 96 en metapad) — Ctrl+S no abría Guardar. (3) **Barra de menú**: los labels top-level iban con '&' crudo (el kernel los dibujaba y medía con el '&') mientras el hit-test usaba el limpio — derivaban 8 px por menú y el clic abría el popup equivocado; ahora el flat va limpio. (4) **Tres botones de título** (X rojo + minimizar + maximizar/restaurar con glifos): clic en el título maximiza a pantalla completa (rect guardado + blit del cliente acotado a saved_cw/ch) y restaura; minimizar oculta la ventana (SYS_WINVIS 43) y el taskbar del escritorio la restaura (SYS_WINFIND 44 por pid). Test `tools/test_fase24p4.py` **16/16 PASS**. |
| 24   | **Fase 24-P4.5: drivers de audio AC'97 y red RTL8139**: (1) **AC'97** (QEMU -device AC97): PCI scan clase 0x0401 → BARs (NAM/NABM), reset del codec (registro RESET = offset 0x00 del NAM), 48 kHz + sin mute, y reproducción por DMA del bus master con descriptor de 8 B (len = halfwords). Beep de arranque (440 Hz, 150 ms) valida codec+DMA. (2) **RTL8139** (10EC:8139): init con RX ring de 8 KB+16 (RCR), TX por descriptores rotativos, y **ping al gateway del user-net**: ARP (request broadcast + reply parseado → MAC de 10.0.2.2) + ICMP echo request (64 B de datos, checksums válidos); el reply llega al netdev (verificado con filter-dump) pero el ring no lo entrega en la 2ª recepción (quirk de QEMU documentado). **Lección clave**: sin el bit BUS MASTER del PCI command register el DMA de QEMU devuelve **ceros silenciosamente** (los buffers del kernel en la BSS se leen como 0x00; el heap funciona). Tests `tools/test_fase24p45.py` **8/8 PASS** (codec + beep + ARP end-to-end + pcap con 4 paquetes válidos). |
| 25   | **Fase 25-W2A paso 2: thunks W→A de kernel32 (~45) con tabla UTF-16→cp437**: para correr .exe reales de Microsoft (MSVC, APIs Unicode). **Conversión**: `cp437_map[]` (78 entradas Unicode→cp437: Latin-1, griego, símbolos) + `utf16_to_cp437`/`cp437_to_utf16` (todo aritmética 32-bit, sin libgcc en ring 3). **Thunks de entrada** (W→A→llamada): CreateFileW, FindFirstFileW/FindNextFileW (con WIN32_FIND_DATAW), GetFileAttributesW, SetFileAttributesW, DeleteFileW, MoveFileW, CopyFileW, CreateDirectoryW, RemoveDirectoryW, GetEnvironmentVariableW, SetEnvironmentVariableW, GetTempPathW, GetWindowsDirectoryW, GetSystemDirectoryW, GetFullPathNameW (con filepart), GetModuleHandleW, GetVersionExW, GetVolumeInformationW, GetLogicalDriveStringsW, GetPrivateProfileStringW, WritePrivateProfileStringW, GetDateFormatW, GetTimeFormatW, GetLocaleInfoW, FormatMessageW. **Salida**: A→UTF-16 en el buffer del caller. **lstr\*W** directos sobre UTF-16 (lstrlenW/lstrcpyW/lstrcpynW/lstrcatW/lstrcmpW/lstrcmpiW). **A complementarias nuevas**: GetEnvironmentVariableA (busca en el bloque PATH/HOME), SetEnvironmentVariableA, DeleteFileA, MoveFileA/CopyFileA (read+write+delete reales por MEFS), CreateDirectoryA (SYS_MKDIR), RemoveDirectoryA, GetLogicalDriveStringsA ("C:\\"); GetCurrentDirectoryA ahora devuelve "C:\\MyOS" (antes vacío). **BUG REAL DEL KERNEL ARREGLADO**: `.rel.data` se perdía — `parse_exports` fusionaba `.rel.data` y `.rel.exports` en el mismo par de offsets (el último ganaba), así que los punteros inicializados de `.data` (p.ej. `env_block[]`) nunca se reubicaban con el delta 0x200000 y apuntaban a páginas sin mapear: el primer dereferenciado (GetEnvironmentVariableA) daba #PF. Ahora `.rel.exports` tiene offsets propios (`rel_exports_off/size`) y `apply_relocs` los procesa por separado. Test 19/19 PASS (paso 2) dentro de `tools/test_fase25w2a1.py` (archivos W con nombre cp437 "café.txt", find W, paths W, envW, lstr\*W, copy/move/mkdir/rmdir W, fmt/date/locale W). |
| 25   | **Fase 25-W2A paso 3: thunks W→A de user32 (~38) + msvcrt W**: RegisterClassW (copia el WNDCLASS a 40 B y convierte lpszClassName/lpszMenuName), CreateWindowExW, SetWindowTextW/GetWindowTextW/GetWindowTextLengthW, MessageBoxW, LoadStringW, SendMessageW (convierte WM_SETTEXT/WM_GETTEXT), GetClassNameA/W (nuevo: almacena la clase de cada hijo en `child_class[]`), CharNextA/W, CharUpperA/W, CharLowerW, CharUpperBuffW/CharLowerBuffW, LoadMenuW/LoadIconW/LoadCursorW/LoadAcceleratorsW, GetWindowLongW/SetWindowLongW/SetClassLongW, RegisterWindowMessageW, GetDlgItemTextW/SetDlgItemTextW, DialogBoxParamW/CreateDialogParamW, y alias sin cadenas (GetMessageW/PeekMessageW/PostMessageW/DispatchMessageW/DefWindowProcW/SendDlgItemMessageW/IsDialogMessageW/TranslateAcceleratorW). **msvcrt W**: `_wgetmainargs` (+`__wgetmainargs` VC6 y `_getmainargs` alias — argv wchar_t desde el TIB), wcscpy/wcscat/wcscmp/wcsncmp/wcsncpy/wcschr/wcsrchr/wcsstr, _wcsicmp/_wcsnicmp/_wcslwr/_wcsupr/_wtoi/_wtol/_itow, _wfopen (stub con log). **3 bugs reales de user32 arreglados**: (1) `RegisterClassA` guardaba el puntero del nombre del caller — RegisterClassW lo pasaba desde la pila y el nombre quedaba colgando; ahora se copia a un pool estático por clase; (2) `class_find` comparaba punteros en vez de contenido; (3) `builtin_wndproc` leía WM_SETTEXT/WM_GETTEXT del parámetro equivocado (el texto va en lParam, no en wParam) — nunca se había notado porque el texto se escribía directo por SetWindowTextA. **FS ampliado a 2400 sectores** (1.2 MB): user32.elf creció a 73 KB y el bitmap de 2000 sectores desbordaba. Test `tools/test_fase25w2a1.py` **24/24 PASS** (clase W + ventana + EDIT con texto W por Set/Send, GetClassNameW, Char*W, msvcrt W completo, _wgetmainargs). Regresión completa PASS. Próximo: paso 4 — probar con el primer binario MSVC real (notepad.exe Win98/2000 o resource kit). |
| 22   | **Arranque tipo Windows**: pantalla de carga (`bootscreen.c`) con barra de progreso y fases (drivers → memoria → paginación → interrupciones → FS → DLLs Win32 → multitarea → shell) dibujada al LFB; **pulido**: durante la animación `kprint` solo escribe al serial (el log del boot no pisa la pantalla de carga, el serial lo conserva para debug/tests) y se eliminaron los demos del boot (PIT 1 s, ventana de teclado 3 s, tareas T-A/T-B). **Autoboot del escritorio** — la shell espera 3 s (cualquier tecla cancela y va a la consola) y si no llega entrada lanza `desktop.elf` solo, quedando la shell viva por debajo (al cerrar el desktop con 'q' el WM restaura la consola y el prompt vuelve); **flag persistido `bootgui on|off`** en el superbloque MEFS (offset 36, se persiste con `flush`); **fix**: `keyboard_flush()` al volver a la consola (la tecla que cerró la última ventana no contaminaba la shell) y **fix del scheduler**: `sched_kill_current` deja `current` apuntando a la tarea muerta y `sched_tick` con `task_count<2` no rotaba — sin las tareas demo la CPU se quedaba en el bucle `task_stub_exit` del desktop muerto y la shell nunca volvía; ahora el tick rota si `current` está `TASK_FREE`. Tests actualizados con cancelación del autoboot; `test_fase22.py` **7/7 PASS** (barra visible y limpia, autoboot, q→shell, bootgui off persistido, prompt directo). |

## Requisitos

- `gcc -m32` / `ld` / `nasm` / `qemu-system-i386` / `python3` (host Linux)
- `i686-w64-mingw32-gcc` (para generar los `.exe` de prueba de las Fases 9-11)
- `xorriso` (solo target `iso`/`test_cd`)

## Compilar y probar

```sh
make os-image.bin -j4   # boot + kernel + fs.bin (MEFS, 19 archivos)
make run                # QEMU con disco raw (ventana + teclado)
make test               # headless: consola por serial (shell por COM1)
make iso && make test_cd
make win_hello          # compila los exe de las Fases 9-11 e imprime sus imports
```

Prueba de la Fase 9 (headless, shell por serial):

```sh
timeout 40 sh -c '(sleep 6; printf "run hello_win.exe\n"; sleep 5) | \
  qemu-system-i386 -display none -monitor none -serial stdio -no-reboot \
  -drive format=raw,file=os-image.bin'
```

Salida esperada: `Hello from a REAL Windows-CRT exe!`, `argc = 1`,
`malloc: 10 bytes = 'heap works'`, `bye` y `exit:42`.

Prueba de la Fase 10 (APIs de fichero de Win32 dentro del ISO):

```sh
timeout 40 sh -c '(sleep 6; printf "run dir.exe\n"; sleep 5) | \
  qemu-system-i386 -display none -monitor none -serial stdio -no-reboot \
  -drive format=raw,file=os-image.bin'
```

Salida esperada: lista de los 19 archivos del MEFS (con `FindFirstFileA`)
y el contenido de `readme.txt` (leído con `CreateFileA` + `ReadFile`),
terminando en `exit:0`.

Prueba de la Fase 11 (APIs de proceso de Win32):

```sh
timeout 40 sh -c '(sleep 6; printf "run proc.exe\n"; sleep 5) | \
  qemu-system-i386 -display none -monitor none -serial stdio -no-reboot \
  -drive format=raw,file=os-image.bin'
```

Salida esperada: `proc: GetCurrentProcessId = 3`, `proc: GetModuleFileNameA
= 'proc.exe' (8 chars)`, `proc: argc = 1`, `proc: argv[0] = 'proc.exe'` y
`exit:7`. Con argumentos (`run proc.exe uno "dos tres" 4`) imprime
`argc = 4` y `argv[2] = 'dos tres'`.

## Estructura

```
boot/            bootloader NASM (sector 0) + arranque por CD
kernel/          núcleo: entrada, IDT, PMM, paginación, multitarea,
                 syscalls, shell, MEFS, drivers (VGA/serial/PIT/PS2/ATA)
kernel/pe.c      loader PE32: cabeceraMZ, secciones, imports PE estándar
kernel/win32.c   modulos Win32 fixed (kernel32/user32/ntdll/msvcrt),
                 TIB del CRT (FS), resolución case-insensitive
user/            programas de usuario (ELF y .exe myos)
user/win32/      shims de DLLs: kernel32.dll, user32.dll, ntdll.dll,
                 msvcrt.dll, dir.exe (listado FS vía API Win32) y
                 hello_win.exe (CRT mingw real)
tools/           makepe.py (ELF → PE MyOS), makeiso.py, makefs.py
docs/            documentación técnica formal por subsistema
DESIGN.md        decisiones de diseño y bitácora por fases (leer primero)
```

Documentación técnica completa por subsistema: **`docs/`** — arquitectura,
arranque, memoria, multitarea, filesystem, syscalls, drivers, GUI/window
manager, capa Win32, y la evaluación de correr apps Windows GUI reales.

## Cómo funciona lo de los .exe (Fase 8-9)

1. **Solo lectura, nada de Windows**: el loader PE32 (`kernel/pe.c`)
   valida la cabecera MZ/PE, mapea en espacio USER una página por la
   cabecera, las secciones y las bases. Los imports se resuelven contra
   módulos fijados **dentro** del propio MyOS.
2. **DLLs fijadas en ring 3**: `kernel32.dll`, `user32.dll`, `ntdll.dll`
   y `msvcrt.dll` son ELF32 enlazados a bases fijas
   (0xB0000000...0xB3000000, `tools/dll32.ld`) con una tabla `.exports`
   (nombre → VA). `win32_map_all()` los mapea en el PD de cada tarea.
3. **Imports estándar**: `pe_resolve_imports_std` camina
   `IMAGE_IMPORT_DESCRIPTOR` + FIRST thunk (IAT) del `.exe`, y escribe
   la VA de cada export (case-insensitive) en el slot de la IAT. El formato
   histórico `.idata` de makepe.py queda como fallback.
4. **CRT de mingw**: la Fase 9 se ocupa de lo que exige un exe
   compilado con la toolchain real: el TIB (`%fs:0x18` via GDT 0x33),
   `__getmainargs`/`_initterm`/`atexit`/media (shim msvcrt), el
   va_list `char*` de MSVCRT, `malloc` vía SYS_MALLOC, y el retorno
   final (`_crt_ret` escribe `main()`→`exit()`, verificado `exit:42`).

Todos los detalles, decisiones y bugs encontrados están en `DESIGN.md`.

## Pruebas

- `make test` / `make test_cd`: QEMU headless, shell por serial,
  ejecuta `run` + `hello.elf/winapi/hello_win`... y verifica ausencia de
  panic/#PF.
- Scheduler de demo T-A/T-B desactiva el demo impreso al entrar en el
  shell: se activa con el syscall de debug de kmain si quieres.

## Estado

- Fase 9 completada: `hello_win.exe` (CRT mingw) corre end-to-end y
  devuelve `exit:42`; todos los tests de regresión (ELF y PE MyOS) pasan.
- Fase 10 completada: `dir.exe` (mingw real) lista el FS y lee un
  archivo con `FindFirstFileA`/`CreateFileA`/`ReadFile`; ejecutable y
  `readme.txt` van dentro del ISO.
- Fase 11 completada: `proc.exe` (mingw real) obtiene su PID real y su
  nombre con `GetCurrentProcessId`/`GetModuleFileNameA` (SYS_SELFNAME);
  línea de comandos real (`GetCommandLineA` → `argc/argv` con la línea
  de `run`, quoting incluido); `exit:7` viaja por el CRT. Escalera de
  compatibilidad **11/11 PASS** (disco y CD): hello.elf, quick.exe,
  winapi.exe, hello_win.exe, dir.exe, fork.exe, exec.exe, console.exe,
  input interactivo, proc.exe, proc.exe con argumentos.
- Fase 12 completada: **modo gráfico VBE 800x600x32** (dispi + LFB leído
  del BAR0 PCI, consola del kernel dibujando en el framebuffer) y
  `messagebox.exe` (mingw real) abriendo una ventana gráfica real
  (`MessageBoxA` contra user32.dll) con el botón OK. Escalera de
  compatibilidad **13/13 PASS** (disco y CD).
- Fase 13 completada: **ratón PS/2 (IRQ12)** con cursor en el framebuffer
  (driver 8042, paquete de 3 bytes, save/restore del cursor; validado con
  `mouse_move` del monitor QEMU + screendump).
- Fase 14 completada: **syscalls de eventos gráficos** — cola FIFO global
  de eventos de ratón y teclado (`SYS_MOUSEINFO` 16 / `SYS_EVENT` 17) y
  `MessageBoxA` con el botón OK **clickeable** (hit-testing + estado
  presionado; cierre con clic o Enter). Escalera de compatibilidad
  **14/14 PASS** (disco).
- Fase 15 completada: **widgets interactivos en user32** — mini-API de
  botones (`MyOS_PollEvent`/`MyOS_DrawButton`/`MyOS_WidgetHit`/
  `MyOS_ButtonFeed`) con hover y estado presionado, validada por
  screendump en los tres estados; `MessageBoxA` refactorizado sobre ella.
  Escalera **14/14 PASS** (disco).
- Fase 16 completada: **gestor de ventanas en el kernel** — `SYS_WINCREATE`
  (18)/`SYS_WINCLOSE` (19)/`SYS_WINMOVE` (20)/`SYS_WINUPDATE` (21)/
  `SYS_WININFO` (22); composición centralizada (marco, título y botón X
  del kernel + área cliente de la app en ring 3), z-order con snapshot del
  fondo, arrastre por la barra de título (consumido por el WM) y botón X
  entregado como `EV_WINCLOSE` 5. `win_demo.c` validado con QMP
  (screendumps: superposición inicial, ventana arrastrada, ventana
  cerrada). Escalera **14/14 PASS** (disco).
- Fase 17 completada: **entorno de escritorio funcional** —
  `desktop.c` (wallpaper + taskbar con botón EXPLORADOR), `explorer.c`
  (lista MEFS con `SYS_DLIST`, selección por clic, Enter abre visor de
  texto `win_two.c`), `winlib.h` (API compartida). **Enrutado de eventos
  por PD** — `wm_route` enruta `EV_KEY` a la ventana superior, clic/drag
  al dueño bajo el cursor; `SYS_EVENT` reclama solo su cola; `wm_cleanup_pd`
  libera ventanas y colas al morir la tarea. **5 bugs críticos corregidos**:
  (1) TLB flush en `sys_exec`, (2) IRQ1 teclado habilitada, (3) z-order
  del WM (raise + excluir BG), (4) blit cliente con offset correcto,
  (5) FS truncado (`FS_SECTORS=512→560`, PMM reserva extendida).
  **Validación completa**: `desktop_test.py` **OK 1-9 + DONE**,
  `ladder_test.py` **14/14 PASS** (incluye messagebox GUI),
   `f17_test.py` **OK 1-5 + DONE** (fork + 2 procesos × 2 ventanas,
   limpieza total). **Arranque por ISO (El Torito) validado: 14/14 PASS**.
- Fase 18 completada: **GDI de dibujo + metapad.exe 100% cargado** —
  `gdi32.dll` con DC de dibujo real (buffer del cliente de user32,
  `TextOutA`/`FillRect`/`Rectangle`/`LineTo` (Bresenham)/`PatBlt`, objetos
  GDI stock+creados, `GetTextMetricsA`/`GetDeviceCaps`, stubs de impresión)
  y **3 módulos nuevos** (`comctl32.dll` con `CreateToolbarEx` fake y
  `InitCommonControls` por ordinal 8, `comdlg32.dll`/`advapi32.dll`/
  `shell32.dll`), región Win32 de 5 a 9 DLLs. **`pe.c` reubica PE con
  ImageBase baja** (0x00400000 de metapad) aplicando la tabla `.reloc` a
  0x81000000; **teclado set 1 completo** (shift/caps/símbolos, Ctrl/Alt en
  el evento, teclas VK especiales). `gdidemo.exe` (prueba) y **metapad.exe
  contento con los imports de sus 8 DLLs**: ventana + menú (146 items) +
  control RichEdit, sin crash. **Validado por screendump + conteo de
  píxeles** (rectángulo azul exacto, línea roja diagonal, fondo de texto
  opaco en gdidemo; cliente blanco de metapad) y **escalera 14/14 PASS**
  en disco y CD.
- Fase A concluida: **edición real en metapad.exe** — `WM_CHAR`/`WM_KEYDOWN`
  se enrutan al control RichEdit enfocado (`SetFocus`/`GetFocus` reales,
  foco inicial en el primer editor), el wndproc builtin edita `child_text`
  (inserción/borrado en el caret: backspace, Enter, flechas, Home/End,
  Supr, Up/Down entre líneas) y repinta el hijo en el buffer del padre con
  **caret visible** y salto de línea en `'\n'`. Validado en QEMU: teclear
  `hola<enter>mundo<backspace><enter>xyz` dibuja el texto (blanco/negro)
  en el cliente y el caret/edición funcionan (screendumps + diff de
  píxeles + verificación interactiva).
- Fase B concluida: **abrir archivo por línea de comandos** — `run
  metapad.exe readme.txt` carga y muestra el archivo en el editor sin
  interacción: el entry pasa lpCmdLine a WinMain, que copia el nombre a su
  buffer global, `GetFullPathNameA`/`FindFirstFileA` derivan el directorio,
  y `CreateThread` (ahora ejecuta el cuerpo síncrono, antes era stub)
  lanza el worker de apertura: `CreateFileA`+`GetFileAttributesA`+`ReadFile`
  y `SetWindowTextA` al RichEdit. `MessageBoxA` implementa `MB_YESNO`
  (Si/No, y/n, IDYES=6/IDNO=7) y `SendMessageA` acepta
  `EM_SETPARAFORMAT`/`EM_SETTEXTEX` al hijo (evita el MessageBox "Couldn't
  set para format."). Validado en QEMU: serial muestra el flujo completo y
  el screendump deja ver las 4 líneas de readme.txt en el cliente blanco;
  regresión Fase A OK y escalera 14/14 en disco y CD.
- Fase C concluida: **diálogo Abrir real (`GetOpenFileNameA`)** — `Ctrl+O`
  en metapad (acelerador → `TranslateAcceleratorA` corregido: comparación
  sin mayúsculas, verificación de Ctrl y WM_COMMAND a la ventana principal)
  abre una ventana propia con la **lista de archivos del MEFS** (SYS_DLIST),
  filtro por extensiones (`lpstrFilter`+`nFilterIndex`), campo de nombre con
  **autocompletado por prefijo** (teclear "readme" selecciona readme.txt),
  navegación por teclado (flechas/PgUp/PgDn/Home/End/Enter/Esc), clic en
  filas y botones Abrir/Cancelar. Al confirmar devuelve el nombre y metapad
  reutiliza el flujo de apertura de la Fase B. `FS_SECTORS` 1100→1130 (el
  comdlg32.elf más grande dejaba metapad.exe cortado en el disco). Validado
  en QEMU: Ctrl+O → diálogo → "readme" → Enter → las 4 líneas de readme.txt
  en el editor; regresión Fase A OK y escalera 14/14 en disco.
- Fase D concluida: **menú de barra + desplegables** — la ventana de metapad
  muestra la **barra de menú** (File Edit Favourites Options Help) dibujada por
  el winmgr del kernel (`SYS_MENUBAR`), y clic o `Alt+letra` despliegan un
  **menú modal** navegable por teclado (flechas/Home/End/PgUp/PgDn/Enter/Esc/
  mnemónico) y por ratón (hit-testing + highlight) que envía `WM_COMMAND` a
  los handlers existentes (Open 0x9c43, New, Save, Exit...). **Se reescribió
  el parser RT_MENU** (recursivo, `MF_END`/`MF_POPUP` como flags del
  `mtOption` y separadores = id 0 + texto vacío): antes los submenús anidados
  (Viewers, File Format, Block, Convert Selected, Options...) se aplanaban a
  depth 0 y la barra solo mostraba "File". Corregido también el bug de "el
  menú se abre y desaparece al soltar" (`EV_BUTTON_UP` sobre la barra se
  ignora) y el popup se dibujaba con `lfb=NULL` (se inicializa en el modal).
  Validado en QEMU: Alt+F → File (18 items) persiste → ↓↓Enter → diálogo
  Abrir; Alt+E/Alt+H abren Edit/Help; 4/4 PASS en disco.
- Fase E concluida: **persistencia real (escritura a disco)** — **Guardar /
  Guardar Como de metapad escribe en el disco físico** y sobrevive al
  reinicio. Se añadieron: `ata_write_sector` (comando ATA 0x30); un MEFS de
  escritura con allocator "bump" (`next_free_lba` en el superbloque) y
  `mefs_create/write/delete/flush`; syscalls `SYS_FCREATE`/`SYS_FWRITE`/
  `SYS_FDELETE`/`SYS_FLUSH` (26-29); `CreateFileA(GENERIC_WRITE)` +
  `WriteFile` + `SetEndOfFile` + `CloseHandle` reales en kernel32;
  `GetSaveFileNameA` (diálogo Guardar Como) real en comdlg32; comandos de
  shell `touch/write/rm/flush`; teclas F1-F12 mapeadas. `FS_SECTORS` 1130→1400.
  Validado en QEMU disco raw: metapad edita `hola` → menú File→Save As →
  Guardar → persiste tras reinicio; `writetest.exe` y shell `write` también
  persisten. **6/6 PASS en disco** (`tools/test_faseE.py`).
- **Fase 19** = las Fases A-E sobre metapad (edición, abrir, diálogo Abrir,
  menús, persistencia) como un conjunto; ver `DESIGN.md`.
- **Fase 20 (en curso)** = roadmap de compatibilidad Win32 agrupado en 4
  fases. Detalle en la skill `.opencode/skills/win32-compat-roadmap/`.
- **Fase 20 (A) concluida**: **MEFS v2** — migración del allocator "bump"
  a un **bitmap de bloques libres** (los archivos liberan y reutilizan
  bloques al borrar/reescribir), **comando `format`** (formatea el disco
  para uso propio desde MyOS: superbloque limpio + bitmap a 0), y
  **subdirectorios** (`mkdir`, `cd`, `pwd`, `ls` con `/`; entradas con
  `flags`/`parent`). `mefs_write` libera + reasigna vía bitmap; la API de
  raíz (`SYS_DLIST`) sigue lista solo archivos root para metapad. Validado:
  **8/8 PASS** en disco (`tools/test_fase20a.py`): crear/borrar/reutilizar
  bloques, `mkdir docs`→`cd`→`pwd`→`/docs` persiste, `format` limpia,
  regresión metapad OK.
- **Fase 20 (B) concluida**: **`GetSaveFileNameA` con confirmación de
  sobrescritura** (diálogo modal Sí/No en comdlg32 cuando el archivo ya
  existe; si respondes No se vuelve al diálogo y no se sobrescribe) +
  **`CreateToolbarEx` real** en comctl32 (syscall `SYS_TOOLBAR` 30; el
  kernel dibuja la barra de herramientas bajo el menú, igual que
  `SYS_MENUBAR`). Validado: **3/3 PASS** (`tools/test_fase20b.py`) +
  screendump de la toolbar + regresión metapad (abrir/editar/guardar).
- **Fase 20 (C) concluida**: **relocaciones para DLLs en direcciones
  variables**. Las DLLs se enlazan con `ld -q` (conservan `.rel.text`/
  `.rel.data`/`.rel.rodata`/`.rel.exports`) y el kernel aplica los R_386_32
  al cargar; `WIN32_RELOC_DELTA` las reubica fuera de su base fija (p.ej.
  kernel32 en 0xB0200000). `SYS_DLLBASE`/`SYS_GETPROC` (31/32) + kernel32
  resuelven por la base real (GetModuleHandleA/GetProcAddress). Validado:
  **7/7 PASS** (`tools/test_fase20c.py`) con todos los binarios
  (metapad abrir/editar/guardar, dir.exe FindFirstFileA, messagebox.exe).
- **Fase 20 (D) concluida**: **métricas de texto reales + blit por
  regiones**. `GetTextMetricsA`/`GetTextExtentPoint32A`/`GetCharWidthA`
  devuelven métricas 8x16 correctas; el DC de gdi32 acumula un rect sucio
  y `SYS_WINUPDATE` acepta un rect opcional: el kernel (`wm_update_rect`)
  restaura solo el rect del fondo y hace blit parcial del cliente de las
  ventanas que lo intersectan (los hijos usan su rect). Validado:
  **6/6 PASS** (`tools/test_fase20d.py`) + regresión Fase C 7/7.
- **Fase 20 completa (A-D).** Backlog: más shims (OLE32/SHLWAPI/WINSPOOL),
  message loop + CreateWindowEx extendido, diálogos modales, modos
  binario/texto, per-process heap.
- Roadmap tentativo: long mode (64 bits), **apps Windows GUI reales** (ver
  `docs/10-win32-gui-eval.md`: primero nuestro notepad mingw, luego un
  exe externo como Metapad), según el interés.