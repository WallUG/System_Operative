/* MyOS - kernel/task/task.c
 * Scheduler round-robin sobre el IRQ0 (PIT, Fase 5) + tareas de
 * usuario aisladas por paginacion (Fase 7).
 *
 * sched_tick() se invoca desde el handler del timer con el puntero al
 * marco registers_t (tal como lo deja irq_common_stub en la pila de la
 * tarea actual). Guarda ese puntero en task->esp, instala el esp0 del
 * TSS si la entrante es de usuario y restaura el de la siguiente tarea
 * (sched_switch carga tambien su CR3). El resto lo restaura el iret.
 *
 * Las tareas muertas se quitan de la lista y su slot queda libre para
 * reutilizar; su espacio de usuario se libera al morir (PD + paginas +
 * PTs). La pila de kernel del slot no se libera (un frame como mucho
 * por slot, MAX_TASKS fija). */

#include <stdint.h>
#include <string.h>
#include "task.h"
#include "mem/pmm.h"
#include "gdt.h"
#include "win32.h"
#include "winmgr.h"
#include "kprint.h"

/* Implementado en kernel/task/switch.asm. No retorna (iret). */
extern void sched_switch(uint32_t *old_esp, uint32_t new_esp, uint32_t frame,
                         uint32_t new_cr3);
extern void task_stub_exit(void);

static task_t tasks[MAX_TASKS];
static task_t *current;
static uint32_t next_pid = 1;       /* 0 es la tarea idle */
static uint32_t task_count;

/* Solo el IRQ0 dispara el scheduler; la tarea idle corre el resto. */
static volatile uint32_t sched_enabled = 0;

static void list_insert(task_t *t)
{
    t->next = current->next;
    current->next = t;
}

static void list_remove(task_t *t)
{
    task_t *p = t->next;

    while (p->next != t)
        p = p->next;
    p->next = t->next;
}

static task_t *free_slot(void)
{
    int i;

    for (i = 1; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_FREE)
            return &tasks[i];
    }
    return NULL;
}

void sched_init(void)
{
    task_t *idle = &tasks[0];
    int i;

    for (i = 0; i < MAX_TASKS; i++)
        tasks[i].state = TASK_FREE;

    idle->esp = 0;
    idle->pid = 0;
    idle->state = TASK_RUNNING;
    idle->stack_base = 0;
    idle->esp0 = 0;
    idle->cr3 = paging_kernel_pd();
    idle->name[0] = 'i'; idle->name[1] = 'd'; idle->name[2] = 'l';
    idle->name[3] = 'e'; idle->name[4] = 0;
    idle->next = idle;              /* lista circular de 1 tarea */

    current = idle;
    task_count = 1;
}

static int task_init(task_t *t, const char *name)
{
    int i;

    t->pid = next_pid++;
    t->state = TASK_READY;
    for (i = 0; name[i] && i < TASK_NAME_LEN - 1; i++)
        t->name[i] = name[i];
    t->name[i] = 0;
    list_insert(t);
    task_count++;
    return t->pid;
}

int task_create(const char *name, void (*entry)(void))
{
    uint32_t stack_frame;
    uint32_t *sp;
    task_t *t;

    t = free_slot();
    if (t == NULL)
        return -1;

    stack_frame = pmm_alloc_frame();
    if (stack_frame == 0)
        return -1;                  /* PMM agotado */

    /* Marco registers_t falso en el tope de la pila nueva (kernel):
     * ds, es, pusha (8 dwords), int_no, err_code y el marco de la CPU
     * (eip, cs, eflags). El epilogo del stub: pop ds, pop es, popad,
     * add esp 8, iret -> salta al entry de la tarea. */
    sp = (uint32_t *)(stack_frame + 0x1000);
    sp -= 16;

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

    t->esp = (uint32_t)sp;
    t->stack_base = stack_frame;
    t->esp0 = 0;
    t->cr3 = paging_kernel_pd();

    return task_init(t, name);
}

