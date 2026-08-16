---
name: win32-unicode-w2a
description: "Use when extending MyOS to run real Microsoft-compiled (MSVC) .exe files: Unicode W-to-A thunking (CreateFileW, GetCommandLineW, GetModuleFileNameW...), the MSVC startup set (GetSystemTimeAsFileTime real clock, GetTickCount, QueryPerformanceCounter, critical sections, VirtualAlloc/VirtualProtect), winmm for real audio apps, and a minimal TCP/IP stack to test internet with wget/links-class apps. Trigger words: thunking, Unicode, W→A, W2A, CreateFileW, GetCommandLineW, GetEnvironmentStringsW, GetModuleFileNameW, VirtualAlloc, VirtualProtect, GetSystemTimeAsFileTime, QueryPerformanceCounter, InitializeCriticalSection, MSVC, binario real, notepad.exe, winmm, waveOutWrite, PlaySoundA, navegador, browser, wget, TCP/IP, stack TCP."
---

# Skill: win32-unicode-w2a — correr .exe reales de Microsoft (MSVC)

Guía para el siguiente salto de compatibilidad de MyOS: **pasar del
ecosistema mingw (APIs A) a los binarios compilados por Microsoft
(MSVC, APIs Unicode W)**. El objetivo es que el primer .exe real de
Windows (notepad.exe de Win98/2000) abra una ventana, y luego usar
apps reales para probar los drivers de audio (AC'97) y red (RTL8139)
añadidos en la Fase 24-P4.5.

Regla de oro: cada ítem = compilar + test QEMU headless + regresión
completa + commit. La Fase 24 (metapad, escritorio, explorer,
diálogos, botones de título) NO debe romperse.

## Estado actual (referencia)

- metapad.exe (mingw) corre completo: menús, aceleradores (formato
  8 B corregido), RichEdit, comdlg32 Guardar/Abrir, iconos .rsrc,
  botones de título X/min/max (kernel).
- Exports: kernel32 108, user32 103, gdi32 37, msvcrt 61, comctl32 7,
  comdlg32 10, advapi32 8, shell32 4, ole32 9, winspool 8, ntdll 5.
- Drivers QEMU: AC'97 (audio, beep de arranque por DMA) y RTL8139
  (red: ARP + ICMP contra 10.0.2.2). Sin stack TCP/UDP aún.
- Detalle completo del análisis: `docs/compat_fase24p4.md`.

## Progreso (Fase 25, paso 1 COMPLETADO — commit pendiente)

El set de inicialización W está hecho y validado (`tools/test_fase25w2a1.py`
9/9 PASS + regresión completa PASS):
- **Reloj real**: `SYS_QPC 45` (nuevo syscall, devuelve edx:eax =
  contador PIT de alta resolución: latch + `ticks*divisor + (divisor-cnt)`,
  serie continua entre ticks; `kernel/drivers/timer.c` `timer_qpc()`).
  `GetSystemTimeAsFileTime` = FILETIME real con origen 2024-01-01
  (constante 133485408000000000 = 0x01DA3C45_7689C000; TODO en
  aritmética 32-bit — **sin `__udivdi3`/`__muldi3` en ring 3**, el
  linker falla con undefined reference). `QueryPerformanceCounter`/
  `Frequency` (frec = 1193182). `GetTickCount` = ticks×10.
- **Sleep real** (espera por ticks, antes era busy-wait corto que no
  avanzaba GetTickCount en el test).
- **Versiones W**: `GetCommandLineW` (TIB ASCII → UTF-16, buffer
  estático en kernel32 — privado por proceso), `GetEnvironmentStringsW`
  (+ Free), `GetModuleFileNameW`, `GetCurrentThreadId` (= pid de la
  tarea actual; cada hilo del kernel tiene pid propio).
- Ya existían y se validaron: secciones críticas (spinlock de 1 bit),
  VirtualProtect (no-op TRUE), VirtualAlloc (win_malloc), Tls*.
- Ajuste de regresión: `test_fase24p32.py` esperaba items=50; la raíz
  ganó w2atest.exe → items=51.

Lecciones de la implementación:
- ring 3 (kernel32.dll) NO tiene libgcc: toda aritmética 64-bit debe
  hacerse con ops 32-bit (u32×u32 → u64 con `mull`, sumas y shifts).
- Los buffers estáticos de las DLL son por-proceso (cada PD mapea su
  copia), seguro para GetCommandLineW/GetEnvironmentStringsW.
- No confiar en discos persistidos viejos al hacer tests manuales:
  `make persist_disk` antes de cada run (un disco stale produjo un
  hang fantasma durante el debug).

## El problema: MSVC ≠ mingw

- mingw compila con APIs **A** (ANSI). MSVC define `UNICODE` y usa
  `wchar_t` (UTF-16): su tabla de imports pide `CreateFileW`,
  `GetCommandLineW`, `RegisterClassW`, etc.
- El loader PE resuelve imports **por nombre exacto** contra los
  exports de cada DLL de MyOS. Si `CreateFileW` no existe → el
  proceso muere antes de ejecutar una instrucción.
- Diagnóstico: el loader ya dice "import X no resuelto" → se
  implementa X → se repite. Iteraciones de minutos.

## Qué ya existe como stub (kernel32.c) y qué falta

| Función | Estado | Trabajo |
|---|---|---|
| GetSystemTimeAsFileTime | **REAL** (origen 2024-01-01, aritmética 32-bit) | — |
| QueryPerformanceCounter | **REAL** (SYS_QPC 45, frec 1193182) | — |
| GetTickCount | **REAL** (ticks×10) | — |
| Sleep | **REAL** (espera por ticks) | — |
| GetCommandLineW | **REAL** (TIB ASCII→UTF-16) | — |
| GetEnvironmentStringsW | **REAL** (bloque UTF-16 PATH/HOME) | — |
| GetModuleFileNameW | **REAL** (A→UTF-16) | — |
| GetCurrentThreadId | **REAL** (= pid de la tarea) | — |
| VirtualAlloc | win_malloc(size) | OK para el caso común |
| VirtualFree | no-op TRUE | OK |
| GetModuleFileNameA | "C:\\MyOS\\"+selfname | OK |
| VirtualProtect | no-op TRUE (sin DEP) | OK |
| CriticalSections | spinlock de 1 bit | OK (MSVC las trata opacas) |
| Toda versión W de archivos/user32 | **no existe** | thunks W→A (paso 2) |

## Thunking W→A (el núcleo)

Exportar las versiones `W` que convierten UTF-16→cp437 y llaman a la
`A` existente. Patrón:

```c
static void utf16_to_ascii(char *out, const uint16_t *in, int max)
{
    int k = 0;
    while (in[k] && in[k] < 256 && k < max - 1)
        out[k] = (char)(in[k] & 0xFF);   /* lossy: acentos a cp437 */
    out[k] = 0;
}

uint32_t __stdcall CreateFileW(const uint16_t *name, uint32_t access, ...)
{
    char a[64];
    utf16_to_ascii(a, name, sizeof(a));
    return CreateFileA(a, access, ...);
}
```

Dirección del thunk:
- **Entrada de cadena** (CreateFileW, FindFirstFileW, GetTempPathW):
  W→A y llamar a la A. Trivial.
- **Salida de cadena** (GetModuleFileNameW, GetCommandLineW): la A
  rellena ASCII; convertir A→UTF-16 en el buffer W del caller (el
  max es en wchar_t: doble de bytes).
- **Bloque de cadenas** (GetEnvironmentStringsW): buffer estático
  convertido.
- **user32**: RegisterClassW/CreateWindowExW/SetWindowTextW/
  GetWindowTextW/LoadStringW — el texto interno se guarda como A.

Limitación aceptada: el MEFS es ASCII — nombres de archivo no-ASCII
se aplanan (mapear a cp437 mitiga).

## Reloj real (ajuste al plan)

Implementar el reloj de verdad, no un stub:
- `GetSystemTimeAsFileTime`: FILETIME = 100 ns desde 1601-01-01.
  Fuente: `timer_get_ticks()` (PIT 100 Hz) escalado a unidades de
  100 ns + origen fijo (p.ej. 2024-01-01T00:00:00Z). Necesita
  exponer el tick del kernel a ring 3 (ya existe SYS_TICKS 41).
- `QueryPerformanceCounter/Frequency`: el PIT como fuente (freq
  real, p.ej. 100 Hz o mejor 1193182/divisor) — no devolver 0.
- `GetTickCount`: ms desde el boot (ticks × 10).
- Esto arregla: `_time64`/`time()`, semilla de rand, tmpnam,
  nombres de temporales derivados del reloj.

## El plan de desarrollo

### 1. Set de inicialización W (lo primero — sin esto ningún MSVC arranca)
**COMPLETADO** (ver "Progreso"): GetCommandLineW, GetEnvironmentStringsW,
GetModuleFileNameW, GetTickCount, QueryPerformanceCounter (SYS_QPC 45),
GetSystemTimeAsFileTime (reloj real con origen 2024-01-01),
GetCurrentProcessId, GetCurrentThreadId, secciones críticas,
VirtualProtect (no-op TRUE). Validado con `tools/test_fase25w2a1.py`
y w2atest.exe.

### 2. Thunks W→A de kernel32 (~50)
**COMPLETADO** (commit con el paso 1): 45 exports nuevos en kernel32:
- Tabla **UTF-16→cp437** `cp437_map[]` (78 entradas: Latin-1, griego,
  símbolos) + `utf16_to_cp437` (W→A, lossy '?') y `cp437_to_utf16`
  (A→W, U+FFFD). Arriba del todo de kernel32.c.
- Thunks de entrada: CreateFileW, FindFirstFileW/FindNextFileW
  (WIN32_FIND_DATAW), GetFileAttributesW, SetFileAttributesW,
  DeleteFileW, MoveFileW, CopyFileW, CreateDirectoryW,
  RemoveDirectoryW, GetEnvironmentVariableW, SetEnvironmentVariableW,
  GetTempPathW, GetWindowsDirectoryW, GetSystemDirectoryW,
  GetFullPathNameW (filepart), GetModuleHandleW, GetVersionExW,
  GetVolumeInformationW, GetLogicalDriveStringsW,
  GetPrivateProfileStringW, WritePrivateProfileStringW,
  GetDateFormatW, GetTimeFormatW, GetLocaleInfoW, FormatMessageW.
- lstr*W directos sobre UTF-16 (sin conversión): lstrlenW, lstrcpyW,
  lstrcpynW, lstrcatW, lstrcmpW, lstrcmpiW.
- A complementarias nuevas: GetEnvironmentVariableA (busca en el
  bloque PATH/HOME), SetEnvironmentVariableA, DeleteFileA,
  MoveFileA/CopyFileA (sys_dread+fcreate+fwrite+fdelete reales),
  CreateDirectoryA (SYS_MKDIR parent=0), RemoveDirectoryA (mefs_delete
  borra dirs vacíos), GetLogicalDriveStringsA ("C:\\").
  GetCurrentDirectoryA ahora devuelve "C:\\MyOS" (antes vacío).
- Validado: w2atest.exe ampliado (10 bloques nuevos), test 19/19 PASS.

**BUG REAL DEL KERNEL ARREGLADO en el camino**: `parse_exports`
(win32.c) fusionaba `.rel.data` y `.rel.exports` en el mismo par de
offsets — el último ganaba, así que los punteros inicializados de
`.data` (p.ej. `env_block[]`) NUNCA se reubicaban con el delta
0x200000: el primer dereferenciado daba #PF (GetEnvironmentVariableA
leyó 0xB0005170 sin mapear). Fix: `rel_exports_off/size` separados y
`apply_relocs` procesa las tres secciones por separado. Síntoma de
diagnóstico: IAT resuelta correcta (0xB0203BC0) pero datos de .data
con direcciones de la base vieja. Lección: cualquier puntero estático
inicializado en una DLL requiere relocación de .rel.data — verificar
con `readelf -r` si aparecen #PF raros en datos.

