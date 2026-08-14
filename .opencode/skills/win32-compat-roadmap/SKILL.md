---
name: win32-compat-roadmap
description: "Use when planning or implementing MyOS Fase 20 (the 12-item Windows compatibility roadmap grouped into four phases A-D). Covers: (A) MEFS bitmap of blocks + format command + subdirectories, (B) real comctl32 (listview/toolbar) + GetSaveFileNameA overwrite confirmation, (C) PE relocations for variable-address DLLs, (D) real font metrics / text measurement. Also documents the historical Fase 19 (metapad phases A-E), Fase 21 (escritorio lanzador + explorador con subdirectorios) y Fase 22 (arranque tipo Windows: bootscreen con barra de carga, autoboot del escritorio, flag bootgui, shell viva por debajo). Trigger words: Fase 20-22, roadmap, comctl32, OLE32, SHLWAPI, WINSPOOL, reloc, relocations, bitmap de bloques, format, subdirectorios, TrueType, GDI+, GetTextMetrics, blit por regiones, heap por proceso, bootscreen, barra de carga, autoboot, bootgui, escritorio."
---

# Win32 Compatibility Roadmap (Fase 20)

This skill captures the full roadmap for extending MyOS Windows compatibility,
grouped into four implementation phases (A-D). It exists so the plan does not
get lost between sessions. Use it whenever work on any of these areas begins.

## Historical context: Fase 22 (arranque tipo Windows)

After Fase 21, MyOS got a Windows-style boot experience (the "system.exe"
model):

- **Boot splash + progress bar** (`kernel/bootscreen.c/h`): azure background,
  "MyOS 0.4.0" title, phase text and green progress bar drawn straight to
  the LFB. `kmain` calls `bootscreen_status(fase, pct)` at each stage
  (drivers → memory → pagination → interrupts → MEFS → Win32 DLLs →
  multitasking → shell) with short delays so it is visible. During the
  splash, `kprint` only writes to the serial (`bootscreen_active()` check in
  kprint.c): the boot log stays available for headless tests but does NOT
  paint over the animation (fix: text used to overlap the progress bar).
  The Fase 3/5 boot demos were removed for a clean boot: PIT 1 s check,
  the 3 s keyboard window, and the T-A/T-B scheduler demo tasks.
  `bootscreen_done()` clears to the vgafx console.
- **Autoboot of the desktop**: `shell_autoboot()` runs before the prompt;
  if the MEFS superblock flag `boot_gui` is set and `desktop.elf` exists it
  prints "Autoboot: escritorio en 3 s" and waits 3 s polling keyboard+serial
  without blocking. Any key cancels (key is discarded) → console. Otherwise
  it calls `shell_run_file("desktop.elf")` (refactored core of the `run`
  command) and the shell keeps living underneath: closing the desktop with
  'q' restores the console and the prompt is back.
- **Persisted flag**: `MEFS_SB_BOOTGUI` (superblock byte at offset 36),
  `mefs_boot_gui()` / `mefs_set_boot_gui()`, `bootgui on|off` shell command
  (+ `flush` to persist), written by `tools/makefs.py` (`-b 1` default).
- **Fix**: `keyboard_flush()` in `wm_recompute` when the last window closes
  — the key that closed the window no longer leaks into the shell's line.
- **Fix (scheduler)**: `sched_kill_current` leaves `current` pointing at the
  dead task and `sched_tick` returned early when `task_count<2`, so without
  the demo tasks the CPU stayed in the dead task's `task_stub_exit` loop and
  the shell never resumed. Now `sched_tick` rotates when `current` is
  `TASK_FREE` even with a single live task (the idle → shell read_line).
- **Tests**: `tools/test_fase22.py` 7/7 PASS; all previous test scripts got
  a `cancel_autoboot()` helper (waits for "Autoboot:" and sends a key that
  is discarded). test_faseE also waits for `exit:0` before launching the
  next app (the shell does not read keys while a user task runs).

## Historical context: Fase 21 (escritorio lanzador + explorador con subdirs)

After Fase 20 (A-D), MyOS completed **Fase 21** = desktop launcher +
subdirectory explorer, closing the loop between the Win32 work and the
native GUI environment:

- **`SYS_DLISTDIR` (33), `SYS_DPARENT` (34), `SYS_DLOOKUP` (35)** — ring-3
  navigation of MEFS subdirectories (`parent` = entry index, `MEFS_ROOT`
  = 0xFFFFFFFF). `sys_dlistdir` returns `{name[16], size, flags}` per entry,
  `sys_dparent(idx)` the parent, `sys_dlookup(parent, name)` the index.
