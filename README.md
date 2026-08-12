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
- Roadmap tentativo: long mode (64 bits), ATA con escritura de archivos
  (CreateFileA con GENERIC_WRITE), **apps Windows GUI reales** (ver
  `docs/10-win32-gui-eval.md`: primero nuestro notepad mingw, luego un
  exe externo como Metapad), según el interés.