### 3. Thunks W→A de user32 (~15) + msvcrt
**COMPLETADO** (commit con el paso 2): ~38 exports W nuevos en user32 +
msvcrt W:
- user32: RegisterClassW (copia WNDCLASS de 40 B a la pila y parchea
  lpszClassName/lpszMenuName), CreateWindowExW, SetWindowTextW,
  GetWindowTextW, GetWindowTextLengthW, MessageBoxW, LoadStringW,
  SendMessageW (convierte WM_SETTEXT/WM_GETTEXT), GetClassNameA/W
  (almacena child_class[] al crear el hijo), CharNextA/W,
  CharUpperA/W, CharLowerW, CharUpperBuffW/CharLowerBuffW,
  LoadMenuW/LoadIconW/LoadCursorW/LoadAcceleratorsW,
  GetWindowLongW/SetWindowLongW/SetClassLongW,
  RegisterWindowMessageW, GetDlgItemTextW/SetDlgItemTextW,
  DialogBoxParamW/CreateDialogParamW, y alias sin cadenas:
  GetMessageW/PeekMessageW/PostMessageW/DispatchMessageW/
  DefWindowProcW/SendDlgItemMessageW/IsDialogMessageW/
  TranslateAcceleratorW (MSG y paquetes tienen el mismo layout).