- **`desktop.elf` multi-button taskbar** — buttons for EXPLORADOR
  (explorer.elf), METAPAD (metapad.exe), MENSAJE (messagebox.exe), DEMO
  (win_demo.elf); keys 1-4 or click launch via fork+exec (kernel picks PE
  vs ELF automatically). Clicking is mouse-driven; keys 1-4 are the
  headless-test path.
- **`explorer.elf` subdirectory navigation** — lists the cwd via
  `sys_dlistdir`, dirs shown with trailing `/` and in cyan; Enter on a dir
  = cd, `b` = up (`sys_dparent`), arrows Up (0x102)/Down (0x103) or
  u/d/j/k move the selection; Enter on `.exe`/`.elf` = fork+exec
  (launches Win32 and native apps), Enter on other files = text viewer.
- **Bugfix**: `sys_winupdate` in `winlib.h` now forces `ecx=0` — the kernel
  treats `ecx!=0` as an optional rect pointer for region blits (Fase 20-D),
  and garbage in ecx made `user_memcpy_in` fail silently so `wm_update`
  never ran → the window stayed black (only appeared when launched from
  the desktop; direct launch happened to leave ecx=0).

## Historical context: Fase 19 (metapad phases A-E)

Before the Fase 20 roadmap, MyOS completed **Fase 19** = the metapad
capability set, committed as a series of phase-letters:

- **Fase 19 (A)** — real editing in metapad: WM_CHAR/WM_KEYDOWN to the focused
  RichEdit, caret.
- **Fase 19 (B)** — open file by CLI: CreateThread real, ReadFile, non-blocking
  dialog.
- **Fase 19 (C)** — real Open dialog: `GetOpenFileNameA` over the MEFS list
  (filters, prefix, keyboard+mouse navigation).
- **Fase 19 (D)** — menu bar + drop-downs: RT_MENU parser fixed, recursive,
  `TrackPopupMenuEx` modal, WM_COMMAND.
- **Fase 19 (E)** — real disk persistence: `ata_write_sector` (ATA 0x30),
  writable MEFS (bump allocator via `next_free_lba`), syscalls
  `SYS_FCREATE/FWRITE/FDELETE/FLUSH` (26-29), `CreateFileA(GENERIC_WRITE)` +
  `WriteFile` + `SetEndOfFile`/`CloseHandle`, `GetSaveFileNameA` real.
  Makefile targets `persist_disk`/`run_persist`/`test_persist`.

**Fase 20** groups the remaining roadmap (items below) into four phases A-D.

## The 12-item roadmap

Grouped by area of impact. All items are about growing compatibility inside
MyOS's own DLL shims / kernel — never by pulling in Windows binaries, Wine, or
host emulation.

### Loader core (highest impact)
1. **More DLL shims / imports**: `GetOpenFileNameA`/`GetSaveFileNameA` already
   work. Add `OLE32.dll`, `SHLWAPI.dll`, `WINSPOOL.dll` shims for apps that
   import them (metapad does not use them, so these are for other apps).
2. **Real `comctl32.dll`** (metapad imports Common Controls): today it is a
   ~5KB stub. Implement `InitCommonControls`, real listviews/toolbars to open
   richer apps.
3. **GDI+ / TrueType fonts**: today the font is an 8x16 bitmap. Apps that
   measure text with `GetTextMetrics`/`DrawText` rely on metrics; without real
   fonts the layout differs.

### Windows / message API
4. **Message loop + `TranslateMessage`**: harden `WM_KEYDOWN`→`WM_CHAR`,
   focus, and child windows (RichEdit already works well).
5. **Extended `CreateWindowEx`**, real `WS_OVERLAPPEDWINDOW` styles,
   resizing.
6. **Correct blocking modal dialogs** (today the Open dialog is a hand-rolled
   loop).

### Files (basis for better "Save")
7. **Bitmap of free blocks in MEFS + subdirectories** (for a real file
   explorer).
8. **`GetSaveFileNameA` with overwrite + confirmation dialog** (metapad today
   overwrites silently).
9. **Binary vs text file modes** (LF/CRLF conversion) that some apps expect.

### Fixed DLLs / relocation
10. **PE relocations (`.reloc`)**: the Win32 DLLs are fixed at
    `0xB0000000+` (1 MiB each). Apps with many imports or their own DLLs need
    relocations — today a fixed base is assumed.

### Performance / correctness
11. **Real font resolution / text measurement** and **region-based blitting**
    (today it redraws a lot).
12. **Zero `.bss` for DLLs and per-process heap** (today a global bump).

## Phased implementation order

