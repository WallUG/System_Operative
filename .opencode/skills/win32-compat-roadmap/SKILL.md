---
name: win32-compat-roadmap
description: "Use when planning or implementing MyOS Fase 20 (the 12-item Windows compatibility roadmap grouped into four phases A-D). Covers: (A) MEFS bitmap of blocks + format command + subdirectories, (B) real comctl32 (listview/toolbar) + GetSaveFileNameA overwrite confirmation, (C) PE relocations for variable-address DLLs, (D) real font metrics / text measurement. Also documents the historical Fase 19 (metapad phases A-E). Trigger words: Fase 20, roadmap, comctl32, OLE32, SHLWAPI, WINSPOOL, reloc, relocations, bitmap de bloques, format, subdirectorios, TrueType, GDI+, GetTextMetrics, blit por regiones, heap por proceso."
---

# Win32 Compatibility Roadmap (Fase 20)

This skill captures the full roadmap for extending MyOS Windows compatibility,
grouped into four implementation phases (A-D). It exists so the plan does not
get lost between sessions. Use it whenever work on any of these areas begins.

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
- [ ] Phase C — PE relocations
- [ ] Phase D — font metrics / text measurement

## Validation notes
- Metapad is the reference app (already runs: editing, menus, save). Each
  phase should keep metapad working (regression) while opening the door to
  richer apps.
- Use QEMU headless + serial + screendumps as in previous phases
  (`tools/test_faseE.py` pattern).
- The persistence disk must live in the repo cwd (QEMU does not boot from
  `/tmp/...`).