- msvcrt: `_wgetmainargs` + `__wgetmainargs` (VC6) + `_getmainargs`
  (alias A) — argv wchar_t desde el TIB. wcscpy/wcscat/wcscmp/
  wcsncmp/wcsncpy/wcschr/wcsrchr/wcsstr, _wcsicmp/_wcsnicmp/
  _wcslwr/_wcsupr/_wtoi/_wtol/_itow, _wfopen (stub con log; sin
  fopen real en el CRT). Ojo: mingw NO enlaza _wgetmainargs
  (no está en libmsvcrt) — en los tests se resuelve por
  GetProcAddress(GetModuleHandleA("msvcrt.dll"), "_wgetmainargs").
- **FS ampliado a 2400 sectores** (1.2 MB): con user32 a 73 KB el
  bitmap de 2000 sectores desbordaba (IndexError en makefs.py).
  Cambiar Makefile FS_SECTORS Y boot.asm FS_SECTORS equ (idénticos).
- Validado: w2atest.exe pasos 3-5 (clase W + ventana + EDIT con texto
  W por Set/SendMessage, GetClassNameW, Char*W, msvcrt W,
  _wgetmainargs), test 24/24 PASS.

**3 bugs reales de user32 arreglados**:
1. RegisterClassA guardaba el PUNTERO del nombre del caller; el W
   convertía a un buffer de su pila → nombre colgando. Fix: pool
   estático class_name_pool[MAX_CLASSES][16], RegisterClassA copia.
