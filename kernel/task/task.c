/* MyOS - kernel/task/task.c
 * Scheduler round-robin simple sobre el IRQ0 (PIT, Fase 5).
 *
 * sched_tick() se invoca desde el handler del timer con el puntero al
 * marco registers_t (tal como lo deja irq_common_stub en la pila de la
 * tarea actual). Guarda ese puntero en task->esp y restaura el de la
 * siguiente tarea. El resto del estado lo restaura el propio iret.
 *
 * Las tareas son de kernel (ring 0), comparten el espacio de direcciones
 * identity 0-1 GiB de la Fase 4 y cada una tiene su pila en un frame
 * del PMM. MAX_TASKS entradas en una tabla estatica (los stack frames
 * no se liberan aun: sin kfree de stacks, no hay TASK_DEAD real). */

#include <stdint.h>
#include "task.h"
#include "mem/pmm.h"
#include "kprint.h"

/* Implementado en kernel/task/switch.asm. No retorna (iret). */
extern void sched_switch(uint32_t *old_esp, uint32_t new_esp, uint32_t frame);
extern void task_stub_exit(void);

static task_t tasks[MAX_TASKS];
static task_t *current;
static uint32_t next_pid = 1;       /* 0 es la tarea idle */
static uint32_t task_count;

/* Solo el IRQ0 dispara el scheduler; la tarea idle corre el resto. */
static volatile uint32_t sched_enabled = 0;

void sched_init(void)
{
    task_t *idle = &tasks[0];

    idle->esp = 0;
    idle->pid = 0;
    idle->state = TASK_RUNNING;
    idle->stack_base = 0;
    idle->name[0] = 'i'; idle->name[1] = 'd'; idle->name[2] = 'l';
    idle->name[3] = 'e'; idle->name[4] = 0;
    idle->next = idle;              /* lista circular de 1 tarea */

    current = idle;
    task_count = 1;
}

int task_create(const char *name, void (*entry)(void))
{
    uint32_t stack_frame;
    uint32_t *sp;
    task_t *t;
    int i;

    if (task_count >= MAX_TASKS)
        return -1;

    stack_frame = pmm_alloc_frame();
    if (stack_frame == 0)
        return -1;                  /* PMM agotado */

    /* Fabricar un marco registers_t falso en el tope de la pila nueva:
     * ds, es (push es; push ds), pusha (8 dwords), int_no, err_code y
     * el marco de la CPU (eip, cs, eflags). El epilogo del stub:
     *   pop ds, pop es, popad, add esp 8, iret
     * deja esp justo en eip -> salta al entry de la tarea. */
    sp = (uint32_t *)(stack_frame + 0x1000);    /* tope del frame */
    sp -= 16;                                   /* 16 dwords del marco */

    sp[0]  = 0x10;                  /* ds   (pop ds)   */
    sp[1]  = 0x10;                  /* es   (pop es)   */
    sp[2]  = 0;                     /* edi  (popad)    */
    sp[3]  = 0;                     /* esi              */
    sp[4]  = 0;                     /* ebp              */
    sp[5]  = stack_frame + 0x1000;  /* esp (valor coherente) */
    sp[6]  = 0;                     /* ebx              */
    sp[7]  = 0;                     /* edx              */
    sp[8]  = 0;                     /* ecx              */
    sp[9]  = 0;                     /* eax              */
    sp[10] = 0;                     /* int_no           */
    sp[11] = 0;                     /* err_code         */
    sp[12] = (uint32_t)entry;       /* eip (iret)       */
    sp[13] = 0x08;                  /* cs               */
    sp[14] = 0x202;                 /* eflags: IF=1     */

    t = &tasks[task_count];
    t->esp = (uint32_t)sp;
    t->pid = next_pid++;
    t->state = TASK_READY;
    t->stack_base = stack_frame;
    for (i = 0; name[i] && i < TASK_NAME_LEN - 1; i++)
        t->name[i] = name[i];
    t->name[i] = 0;

    /* Insertar al final de la lista circular (antes de la idle). */
    t->next = current->next;
    current->next = t;

    task_count++;
    return t->pid;
}

void sched_tick(registers_t *regs)
{
    task_t *prev;

    if (!sched_enabled || task_count < 2) {
        /* Sin multitarea: volver al epilogo normal del stub (iret
         * con el marco actual). Equivale a no hacer nada. */
        return;
    }

    prev = current;
    current = current->next;
    /* Guardar el marco actual y saltar al de la siguiente tarea. */
    sched_switch(&prev->esp, current->esp, (uint32_t)regs);
}

/* La idle arranca en kmain; sched_enabled se activa tras crear las
 * tareas de prueba. */
void sched_start(void)
{
    sched_enabled = 1;
}

uint32_t sched_task_count(void)
{
    return task_count;
}

uint32_t sched_current_pid(void)
{
    return current ? current->pid : 0;
}

const char *sched_current_name(void)
{
    return current ? current->name : "?";
}
