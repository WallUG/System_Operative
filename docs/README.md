# MyOS — Documentación técnica

Documentación formal del sistema operativo MyOS (x86, IA-32). Cada
documento cubre un subsistema completo: diseño, piezas, formatos,
ABI y bug-notes relevantes.

## Índice

| Documento | Contenido |
|-----------|-----------|
| [01-arquitectura.md](01-arquitectura.md) | Visión global: modos, mapa de memoria, layout del disco/CD, GDT, estado actual |
| [02-arranque.md](02-arranque.md) | Bootloader (BIOS → modo protegido), arranque por CD (El Torito), boot_info |
| [03-memoria.md](03-memoria.md) | PMM (E820 + bitmap), paginación (PSE + 4 KiB), heap del kernel |
| [04-multitarea.md](04-multitarea.md) | Scheduler round-robin, context switch, TSS, tareas de usuario |
| [05-filesystem.md](05-filesystem.md) | MEFS: superbloque, directorio, datos, fuentes de sector (ATA/RAM) |
| [06-syscalls.md](06-syscalls.md) | ABI `int 0x80`: tabla completa, convenciones, validación de punteros |
| [07-drivers.md](07-drivers.md) | VGA, serial, PIT, teclado PS/2, ratón PS/2, ATA, VBE |
| [08-gui.md](08-gui.md) | Consola gráfica vgafx, eventos de ratón/teclado, gestor de ventanas (winmgr), escritorio |
| [09-win32.md](09-win32.md) | Capa Win32: loader PE, DLLs fijas ring 3, TIB/CRT, escalera de compatibilidad |
| [10-win32-gui-eval.md](10-win32-gui-eval.md) | Evaluación: ejecutar aplicaciones Windows GUI reales (notepad++, alternativas) |

## Cómo se relaciona con otros archivos

- `DESIGN.md` — bitácora de decisiones y bugs por fase (histórico).
- `README.md` — vista rápida, requisitos y comandos.
- `WIN32_COMPAT_LADDER.md` — la escalera de compatibilidad .exe.
- `kernel/`, `user/`, `boot/`, `tools/` — el código fuente al que esta
  documentación referencia.

## Glosario rápido

- **LFB** — Linear Framebuffer del modo VBE (800x600x32, 0x00RRGGBB).
- **MEFS** — MyOS Easy FS: filesystem propio de solo lectura.
- **PD/PT** — Page Directory / Page Table (paginación x86).
- **CR3** — registro que apunta al PD actual (conmutación de espacio de
  direcciones).
- **TIB** — Thread Information Block del CRT mingw (`%fs:0x18`).