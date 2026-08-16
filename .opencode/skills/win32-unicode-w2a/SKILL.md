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
CreateFileW, FindFirstFileW, FindNextFileW, GetModuleFileNameW,
GetCommandLineW, GetEnvironmentStringsW, GetEnvironmentVariableW,
SetEnvironmentVariableW, GetTempPathW, GetCurrentDirectoryW,
SetCurrentDirectoryW, DeleteFileW, MoveFileW, CopyFileW,
GetFullPathNameW, lstrlenW, lstrcpyW, lstrcatW, lstrcmpW,
lstrcmpiW, CharUpperW, CharLowerW, wsprintfW, GetFileAttributesW,
SetFileAttributesW, CreateDirectoryW, RemoveDirectoryW,
GetLogicalDriveStringsW, GetSystemDirectoryW, GetWindowsDirectoryW,
GetVolumeInformationW, SetFilePointerW (no existe — es A), y las
que pida el binario objetivo (diagnóstico del loader).

### 3. Thunks W→A de user32 (~15) + msvcrt
RegisterClassW, CreateWindowExW, SetWindowTextW, GetWindowTextW,
GetWindowTextLengthW, MessageBoxW, LoadStringW, DefWindowProcW,
SendMessageW, GetClassNameW, CharNextW, DrawTextW, TextOutW (gdi32),
GetTextExtentPoint32W (gdi32).
msvcrt: `_wgetmainargs` (entrada en modo W del CRT de MSVC),
`_wfopen`, `_wcsicmp`, `_wtoi`, `_wcslen`, `_wcscpy`.

### 4. Probar con el binario objetivo
- **notepad.exe de Windows 98/2000** (~50 KB, imports pequeños, sin
  manifiestos, sin comctl32 v6, sin .NET) — el mejor primer objetivo.
- **Utilidades del resource kit de Win2000** (consola: añade
  cobertura kernel32/msvcrt W sin GUI).
- El loader da el diagnóstico exacto: "import X no resuelto" →
  implementar X → repetir.

### 5. Probar los drivers de audio y red con apps reales

**Audio (winmm.dll — el siguiente grupo tras el set W)**:
- Implementar **winmm.dll** con: `PlaySoundA`, `waveOutOpen`,
  `waveOutWrite`, `waveOutClose`, `waveOutReset`, `waveOutGetDevCaps`,
  `mciSendStringA`, `timeGetTime` — todos sobre el AC'97 (un buffer
  de DMA a la vez, como el beep de arranque).
- Apps de prueba: **sndrec32.exe** (Grabadora de sonidos de Win98 —
  reproduce WAV), **CD Player de Win98** (mciSendStringA), o un
  player WAV mingw hecho a medida que use waveOutWrite.
- Métrica: que un .exe real reproduzca un WAV por el AC'97 (el test
  verifica el wav del host con `-audiodev wav` — las muestras ya no
  deben ser ceros; ojo: los buffers de DMA deben ir en el HEAP
  (kmalloc), la BSS estática se lee como 0x00 por DMA en QEMU).

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