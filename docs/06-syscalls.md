# MyOS — Syscalls (ABI `int 0x80`)

Archivos: `kernel/syscall.c|h`, `kernel/gdt.c` (gate DPL=3), usuarios en
`user/*.c` y `user/win32/*.c`.

## Convención

- **Gate de interrupción DPL=3** (`int 0x80`): IF se limpia al entrar; el
  handler real vuelve a habilitar interrupciones si bloquea.
- Paso de argumentos por registros:
  - `eax` = número de syscall
  - `ebx`, `ecx`, `edx`, `esi` = argumentos
  - Retorno en `eax`
- Los wrappers en C usan `__asm__ volatile("int $0x80" : "=a"(r) : ... )`
  con la salida declarada (`(void)r` en los que no usan el retorno) — sin
  esto, GCC reutilizaba `eax` y la siguiente syscall se despachaba con el
  retorno anterior como número (bug #ABI Fase 11).

## Validación de punteros

Todo puntero de ring 3 se copia por página contra el PD del que llama:
`user_strcpy` / `user_memcpy_out` (kernel→usuario) / `user_memcpy_in`
(usuario→kernel). Un puntero inválido devuelve -1/0, **nunca** produce
#PF de kernel.

## Tabla de syscalls

| Nº | Nombre | Argumentos | Descripción |
|----|--------|-----------|-------------|
| 1 | `SYS_PRINT` | `ebx=char*` | Cadena NUL-terminada al serial (consola antigua) |
| 2 | `SYS_EXIT` | — | Termina la tarea actual (`sched_kill_current`) |
| 3 | `SYS_FORK` | — | Clona la tarea; hijo: `eax=0` |
| 4 | `SYS_EXEC` | `ebx=nombre` | Carga ELF/PE y reemplaza la imagen; **flush TLB** al final |
| 5 | `SYS_GETPID` | — | PID de la tarea |
| 6 | `SYS_READ` | `ebx=buf, ecx=max` | Consola: lee chars (bloquea con `sti;halt;cli`) |
| 7 | `SYS_WRITE` | `ebx=buf, ecx=len` | Consola: bytes exactos (serial + vgafx si no hay ventanas) |
| 8 | `SYS_FSIZE` | `ebx=nombre` | Tamaño de archivo o -1 |
| 9 | `SYS_FREAD` | `ebx=nom, ecx=buf, edx=input` | Lee archivo completo |
| 10 | `SYS_MALLOC` | `ebx=bytes` | Heap de usuario (bump) → VA o 0 |
| 11 | `SYS_FREE` | `ebx=ptr` | No-op por ahora (heap bump) |
| 12 | `SYS_DREAD` | `ebx=nom, ecx=buf, edx=off, esi=max` | Lectura posicional |
| 13 | `SYS_DLIST` | `ebx=idx, ecx=name[16], edx=&size` | Enumerar directorio |
| 14 | `SYS_SELFNAME` | `ebx=buf, ecx=max` | Nombre del ejecutable actual |
| 15 | `SYS_GFXINFO` | `ebx=&{lfb_va,w,h,bpp}` | Info del modo gráfico |
| 16 | `SYS_MOUSEINFO` | `ebx=&{x,y,buttons}` | Posición/botones del ratón |
| 17 | `SYS_EVENT` | `ebx=&{type,x,y,buttons,key}` | Dequeue de la cola de la propia app (-1 si vacía) |
| 18 | `SYS_WINCREATE` | `ebx=&{title*,x,y,w,h,buf_va,buf_sz,flags}` | Crea ventana → id |
| 19 | `SYS_WINCLOSE` | `ebx=id` | Cierra (validando dueño) |
| 20 | `SYS_WINMOVE` | `ebx=id, ecx=dx, edx=dy` | Mueve ventana |
| 21 | `SYS_WINUPDATE` | `ebx=id` | Recompone la pantalla |
| 22 | `SYS_WININFO` | `ebx=id, ecx=&{x,y,w,h,cx,cy,cw,ch}` | Geometría |
| 23 | `SYS_WINTITLE` | `ebx=id, ecx=title*` | Cambia el título de la barra |
| 24 | `SYS_EXEBASE` | — | Base `ImageBase` del PE actual (0 si ELF) |
| 25 | `SYS_MENUBAR` | `ebx=id, ecx=on, edx=flat*` | Activa la barra de menú (Fase D): `flat` = labels top-level NUL-separados |
| 26 | `SYS_FCREATE` | `ebx=nombre` | Crea un archivo vacío (Fase E) |
| 27 | `SYS_FWRITE` | `ebx=nombre, ecx=buf, edx=len` | Sobrescribe un archivo |
| 28 | `SYS_FDELETE` | `ebx=nombre` | Elimina un archivo |
| 29 | `SYS_FLUSH` | — | Persiste superbloque + directorio al disco |

Las syscalls 26-29 (Fase E) solo tienen efecto en modo **disco ATA**
(`mefs_writable()`); en CD/RAM devuelven -1 o no-op. Copian los buffers de
usuario con `user_memcpy_in` antes de tocar el FS.

## SYS_EVENT y el enrutado por PD (Fases 14-17)

- El kernel genera eventos: `EV_MOVE` 1, `EV_BUTTON_DOWN` 2,
  `EV_BUTTON_UP` 3, `EV_KEY` 4, `EV_WINCLOSE` 5 (botón X).
- Fase 14: cola global única (un consumidor).
- Fase 17: `wm_route` enruta cada evento a la **cola FIFO de la app
  dueña** (`wm_client_t`, 4 slots × 64 eventos). `SYS_EVENT` reclama solo
  su propia cola (`wm_event_claim(pd)`).
- Enrutado:
  - `EV_KEY` → `wm_topmost_app()` (excluye taskbar FIXED y fondo BG) →
    refresca `wm_focus_pd`.
  - `EV_BUTTON_*`/`EV_MOVE` → dueño de la ventana bajo el cursor o foco;
    drag en el título consumido por el WM.
  - Sin ventanas → `WM_ROUTE_RAW` (el evento va al llamador).

## Reglas de escritura de código

- **Nunca** kprint/printf en handlers de IRQ (solo puertos + aritmética).
- Los bloqueos deben ceder la CPU o re-habilitar IF.
- Los números de syscall en shims de DLL (`SYS_*` en `user/win32/*.c`)
  deben coincidir con `kernel/syscall.h` (bug histórico: `SYS_MALLOC` 10
  vs 11 → malloc NULL → abort del CRT).