2. class_find comparaba punteros (==) en vez de contenido — un cls
   convertido a buffer temporal nunca encontraba la clase. Fix: ci_eq.
3. builtin_wndproc leía WM_SETTEXT/WM_GETTEXT del parámetro 3 (wParam)
   pero el texto va en el 4 (lParam); GetWindowTextA escribía directo
   y nunca se notó. Fix: s=(char*)b; dst=(char*)b, max=(int)a.

### 4. Probar con el binario objetivo
**COMPLETADO** (w2demo.exe — ver "Progreso"): sin acceso a notepad.exe
real (no hay ISO local y no se descarga de fuentes dudosas), el
binario objetivo es w2demo.exe: **compilado con -DUNICODE -municode**
(el mismo patrón de imports W de un notepad real MSVC). Validado con
`tools/test_fase25w2a2.py` **8/8 PASS** (load del CRT W, WM_CREATE,
teclas → WM_CHAR, Ctrl+S → CreateFileW/WriteFile, cat demo.txt=hola,
Esc → WM_CLOSE → exit:0).

Faltantes que el diagnóstico del loader reveló e implementados:
- kernel32: **GetStartupInfoW** (STARTUPINFOW = 68 B a ceros).
- msvcrt: **__p__wcmdln** (puntero a la cmdline W desde el TIB) y
  **__p___winitenv** (entorno W vacío) — el CRT -municode los pide.
