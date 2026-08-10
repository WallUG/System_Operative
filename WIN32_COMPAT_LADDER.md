# Win32 EXE Compatibility Ladder

This document defines the recommended executable set and the order to use it when extending MyOS Win32 compatibility.

## Base idea

The goal is not to chase random `.exe` files. The goal is to keep a small regression ladder that grows the compatibility surface in a controlled order:

1. load a simple ELF user program
2. prove the syscall and task boundary
3. prove PE32 import resolution
4. prove CRT startup and exit
5. prove filesystem-backed Win32 APIs
6. prove GUI primitives later, once the lower layers are stable

The repo already contains the source for the base set. Build them with:

```sh
make compat_suite
```

## Current executable set

### Stage 1: userland and scheduler sanity

- `hello.elf`
- `quick.exe`

What it validates:

- user-mode mapping
- `int 0x80`
- task switching
- `SYS_EXIT`
- basic console output

Why it matters:

If these fail, the problem is still in the kernel/user boundary, not in Win32.

### Stage 2: PE32 import resolution baseline

- `winapi.exe`

What it validates:

- `.exe` load path
- `.idata`/imports
- `kernel32.dll` export resolution
- `WriteFile` call path
- `GetProcAddress` plumbing

Why it matters:

This is the first real PE32 compatibility gate after the loader.

### Stage 3: CRT startup baseline

- `hello_win.exe`

What it validates:

- mingw-w64 CRT startup
- `__getmainargs`
- `malloc`/`free`
- `printf`/`puts`
- `exit` returning the right code

Why it matters:

This confirms the runtime layer is usable for normal console apps.

### Stage 4: filesystem-backed Win32

- `dir.exe`

What it validates:

- `CreateFileA`
- `ReadFile`
- `FindFirstFileA`
- `FindNextFileA`
- `FindClose`
- `GetFileSize`
- MEFS directory enumeration

Why it matters:

This is the best regression for `kernel32` file APIs because it exercises repeated reads and directory iteration.

### Stage 5: process and control-flow exercise

- `fork.exe`
- `exec.exe`
- `console.exe`
- `proc.exe`

What they validate:

- task creation and termination paths
- repeated syscalls
- shell interaction
- process handoff and return-to-shell behavior
- process identification: `GetCurrentProcessId` (real PID via `SYS_GETPID`)
- module name: `GetModuleFileNameA` (via `SYS_SELFNAME`, the name the shell/SYS_EXEC launched)
- `return N` from `main` reaching `exit:N` through the mingw CRT

Why it matters:

These binaries help catch regressions that do not show up in a single linear demo.
`proc.exe` is a real mingw-w64 binary whose imports are resolved against the
`kernel32` shim's process APIs (`GetCurrentProcessId`, `GetModuleFileNameA`,
plus the usual CRT/`GetProcAddress` set).

### Stage 6: future GUI ladder

- `messagebox.exe` style probe
- a small `window_loop.exe`
- a paint/update test

What it will validate once added:

- `user32.dll` window creation
- message loops
- close and paint events
- minimal presentation path

Why it matters:

GUI should come only after the console and filesystem stack is stable.

## Recommended order

Use this exact order when validating a fresh change:

1. `hello.elf`
2. `quick.exe`
3. `winapi.exe`
4. `hello_win.exe`
5. `dir.exe`
6. `fork.exe`
7. `exec.exe`
8. `console.exe`
9. `proc.exe`

If all of those pass, then the current Win32 stack is in a good state for broad `.exe` compatibility.

## What to extend next

If the ladder breaks:

- break before `winapi.exe` means PE/import resolution or `kernel32` basics
- break before `hello_win.exe` means CRT/runtime or `msvcrt`
- break before `dir.exe` means filesystem APIs
- break before GUI probes means `user32` needs more work

## Notes

- The repo already has the source files for the current ladder under `user/` and `user/win32/`.
- The `compat_suite` Makefile target is the one-shot way to rebuild the base set.
- Keep this ladder short and stable; add a new `.exe` only when it proves a new compatibility surface.
