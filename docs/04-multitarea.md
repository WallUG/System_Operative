# MyOS — Multitarea y scheduler

Archivos: `kernel/task/task.c|h`, `kernel/task/switch.asm`, `kernel/isr.c`,
`kernel/isr.asm`, `kernel/gdt.c|h`, `kernel/pic.c|h`, `kernel/drivers/timer.c|h`.

## Modelo

- Scheduler **round-robin preemptivo** sobre el tick del PIT (IRQ0).
- Hasta `MAX_TASKS` (16) tareas; estados: `TASK_FREE`, `TASK_READY`,
  `TASK_RUNNING`, `TASK_DEAD`.
- Cada tarea tiene: nombre (8 char), `exe_name` (32 char, para Win32),
  CR3 (PD propio), `esp` (puntero al marco `registers_t` guardado),
  prioridad implícita por orden, pila de kernel (frame del PMM).

## Interrupciones y context switch

1. El PIT dispara IRQ0 → `irq_common_stub` (isr.asm) pushea el marco
   `registers_t` (ds, es, pusha, int_no, err, eip, cs, eflags) y llama a
   `irq_handler`.
2. `irq_handler` → `sched_tick()`: guarda `task->esp = esp` (puntero al
   marco) del actual, selecciona el siguiente READY y carga su CR3.
3. `switch.asm` guarda/restaura el puntero al marco y ejecuta el **mismo
   epílogo** de `irq_common_stub` (`pop ds/es`, `popad`, `add esp 8`,
   `iret`) — el switch nunca "vuelve" a `irq_handler`.

Clave: `irq_common_stub` restaura el iret completo; `sched_switch` solo
cambia qué marco se restaura. EOI del PIC se envía ANTES de `sched_tick`.

## Tareas de usuario (ring 3)

`task_create_user(name, exe_name, cmdline, pd, entry)`:

- Fabricar un marco falso: `eip = entry`, `cs = 0x1B` (ring 3), `eflags =
  0x202` (IF|bit1), `esp = USER_ESP0_INIT` (0xBFFFFF8).
- `esp0` del TSS = pila de kernel (interrupción desde ring 3 usa la pila
  del kernel, no la de usuario).
- CR3 = PD de usuario aislado (ver 03-memoria.md).

## Syscalls de proceso

| Syscall | Nº | Semántica |
|---------|----|-----------|
| `SYS_FORK` | 3 | Clona la tarea; el hijo ve `eax=0` |
| `SYS_EXEC` | 4 | Reemplaza la imagen; al final **flushea la TLB** (`paging_switch(cr3)`) |
| `SYS_EXIT` | 2 | `sched_kill_current` → `wm_cleanup_pd` (ventanas del muerto) → siguiente tarea |
| `SYS_GETPID` | 5 | PID de la tarea actual |
| `SYS_SELFNAME` | 14 | `exe_name` de la tarea (base de `GetModuleFileNameA`) |

Muerte de una tarea: `sched_kill_current` elimina ventanas del WM
(`wm_cleanup_pd`), libera el PD y el espacio de usuario.

## Bugs históricos

- **GPF en iret**: doble asignación de frames (banda baja no reservada) →
  ver 03-memoria.md.
- **SYS_READ congelaba el sistema**: `halt()` en bucle con IF apagado por
  el gate de interrupción → `sti(); halt(); cli();`.
- **`.bss` del kernel corrupto en CD** → zeroear `.bss` en `_start`.