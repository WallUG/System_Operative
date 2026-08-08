/* MyOS - kernel/task/task.h
 * Scheduler round-robin (Fase 5) con soporte de tareas de usuario en
 * ring 3 aisladas por paginacion (Fase 7).
 *
 * Cada tarea tiene su propia pila de kernel (frame del PMM) y su
 * propio marco registers_t (el mismo layout que los stubs de IRQ
 * dejan en la pila). El campo esp apunta a ese marco: el cambio de
 * contexto es "rehacer el epilogo del stub IRQ" (pop ds/es, popad,
 * add esp 8, iret) con la pila de la otra tarea. task_create fabrica
 * un marco falso para que el primer iret salte al punto de entrada.
 *
 * Las tareas de usuario (task_create_user/task_fork) llevan ademas:
 *  - cr3: su PD (kernel identity supervisor + paginas USER 4 KiB en
 *    0x80000000-0xBFFFFFFF); switch.asm lo carga antes del iret.
 *  - esp0: tope de su pila de kernel, que se instala en el TSS antes
 *    de programarla (gdt_set_esp0) para que la CPU la use al
 *    interrumpirla/syscall desde ring 3.
 *  - (la pila de usuario se mapea siempre en USER_ESP0_TOP - PAGE_SIZE). */

#ifndef MYOS_TASK_H
#define MYOS_TASK_H

#include <stdint.h>
#include "idt.h"
#include "mem/paging.h"

#define TASK_NAME_LEN   8
#define MAX_TASKS       16

/* Estado de la tarea. TASK_FREE = hueco reutilizable (muertas). */
#define TASK_FREE       0
#define TASK_READY      1
#define TASK_RUNNING    2
#define TASK_DEAD       3

typedef struct task {
    uint32_t esp;               /* puntero al marco registers_t en su pila */
    uint32_t pid;
    uint32_t state;
    uint32_t stack_base;        /* base de su pila de kernel (frame PMM) */
    uint32_t esp0;              /* tope de la pila de kernel (TSS), user */
    uint32_t cr3;               /* PD (kernel_pd para tareas ring 0) */
    char     name[TASK_NAME_LEN];
    struct task *next;          /* lista circular (round-robin) */
} task_t;

/* Inicializa el scheduler con la tarea "idle" (el contexto actual). */
void sched_init(void);
/* Activa el cambio de contexto en el IRQ0 (tras crear las tareas). */
void sched_start(void);
/* Crea una tarea de kernel (ring 0) con pila propia; usa el PD global. */
int  task_create(const char *name, void (*entry)(void));
/* Crea una tarea de usuario: pd es su PD (paginacion USER preparada),
 * el entry debe ser su direccion de entrada (ya mapeada en pd).
 * Asigna y mapea una pila de usuario nueva en USER_ESP0_TOP-PAGE_SIZE.
 * El entry debe terminar con SYS_EXIT (si retorna, task_stub_exit). */
int  task_create_user(const char *name, uint32_t pd, uint32_t entry);
/* Fork: copia el espacio de usuario (paginacion) del padre y el marco
 * registers_t (hijo reanuda tras int 0x80 con eax=0). Devuelve el pid
 * del hijo, -1 si falla. Solo valido desde una tarea de usuario. */
int  task_fork(registers_t *regs);
/* Mata la tarea actual: la quita de la lista y libera su espacio de
 * usuario (PD + paginas + PTs). No libera su pila de kernel (aun). */
void sched_kill_current(void);
/* Numero de tareas vivas (incluida idle). */
uint32_t sched_task_count(void);
/* Nombre/pid/cr3 de la tarea actual. */
uint32_t sched_current_pid(void);
const char *sched_current_name(void);
uint32_t sched_current_cr3(void);
/* Llamado desde el handler IRQ0 (timer): guarda el marco actual y
 * restaura el de la siguiente tarea de la lista circular. Retorna solo
 * cuando la multitarea no esta activa (vuelve al epilogo del stub). */
void sched_tick(registers_t *regs);

#endif