- user32: **GetKeyState real** (kbd_mods = modificadores del último
  EV_KEY: 1=ctrl 2=alt 4=shift; sin tecla-arriba queda "pulsado";
  suficiente para Ctrl+S/Ctrl+O). GetKeyboardState sigue stub.

Notas del binario W:
- -municode es OBLIGATORIO para wWinMain (sin él: undefined
  WinMain@16). Con -municode el CRT W llama __wgetmainargs,
  GetStartupInfoW, __p__wcmdln.
- Los WM_KEYDOWN de QEMU llegan con la MINUSCULA ('s' no 'S') —
  comparar ambos casos o usar aceleradores.
- Ctrl+X de QEMU produce 1 EV_KEY con mods=1 (no hay evento de la
  tecla ctrl sola): el estado de GetKeyState sale del propio evento.
- Esc NO genera WM_CHAR (solo imprimibles): cerrar por WM_KEYDOWN 27.
- Con un notepad.exe real de Win98/2000 disponible, el flujo es el
  mismo: `run notepad.exe` → "import X no resuelto" → implementar X.

### 5. Probar los drivers de audio y red con apps reales

**Audio (winmm.dll — HECHO, commit 697ce05)**:
- winmm.dll implementada y probada: `PlaySoundA` (SND_FILENAME),
  `waveOutOpen/Write/Close/Reset/GetDevCaps/PrepareHeader/
  UnprepareHeader` (WAVEHDR real: lpData en +0, dwBufferLength en +4),
  `mciSendStringA` (open/play/close), `timeGetTime` — todo sobre el
  AC'97 vía **SYS_AUDIO_PLAY 46** (ebx=buf user, ecx=bytes, edx=rate;
  el kernel copia a kmalloc — el DMA NO toca páginas de usuario — y
  reproduce en trozos de 1 s bloqueando; poll del SR, sin IRQ).
- wavplay.exe (mingw `-lwinmm`, imagen base 0x80000000): genera
  tone.wav (440 Hz, 0.5 s, 22050 muestras mono 16-bit) con
  CreateFileA/WriteFile y lo reproduce con las 3 APIs. Test
  tools/test_fase25w2a5.py = 13/13 PASS.
- Conversiones en winmm: 8<->16 bit (unsigned) y mono<->stereo
  (duplicado); la TASA se pasa al codec tal cual (8000-48000, la
  escribe el kernel con outw por reproducción).
- **Lección crítica del driver AC97 (corrige P4.5)**: los registros
  NAM de QEMU son SOLO de 16 bits — inl/outl devuelven 0xFFFFFFFF y
  son no-ops POR DISEÑO. El codec nunca había respondido (P4.5 solo
  validaba el poll del bus master, que completa aunque no haya
  codec). Ahora: reset con outw(NAM+0x00), id=0x8384 (Sigmatel),
  volumen master/PCM con outw (0x02/0x18, valor 0 = sin mute),
  tasa con outw(0x2C) — VRA ya activo tras el reset (0x28=0x0009).
  BD: `ac97_bd[1] = len | 0x80000000` (IOC = bit 31; el bit 0 forma
  parte del length y lo rompe).
- **Bug conocido de QEMU 10.2.2**: el wav del host sale distorsionado
  (regresión 42061a14 "audio/mixeng: replace redundant pcm_info
  fields", conv/clip usa hw->info.af en vez de sw->info.af; fix
  924b0be8 aún sin empaquetar en Debian/Kali; `out.mixing-engine=off`
  rompe el backend wav). La validación de contenido se apoya en el
  lado GUEST: el kernel imprime `ac97: play rate=.. bytes=.. cksum=
  .. res=0` por reproducción (DMA completado con los datos correctos)
  + el wav del host con cabecera correcta (44100/2/16) y muestras no
  nulas (>30000: beep + 3 plays; cada segmento sale x2 por el bug).