int task_create_user(const char *name, const char *exe, const char *cmdline,
                     uint32_t pd, uint32_t entry, uint32_t exe_base)
{
    uint32_t stack_frame;
    uint32_t *sp;
    task_t *t;
    uint32_t i;

    t = free_slot();
    if (t == NULL)
        return -1;

    stack_frame = pmm_alloc_frame();
    if (stack_frame == 0)
        return -1;

    for (i = 0; exe[i] && i < TASK_EXE_LEN - 1; i++)
        t->exe_name[i] = exe[i];
    t->exe_name[i] = 0;

    /* Pila de usuario: USER_STACK_SIZE (64 KiB) mapeada como USER en el
     * tope de la region de usuario (los CRTs de Windows consumen pila).
     * El contenido no importa. */
    if (paging_user_map(pd, USER_ESP0_TOP - USER_STACK_SIZE,
                        USER_STACK_SIZE) != 0) {
        pmm_free_frame(stack_frame);
        return -1;
    }
    /* Return address de arranque del CRT (ver win32_crt_ret_init). */
    win32_crt_ret_init(pd);
    /* Linea de comandos real (GetCommandLineA/__getmainargs). */
    win32_tib_set_cmdline(pd, cmdline);

    /* Marco falso para ring 3: el iret restaura tambien esp/ss de
     * usuario (la CPU pushea 5 dwords al interrumpir desde ring 3). */
    sp = (uint32_t *)(stack_frame + 0x1000);
    sp -= 18;                       /* 16 del marco + esp/ss al final */

    sp[0]  = 0x23;                  /* ds (ring 3)       */
    sp[1]  = 0x23;                  /* es                */
    sp[2]  = 0;                     /* edi               */
    sp[3]  = 0;                     /* esi               */
    sp[4]  = 0;                     /* ebp               */
    sp[5]  = USER_ESP0_INIT;        /* esp (pusha)       */
    sp[6]  = 0;                     /* ebx               */
    sp[7]  = 0;                     /* edx               */
    sp[8]  = 0;                     /* ecx               */
    sp[9]  = 0;                     /* eax               */
    sp[10] = 0;                     /* int_no            */
    sp[11] = 0;                     /* err_code          */
    sp[12] = entry;                 /* eip (user code)   */
    sp[13] = 0x1B;                  /* cs con RPL=3      */
    sp[14] = 0x202;                 /* eflags: IF=1      */
    sp[15] = USER_ESP0_INIT;        /* user esp          */
    sp[16] = 0x23;                  /* user ss con RPL=3 */

    t->esp = (uint32_t)sp;
    t->stack_base = stack_frame;
    t->esp0 = stack_frame + 0x1000;
    t->cr3 = pd;
    t->heap_cur = USER_HEAP_BASE;
    t->heap_head = 0;               /* Fase 23-C10: heap por proceso */
    t->exe_base = exe_base;

    return task_init(t, name);
}

int task_fork(registers_t *regs)
{
    registers_t *cf;
    uint32_t pd, stack_frame;
    task_t *t;

    if (current == NULL || current->cr3 == paging_kernel_pd())
        return -1;                  /* fork solo desde tareas de usuario */
    t = free_slot();
    if (t == NULL)
        return -1;

    pd = paging_create_user_pd();
    if (pd == 0)
        return -1;
    if (paging_copy_user_space(pd, current->cr3) != 0) {
        paging_free_user_space(pd);
        paging_free_pd(pd);
        return -1;
    }
    stack_frame = pmm_alloc_frame();
    if (stack_frame == 0) {
        paging_free_user_space(pd);
        paging_free_pd(pd);
        return -1;
    }

    /* El hijo reanuda tras int 0x80 con eax=0: marco identico al del
     * padre, pero el retorno de fork() en el hijo es 0. */
    cf = (registers_t *)(stack_frame + 0x1000);
    cf--;
    *cf = *regs;
    cf->eax = 0;

    t->esp = (uint32_t)cf;
    t->stack_base = stack_frame;
    t->esp0 = stack_frame + 0x1000;
    t->cr3 = pd;
    t->heap_cur = current->heap_cur;
    t->heap_head = current->heap_head;   /* el hijo hereda los bloques */

    return task_init(t, "user");
}