### Phase A — MEFS bitmap + format command + subdirectories
- Replace the bump allocator (`next_free_lba`) with a **free-block bitmap**
  stored in/persisted via the superblock.
- Add a **`format`** shell command (and kernel routine) that writes a clean
  superblock (num_files=0, all blocks free) — i.e. "format a disk for our own
  use" from inside MyOS.
- Add **subdirectories** to MEFS so a real file explorer becomes possible.
- Files touched: `kernel/fs/mefs.c/h`, `kernel/shell.c`,
  `tools/makefs.py`, docs `05-filesystem.md`.

### Phase B — real comctl32 (listview/toolbar) + GetSaveFileNameA confirmation
- Implement `InitCommonControls` and real listview/toolbar widgets in
  `comctl32.dll`.
- Add **overwrite confirmation** to `GetSaveFileNameA` in `comdlg32.c`.
- Files touched: `user/win32/comctl32.c`, `user/win32/comdlg32.c`.

### Phase C — PE relocations for variable-address DLLs
- Parse the `.reloc` section and apply fixups so DLLs can load at addresses
  other than their fixed `0xB0000000+` bases.
- Files touched: `kernel/pe.c`, `tools/makepe.py`, DLL loader in `kernel/win32.c`.

### Phase D — real font metrics / text measurement
- Implement text measurement (`GetTextMetrics`, `GetTextExtentPoint32`) and
  font resolution; improve region-based blitting.
- Files touched: `user/win32/gdi32.c`, `user/win32/user32.c`, font data.

## Progress tracking
- [x] Phase A — MEFS bitmap + format + subdirectories (v2: superbloque con
      bitmap_lba/data_start/fs_capacity; entradas con flags+parent;
      `mefs_format`; comandos shell `format`/`mkdir`/`cd`/`pwd`/`ls`;
      `tools/test_fase20a.py` 8/8 PASS)
- [x] Phase B — real comctl32 + GetSaveFileNameA confirmation
      (GetSaveFileNameA pide confirmacion de sobrescritura: dialogo Si/No
      en comdlg32 cuando el archivo existe; CreateToolbarEx real via
      syscall SYS_TOOLBAR (30) que dibuja la barra de herramientas en el
      kernel bajo el menu; `tools/test_fase20b.py` 3/3 PASS + screendump
      toolbar)
- [x] Phase C — PE relocations (DLLs de MyOS enlazadas con `ld -q`
      conservan `.rel.text/.rel.data/.rel.rodata/.rel.exports`; el kernel
      las parsea y aplica R_386_32 al binario RAM (VA->file offset via
      PT_LOAD); `WIN32_RELOC_DELTA` (0x200000) reubica las DLLs fuera de
      su base fija (kernel32 en 0xB0200000+); `SYS_DLLBASE` (31) y
      `SYS_GETPROC` (32) + GetModuleHandleA/GetProcAddress resuelven por
      base real; `find_module` usa dll_idx (no deriva de la base);
      `tools/test_fase20c.py` 7/7 PASS)
- [x] Phase D — font metrics / text measurement + region-based blitting
      (GetTextMetricsA/GetTextExtentPoint32A/GetCharWidthA con metricas
      8x16 correctas; blit por regiones: DC con dirty rect acumulado,
      SYS_WINUPDATE acepta un rect {x,y,w,h} opcional, wm_update_rect
      restaura solo el rect del fondo y redibuja las ventanas que lo
      intersectan con blit parcial del cliente; child_repaint usa el
      rect del hijo; `tools/test_fase20d.py` 6/6 PASS)

**Fase 20 completa (A-D).** **Fase 21 completada** (escritorio lanzador de
apps Win32/ELF + explorador con subdirectorios). **Fase 22 completada**
(arranque tipo Windows: bootscreen con barra de carga, autoboot del
escritorio con cuenta atras cancelable, flag persistido `bootgui on|off`,
shell viva por debajo como system.exe). Quedan en backlog los items 1 (mas
shims OLE32/SHLWAPI/WINSPOOL), 4-6 (message loop, CreateWindowEx
extendido, dialogos modales), 9 (modos binario/texto) y 12 (per-process
heap). Siguiente paso natural propuesto: instalador tipo Windows (dialogo
que copia archivos del FS a un disco persistente con estructura de
directorios), ahora viable porque ya hay navegacion de directorios,
persistencia y arranque GUI.

## Validation notes
- Metapad is the reference app (already runs: editing, menus, save). Each
  phase should keep metapad working (regression) while opening the door to
  richer apps.
- Use QEMU headless + serial + screendumps as in previous phases
  (`tools/test_faseE.py` pattern).
- The persistence disk must live in the repo cwd (QEMU does not boot from
  `/tmp/...`).
