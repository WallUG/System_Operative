---
name: win32-msvcrt-runtime
description: "Use when extending MyOS msvcrt.dll support for real .exe files: CRT startup, __getmainargs, stdio formatting, heap, atexit, exit behavior, and other runtime expectations from mingw-w64 binaries."
---

# Win32 MSVCRT Runtime Skill

This skill is for the case where a Windows `.exe` already loads in MyOS, but still fails because the MSVCRT runtime surface is incomplete or semantically wrong.

Use this skill to grow the proprietary `msvcrt.dll` shim so real mingw-w64 executables can reach `main`, print correctly, allocate memory, register exit handlers, and return the expected exit code.

## Goal

Make real Windows console `.exe` programs run through the MyOS CRT compatibility layer without relying on host Windows, Wine, or external runtime emulation.

The intended path is:

- the PE loader resolves imports
- `kernel32` provides the basic Win32 services
- `msvcrt` provides runtime startup, stdio, heap, and exit semantics
- `main` executes with sane `argc`/`argv`/environment values
- output and exit status match what the program expects

## Scope

Use this skill when the task involves one or more of the following:

- `__getmainargs` or startup argument plumbing
- `exit`, `_exit`, `_cexit`, `atexit`, `abort`
- `printf`, `fprintf`, `vfprintf`, `puts`, `fputs`, `putchar`, `fputc`, `fwrite`
- `malloc`, `calloc`, `realloc`, `free`
- `localeconv`, `strerror`, `signal`, `_errno`
- MSVCRT data exports such as `__lc_codepage`, `__p__iob`, `__p__fmode`, or `__p___mb_cur_max`
- calling convention mismatches in `va_list` or stdio formatting
- runtime failures that happen after imports resolve but before or inside `main`

This skill is not for loader bugs, filesystem semantics, or GUI support unless the MSVCRT runtime is the actual blocker.

## Architecture Assumptions

MyOS already uses a proprietary Win32 compatibility stack with these assumptions:

- PE32 `.exe` loading is already handled by the kernel loader
- fixed ring-3 DLL modules are mapped into user page directories
- `kernel32` provides the base process and file services the CRT expects
- `msvcrt` is a freestanding shim compiled as part of the OS, not the host C runtime
- runtime behavior should be deterministic and self-contained

Keep those assumptions intact unless the user explicitly asks to change the architecture.

## Required Working Order

When planning runtime work, follow this order:

1. Identify the exact `.exe` and the exact runtime failure point.
2. Decide whether the problem is startup, formatting, heap, exit, or a missing data export.
3. Find the smallest missing function or semantic mismatch.
4. Determine whether the fix belongs in `user/win32/msvcrt.c`, `user/win32/kernel32.c`, or the loader/kernel setup.
5. Implement the narrowest runtime change that lets the binary advance.
6. Validate with the real executable under QEMU or the project’s existing test flow.
7. Generalize only after the target binary works end-to-end.

## Diagnostic Strategy

Classify the failure before editing:

- `__getmainargs` returns bad `argc`/`argv`/`envp`
- the CRT aborts before `main`
- `printf` output is malformed or truncated
- `va_list` handling is wrong for i386 MSVCRT
- `malloc` returns null or corrupts the heap path
- exit handlers do not run in the right order
- `exit` returns the wrong code to the kernel
- data exports are read as functions or vice versa
- `stdin`/`stdout`/`stderr` objects are not shaped the way the CRT expects

Use that classification to choose the next edit instead of broadening the runtime surface blindly.

## Runtime Playbook

When adding or refining MSVCRT behavior:

- match the exact exported symbol spelling the `.exe` imports
- keep the implementation freestanding and deterministic
- prefer simple, predictable semantics over emulating every Windows corner case
- preserve the calling convention the binary expects
- keep `va_list` handling aligned with i386 MSVCRT rules
- implement only the format flags and length modifiers the target binary actually uses
- ensure the runtime can continue even if optional features are stubbed out

When adding a new data export:

- make it look like the Windows runtime object the CRT expects to read
- ensure it is exported as data, not as a callable function
- keep the value stable across the process lifetime
- update the export table only after the backing object exists

When improving heap or exit semantics:

- use the kernel syscall path already available where possible
- track ownership and cleanup in a deterministic way
- keep `atexit` execution LIFO
- make `_cexit`, `_exit`, `exit`, and `abort` consistent with the process termination path

## Semantics Rules

Prefer behavior that is:

- stable across executions
- simple enough to reason about in a freestanding kernel
- compatible with common mingw-w64 console programs
- safe for invalid or unexpected inputs
- good enough to let the program reach useful work rather than crash early

If full Windows semantics are not practical, implement the smallest plausible behavior that the target program can tolerate.

## Implementation Boundaries

Use these files first:

- `user/win32/msvcrt.c` for CRT startup, output, memory, and exit behavior
- `user/win32/kernel32.c` for the Win32 APIs that the CRT calls underneath
- `kernel/win32.c` if a new fixed module export or module mapping rule is required
- `kernel/pe.c` if the import table is correct but runtime startup still fails
- `kernel/gdt.c` and related setup files if the CRT needs segment or TIB state

Avoid duplicating runtime logic in multiple modules if the CRT can share one path.

## Validation Checklist

After an MSVCRT-related change, validate the narrowest possible end-to-end path:

- the executable reaches `main`
- `argc`/`argv`/`envp` are acceptable to the binary
- console output appears as expected
- `malloc`/`free` do not crash the process
- `atexit` callbacks run in the right order if used
- the program exits with the expected code
- no page fault, kernel panic, or CRT abort occurs

Prefer a real mingw-w64 console program like a hello-world, printf-heavy test, or heap test over synthetic stubs.

## Practical Targets

This skill usually touches one of these concrete areas:

- `__getmainargs` and startup argument shape
- `__p__iob`, `__lc_codepage`, `__p__fmode`, `__p___mb_cur_max`
- `printf` / `vfprintf` width, flags, and length modifiers
- `malloc` / `calloc` / `realloc` / `free`
- `exit` / `_exit` / `atexit` / `_cexit` / `abort`
- `localeconv`, `strerror`, `signal`, `_errno`
- simple wide-char helpers that the CRT may import

## Red Flags

Stop and investigate if you see any of these:

- `va_list` is treated like a C99 platform type when the binary expects MSVCRT i386 semantics
- a data export is stored as a function pointer but the CRT reads it as a variable
- `printf` works for `%s` but fails on width, left alignment, or length modifiers the target uses
- `malloc` uses a path that can silently hand back overlapping storage
- `exit` bypasses the kernel process termination path or returns to user code unexpectedly
- the runtime starts accreting ad hoc Windows behavior that no current executable needs

## Output Standard

When using this skill in a task, the result should make it clear:

- which executable or runtime family was targeted
- what runtime behavior was added or fixed
- which file owns the change
- how it was validated
- what still blocks broader `.exe` compatibility

## Recommended Next Questions

If the user has not named a concrete binary, ask one of these:

- which `.exe` should work next
- whether the next gap is startup, `printf`, heap, or exit behavior
- whether the target is a console app or a GUI app
- whether the next fix should live in `msvcrt` or `kernel32`
