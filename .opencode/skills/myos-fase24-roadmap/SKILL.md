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

**Estado**: ListView real (C11), InitCommonControlsEx, falta toolbar/
statusbar/trackbar/treeview completos.

**Tarea**: implementar los hijos virtuales en user32 para estas clases
(msctls_trackbar32, msctls_treeview32) con sus mensajes (TBM_* , TVM_*),
paint y teclado; toolbar con botones que envían WM_COMMAND.

**Por qué**: más controles = más apps.

**Riesgo**: medio.

### P2.2. Threads (CreateThread con scheduler leve)

**Estado**: CreateThread es stub; sin preemptión real.

**Tarea**: `CreateThread` con una tarea por hilo (o hilos cooperativos en
el proceso), un scheduler leve round-robin para ring 3.

**Por qué**: apps con hilos de fondo (UI + worker) se cuelgan sin esto.

**Riesgo**: alto (scheduler). Empezar con 1 proceso = 1 tarea + hilos
cooperativos en user32 (bucle que reparte sys_event).

### P2.3. GDI blits (BitBlt/StretchBlt) + clipboard

**Tarea**: `BitBlt`/`StretchBlt` sobre el DC (copia regiones del buffer),
`GetDC`/`ReleaseDC` con DCs por ventana ya existentes; clipboard
funcional (OpenClipboard/SetClipboardData/GetClipboardData con un buffer
global de texto).

**Por qué**: apps que dibujan imágenes o copian texto.

**Riesgo**: medio.

## Bloque P3 — UX / referencia

### P3.1. Desktop con iconos (taskbar ya existe)

**Estado**: desktop.c ELF con taskbar + fork+exec de apps, sin iconos.

**Tarea**: renderizar iconos (de recursos .rsrc o un set embebido) en el
escritorio + doble clic para lanzar.

**Por qué**: valor de UX alto, % de compatibilidad bajo.

### P3.2. Explorer Win32 completo (listview ya validado en C11)

**Estado**: listview.exe prueba el SysListView32; el explorer real sigue
siendo ELF.

**Tarea**: reescribir explorer.exe Win32 con el ListView (columnas nombre/
tamaño, navegación, doble clic abre/lanza) usando el listview de C11.

**Por qué**: unifica el sistema en Win32.

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