- sndrec32.exe de Win98 no se ha probado aún (requiere más imports:
  waveOutGetNumDevs, GetShortPathNameA, etc.) — wavplay cubre el
  camino.

**Internet (stack TCP/IP — primero, los navegadores lo necesitan)**:
- El driver RTL8139 hoy hace ARP + ICMP. Para navegar hace falta
  **TCP** (y UDP para DNS). Plan mínimo:
  a) **UDP** primero (más simple): probar contra el user-net con el
     DNS de 10.0.2.3 o el echo — valida RX de segunda recepción
     (¡el quirk del ring de QEMU en la 2ª recepción! — verificar si
     afecta a UDP o si era solo el ICMP).
  b) **TCP cliente** (connect/send/recv sobre el RTL8139, sin
     estado de servidor): suficiente para HTTP GET.
  c) Probarlo con **wget.exe / curl.exe de GnuWin32** (consola,
     imports kernel32+msvcrt — compatibles con el set W) contra el
     user-net (el slirp redirige 10.0.2.2:80 → host).
- **Navegadores**: los gráficos modernos (Chrome/Firefox) son
  imposibles. Los navegadores de **texto** (links.exe, w3m, lynx —
  consola, ~100-300 KB, sin GDI) son el objetivo realista: tras el
  stack TCP + set W + msvcrt, `links -dump http://10.0.2.2/` es
  alcanzable. Un navegador gráfico antiguo (IE6, Netscape) requiere
  demasiada superficie (COM, ActiveX, GDI avanzado) — fuera de
  alcance a corto plazo.
- Métrica: `wget` baja un archivo por HTTP y el checksum coincide.

## Patrones de test (QEMU headless)

- `qemu-system-i386 -display none -monitor unix:/tmp/opencode/qmon.sock,server,nowait -serial stdio -no-reboot -no-shutdown -drive format=raw,file=build/os-persist.bin` + `-audiodev wav,path=...,id=audio0 -device AC97,audiodev=audio0` (audio) y `-netdev user,id=net0 -device rtl8139,netdev=net0 -object filter-dump,id=f0,netdev=net0,file=/tmp/opencode/x.pcap` (red).
- Regenerar `build/os-persist.bin` con `make persist_disk` antes de
  cada test (os-image.bin está en la raíz).
- `cancel_autoboot()`: esperar "Autoboot:" y enviar una tecla.
- Esperar `exit:0` de la app anterior antes de lanzar la siguiente
  (`sched_user_busy`).
- Import no resuelto: el loader lo reporta al serial → el test busca
  la cadena del import faltante.
- El wav del host se verifica con struct: las muestras deben tener
  amplitud > 0 (si son ceros: buffers en la BSS o bus master del
  PCI command register sin activar).

## Checklist de commit

- [ ] `make -j$(nproc)` limpio (solo el warning RWX del linker).
- [ ] `make persist_disk` OK.
- [ ] Test del ítem PASS.
- [ ] Regresión completa: 20a-d, faseE, fase22, fase23 completo,
      fase24 p11-p32, p4, p45.
- [ ] README.md, `docs/compat_fase24p4.md` y esta skill actualizados.
- [ ] Un solo commit con mensaje descriptivo.

## Notas de la Fase 24-P4.5 (drivers, útiles para el plan)

- **BUS MASTER**: sin el bit 2 del PCI command register (0x04) el
  DMA de QEMU devuelve CEROS silenciosamente — activarlo en todo
  driver PCI nuevo.
- **BSS vs heap para DMA**: los arrays estáticos de la BSS del
  kernel se leen como 0x00 por DMA en QEMU; usar kmalloc (heap,
  0x2000000+).
- **AC'97**: reset del codec en NAM+0x00 (no 0x7C); BD de 8 B con
  len = halfwords (sin -1).
- **RTL8139**: el QEMU transmite el descriptor actual (rotar
  descriptores); el RX ring: header 4 B + datos + crc 4 B, el
  avance del CAPR es len+4 (no len); la 2ª recepción del ring no
  entrega en QEMU (quirk — verificar con UDP si aplica).