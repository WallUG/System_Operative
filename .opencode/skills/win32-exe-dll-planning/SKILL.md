---
name: win32-exe-dll-planning
description: "Use when extending MyOS to run more proprietary .exe files by implementing or expanding Win32 DLL shims, PE imports, CRT startup behavior, or fixed ring-3 module support."
---

# Win32 EXE DLL Planning Skill

This skill defines the workflow for extending MyOS so more Windows `.exe` binaries run natively through the existing proprietary Win32 compatibility layer.

## Goal

Grow compatibility by implementing the missing Win32 DLL surface inside MyOS, not by relying on Windows binaries, Wine, or host-side emulation. The target is to make real `.exe` files progress from load-time success to correct runtime behavior by adding the minimum viable exports and semantics in the repository’s own DLL shims.

## Scope

Use this skill when the task involves one or more of the following:

- a new `.exe` fails because an import is missing
- a real mingw-w64 or similar executable needs another Win32 API
- a DLL shim needs more exports, better semantics, or new data exports
- the CRT startup path needs to be extended
- the loader resolves imports correctly but runtime behavior is still wrong
- a new fixed ring-3 module needs to be added to the Win32 layer

Do not use this skill for generic kernel work that is unrelated to `.exe` compatibility.

## Current Architecture Assumptions

MyOS currently follows these rules:

- PE32 `.exe` loading is handled by the kernel PE loader.
- Win32 DLLs are fixed ring-3 modules mapped at stable virtual addresses.
- Imports are resolved against a local export table, not against host Windows.
- The compatibility layer is intentionally proprietary and self-contained.
- Compatibility growth happens by adding or refining DLL exports, not by changing the user programs to avoid Windows APIs.

Before making changes, always preserve these invariants unless the user explicitly asks for an architecture shift.

## Required Working Order

Follow this order when planning a new compatibility increment:

1. Identify the failing executable and the exact failure mode.
2. Determine whether the failure happens during load, import resolution, CRT startup, or the first runtime API call.
3. Find the smallest missing DLL export or behavior gap.
4. Decide whether the fix belongs in `kernel/win32.c`, `kernel/pe.c`, `user/win32/*.c`, or the supporting linker/build scripts.
5. Implement the narrowest shim that satisfies the executable.
6. Validate with the real executable under QEMU or the project’s existing test flow.
7. Only after the first executable works, generalize the implementation into a reusable export or helper.

## Diagnostic Strategy

When a `.exe` fails, classify the failure first:

- import missing
- wrong DLL name matching
- wrong export name or calling convention
- bad data export
- incorrect CRT initialization
- bad stack/TIB/FS state
- broken file API semantics
- broken process/exit behavior
- invalid pointer handling or user/kernel copy boundary issue

Use the classification to decide the next edit. Avoid broad refactors before you know which bucket the failure is in.

## DLL Expansion Playbook

When adding a new API to an existing DLL shim:

- confirm whether the function is code export or data export
- match the exact exported symbol spelling that the executable imports
- keep the implementation freestanding and deterministic
- prefer the simplest semantic model that makes the target executable behave correctly
- reuse existing syscalls where possible instead of creating duplicate kernel paths
- add the export entry to the module table after the function is implemented
- ensure case-insensitive resolution still works for common Windows import naming variants

When a new DLL is needed:

- decide whether it belongs in the fixed ring-3 region
- add the module source under `user/win32/`
- wire it into the build and loader path
- ensure the kernel maps it into every user PD that needs Win32 support
- define exports in the `.exports` section so the loader can resolve them

## Semantics Rules

Prefer API behavior that is:

- stable across executions
- safe under invalid user pointers
- simple enough to reason about in a freestanding kernel
- compatible with the CRT expectations of common mingw-w64 binaries

If a Win32 API is not fully implemented, return a plausible failure value instead of crashing the kernel. Avoid exposing partial behavior that can corrupt kernel memory or user address spaces.

## Validation Checklist

After any change in the Win32 layer, validate at the narrowest possible level:

- the executable still loads
- imports resolve to the intended exports
- the runtime path reaches `main`
- the program prints the expected output or performs the expected file action
- exit status is correct
- no kernel panic, page fault, or broken user/kernel boundary appears

Prefer a real mingw-w64 test binary over synthetic stubs whenever possible.

## Practical File Map

Use these areas first when adding support:

- `kernel/pe.c` for PE import resolution and loader behavior
- `kernel/win32.c` for DLL loading, mapping, and export resolution
- `kernel/win32.h` for fixed module constants and Win32 shared types
- `user/win32/kernel32.c` for kernel32-style APIs and kernel-facing syscalls
- `user/win32/msvcrt.c` for CRT startup, heap, stdio, and exit behavior
- `user/win32/user32.c` for basic UI/message shims
- `user/win32/ntdll.c` for low-level RTL helpers
- `user/win32/*.c` for any new proprietary DLL shim

## Red Flags

Stop and investigate if you see any of these:

- the `.exe` imports are resolved, but the first call still jumps to bad memory
- a DLL export exists but the symbol name does not match exactly
- a data export is being treated like a function export or vice versa
- the CRT expects a TIB/FS layout that is not yet mapped
- a file API returns a handle shape that the CRT does not accept
- a change would require host Windows libraries or external emulation

## Output Standard

When using this skill in a task, the result should make it clear:

- which executable or API family was targeted
- what missing capability was added
- which file owns the change
- how it was validated
- what remains missing for broader `.exe` compatibility

## Recommended Next Questions

If the user has not specified a concrete target, ask for one of these:

- the exact `.exe` that should work next
- the Windows API family needed next
- whether the next step is loader, CRT, console, filesystem, or GUI support
- whether the goal is to expand an existing DLL or add a new DLL shim
