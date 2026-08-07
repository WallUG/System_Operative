/* MyOS - kernel/task/task.h
 * Scheduler round-robin simple (Fase 5).
 *
 * Cada tarea tiene su propia pila de kernel y su propio marco registers_t
 * (el mismo layout que los stubs de IRQ dejan en la pila). El campo esp
 * apunta a ese marco: el cambio de contexto es "rehacer el epilogo del
 * stub IRQ" (pop ds/es, popad, add esp 8, iret) con la pila de la otra
 * tarea. task_create fabrica un marco falso para que el primer iret
 * salte directo al punto de entrada de la tarea. */

#ifndef MYOS_TASK_H
#define MYOS_TASK_H

#include <stdint.h>
#include "idt.h"

#define TASK_NAME_LEN   8
#define MAX_TASKS       16

/* Estado de la tarea (por ahora solo listo/corriendo: sin bloqueo). */
#define TASK_READY      0
#define TASK_RUNNING    1
#define TASK_DEAD       2

typedef struct task {
    uint32_t esp;               /* puntero al marco registers_t en su pila */
    uint32_t pid;
    uint32_t state;
    uint32_t stack_base;        /* base de su pila de kernel (frame PMM) */
    char     name[TASK_NAME_LEN];
    struct task *next;          /* lista circular (round-robin) */
} task_t;

/* Inicializa el scheduler con la tarea "idle" (el contexto actual). */
void sched_init(void);
/* Activa el cambio de contexto en el IRQ0 (tras crear las tareas). */
void sched_start(void);
/* Crea una tarea de kernel con pila propia; arranca al primer schedule. */
int  task_create(const char *name, void (*entry)(void));
/* Numero de tareas vivas (incluida idle). */
uint32_t sched_task_count(void);
/* Nombre/pid de la tarea actual. */
uint32_t sched_current_pid(void);
const char *sched_current_name(void);
/* Llamado desde el handler IRQ0 (timer): guarda el marco actual y
 * restaura el de la siguiente tarea de la lista circular. Retorna solo
 * cuando la multitarea no esta activa (vuelve al epilogo del stub). */
void sched_tick(registers_t *regs);

#endif
