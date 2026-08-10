---
name: win32-kernel32-filesystem
description: "Use when extending MyOS kernel32.dll filesystem support for real .exe files: CreateFileA/ReadFile/GetFileSize/CloseHandle, directory enumeration, path handling, and handle-table semantics backed by MEFS syscalls."
---

# Win32 Kernel32 Filesystem Skill

This skill is for the specific case where a real Windows `.exe` gets past loader/import resolution but still fails because `kernel32.dll` filesystem or directory APIs are incomplete.

Use this skill to extend the MyOS Win32 layer in a focused way: add missing file APIs, improve handle semantics, or tighten path and directory behavior so real mingw-w64 binaries can keep running without changing the executables themselves.

## Goal

Make console-style `.exe` programs work with real file and directory operations by expanding the proprietary `kernel32.dll` shim in MyOS.

The intended path is:

- the loader resolves imports successfully
- `kernel32` exports the needed file APIs
- those APIs map to MyOS syscalls or MEFS helpers
- the executable can open files, read them, enumerate directories, and exit cleanly

Do not solve this by introducing host emulation or by rewriting the program logic around the missing APIs.

## Scope

Use this skill when the task involves one or more of the following:

- `CreateFileA` or `CreateFileW`
- `ReadFile` or `WriteFile`
- `GetFileSize` or `GetFileAttributesA`
- `CloseHandle`
- `FindFirstFileA` / `FindNextFileA` / `FindClose`
- path parsing, current directory handling, or wildcard matching
- handle-table lifetime bugs
- directory listing backed by MEFS
- real `.exe` output that depends on filesystem metadata or file contents

This skill is not for GUI work, CRT work, or loader work unless the filesystem API failure is the actual blocker.

## Architecture Assumptions

MyOS uses a proprietary Win32 compatibility layer with these rules:

- `.exe` import resolution is already handled by the PE loader and Win32 fixed modules
- filesystem APIs live primarily in `user/win32/kernel32.c`
- the kernel exposes file and directory primitives through MyOS syscalls and MEFS helpers
- handles are MyOS-defined integers, not raw host pointers
- user buffers must always be validated before data is copied back

Preserve those assumptions unless the user explicitly asks for an architecture change.

## Required Working Order

When planning filesystem compatibility work, follow this order:

1. Identify the exact executable and the exact failing file call.
2. Decide whether the failure is in path parsing, handle creation, read semantics, directory enumeration, or buffer copying.
3. Find the smallest missing behavior gap.
4. Determine whether the fix belongs in `user/win32/kernel32.c`, a kernel syscall path, or MEFS helpers.
5. Implement the narrowest change that makes the real executable advance.
6. Validate with the actual `.exe` under QEMU or the project’s existing test flow.
7. Only generalize once the target binary works end-to-end.

## Diagnostic Strategy

Classify the failure before editing:

- invalid handle value returned too early
- handle lifetime not tracked
- file pointer not updated after reads
- `GetFileSize` inconsistent with the underlying object
- directory enumeration returns bad names or wrong termination
- wildcard matching too strict or too loose
- relative path handling does not match the shell/test expectations
- user buffer copy causes a kernel fault or truncated output
- `ReadFile` works for one case but fails on repeated calls

Choose the fix based on the classification, not on a broad reimplementation.

## File API Playbook

When adding or refining a file API:

- keep the exported symbol spelling exactly as the `.exe` imports it
- prefer fixed-size handle tables over pointer exposure
- store enough per-handle state to support repeated reads and EOF
- return Windows-like failure values consistently
- ensure the handle type is stable across the whole module
- preserve the distinction between file handles and directory enumeration handles
- keep invalid-pointer behavior safe for the kernel

When adding directory enumeration:

- use a simple deterministic iterator over MEFS entries
- normalize wildcard handling to the smallest useful subset first
- fill the expected output structure fields that the executable actually reads
- terminate the enumeration cleanly when entries are exhausted

When adding path handling:

- prefer the simplest path normalizer that satisfies the target executable
- decide explicitly whether `.` and relative paths are supported
- keep directory separators and case handling consistent with the rest of MyOS
- avoid pretending to support Windows path features you do not need yet

## Semantics Rules

Prefer behavior that is:

- deterministic
- stable across multiple runs
- safe for invalid user input
- compatible with common mingw-w64 console programs
- simple enough to reason about in a freestanding kernel

If an API cannot be fully implemented, return a failure value that the executable can handle rather than crashing the system.

## Implementation Boundaries

Use these files first:

- `user/win32/kernel32.c` for the shim API surface and handle-table logic
- `kernel/win32.c` if a new fixed module export or module mapping rule is needed
- `kernel/pe.c` if the executable is failing because of import resolution rather than file semantics
- `fs/mefs.c` and `fs/mefs.h` if MEFS needs a new primitive for reading or enumerating entries
- `kernel/syscall.c` if a new syscall must be added or an existing one must be widened

Avoid adding duplicate paths for the same filesystem object if the existing syscalls can be reused.

## Validation Checklist

After a filesystem-related Win32 change, validate the narrowest possible end-to-end path:

- the target `.exe` loads and resolves imports
- file open succeeds when expected
- repeated `ReadFile` calls advance correctly
- directory enumeration yields all expected names
- text output matches the file contents or listing expected by the test binary
- exit status is correct
- no page fault, kernel panic, or corrupted handle state appears

Prefer a real mingw-w64 program like a directory lister, file viewer, or copy-style utility over synthetic unit stubs.

## Practical Targets

This skill usually touches one of these concrete areas:

- `CreateFileA` / `ReadFile` / `GetFileSize` / `CloseHandle`
- `FindFirstFileA` / `FindNextFileA` / `FindClose`
- wildcard matching for `*` and `?`
- handle-table storage for names, sizes, offsets, and directory cursors
- MEFS-backed `DREAD` / `DLIST` bridging
- `GetCurrentDirectoryA` / `SetCurrentDirectoryA` behavior if the executable depends on relative paths

## Red Flags

Stop and investigate if you see any of these:

- a returned handle is a raw pointer exposed to user mode
- a directory handle and a file handle share the same state layout without an explicit type
- reads succeed once but subsequent reads restart from offset zero unexpectedly
- directory enumeration returns partial names or corrupt `WIN32_FIND_DATAA`
- the kernel trusts user buffers without validation
- path matching grows into a full Windows path emulation project accidentally

## Output Standard

When using this skill in a task, the result should make it clear:

- which executable or API family was targeted
- what filesystem behavior was added or fixed
- which file owns the change
- how it was validated
- what still blocks broader `.exe` compatibility

## Recommended Next Questions

If the user has not named a concrete executable, ask for one of these:

- the exact `.exe` that should work next
- whether the next filesystem gap is open, read, enumerate, or path handling
- whether the target is console-only or also needs write support
- whether a new syscall or only a `kernel32` shim change is desired