void sched_kill_current(void)
{
    if (current == NULL)
        return;
    if (current->cr3 != 0 && current->cr3 != paging_kernel_pd()) {
        /* Fase 17: la tarea muere -> el WM retira sus ventanas, libera
         * su cola de eventos y suelta el fondo si era la ultima. Debe
         * correr ANTES de liberar el PD (aun valido para comprobar
         * duenos y recomponer). */
        wm_cleanup_pd(current->cr3);
        /* Liberar PD + espacio de usuario. CR3 sigue apuntando al PD
         * mientras la tarea termina en kernel (identity map intacto);
         * el proximo sched_switch carga otro PD. */
        paging_free_user_space(current->cr3);
        paging_free_pd(current->cr3);
        current->cr3 = 0;
    }
    list_remove(current);
    current->state = TASK_FREE;
    task_count--;
    /* La tarea muerta queda fuera de la lista pero current sigue
     * apuntandole (el iret la deja en el bucle task_stub_exit). El
     * proximo sched_tick detecta current->state == TASK_FREE y rota a
     * la siguiente tarea viva (la idle -> la shell reanuda). */
}

void sched_tick(registers_t *regs)
{
    task_t *prev;

    /* Sin multitarea: si la tarea actual esta viva, volver al epilogo
     * normal (iret con el marco actual). Si esta muerta (la mato
     * sys_exit/#PF y quedo en el bucle task_stub_exit), rotar a la
     * siguiente tarea lista: si solo queda la idle, es ella. */
    if (!sched_enabled)
        return;
    if (task_count < 2) {
        if (current->state == TASK_READY || current->state == TASK_RUNNING)
            return;
    }

    prev = current;
    do {
        current = current->next;
    } while (current->state != TASK_READY && current->state != TASK_RUNNING
             && current != prev);

    /* Tarea de usuario entrante: su pila de kernel debe ser el esp0
     * del TSS antes de volver a ring 3 (la CPU la usa en la proxima
     * interrupcion/syscall). */
    if (current->esp0 != 0)
        gdt_set_esp0(current->esp0);

    /* Guardar el marco actual y saltar al de la siguiente tarea. */
    sched_switch(&prev->esp, current->esp, (uint32_t)regs, current->cr3);
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

/* 1 si alguna tarea de usuario (PD propio, distinta de la idle) esta
 * READY/RUNNING. La shell (tarea idle) la usa para no robar la entrada
 * de consola mientras un programa de usuario corre. Las tareas demo de
 * kernel (T-A/T-B) no cuentan: comparten el PD del kernel. */
int sched_user_busy(void)
{
    task_t *t;
    uint32_t kpd = paging_kernel_pd();

    if (!sched_enabled)
        return 0;
    t = current;
    do {
        if (t->cr3 != kpd &&
            (t->state == TASK_READY || t->state == TASK_RUNNING))
            return 1;
        t = t->next;
    } while (t != current);
    return 0;
}

uint32_t sched_current_pid(void)
{
    return current ? current->pid : 0;
}

const char *sched_current_name(void)
{
    return current ? current->name : "?";
}

uint32_t sched_current_cr3(void)
{
    return current ? current->cr3 : 0;
}

uint32_t sched_user_heap(void)
{
    return current ? current->heap_cur : 0;
}

void sched_get_exe_name(char *dst, uint32_t max)
{
    uint32_t i;

    if (dst == NULL || max == 0)
        return;
    for (i = 0; current && i < max - 1 && current->exe_name[i]; i++)
        dst[i] = current->exe_name[i];
    dst[i] = 0;
}

void sched_set_exe_name(const char *name)
{
    uint32_t i;

    if (current == NULL)
        return;
    for (i = 0; name[i] && i < TASK_EXE_LEN - 1; i++)
        current->exe_name[i] = name[i];
    current->exe_name[i] = 0;
}

uint32_t sched_current_exe_base(void)
{
    return current ? current->exe_base : 0;
}

void sched_set_exe_base(uint32_t base)
{
    if (current)
        current->exe_base = base;
}

void sched_user_heap_set(uint32_t p)
{
    if (current)
        current->heap_cur = p;
}

uint32_t sched_user_heap_head(void)
{
    return current ? current->heap_head : 0;
}

void sched_user_heap_head_set(uint32_t p)
{
    if (current)
        current->heap_head = p;
}
