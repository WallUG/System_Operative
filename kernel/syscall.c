/* MyOS - kernel/syscall.c
 * Syscalls via int 0x80 (gate con DPL=3, Fase 6) con validacion de
 * memoria de usuario (Fase 7).
 * Convencion i386: eax = numero, ebx/ecx/edx = argumentos.
 *
 * La memoria de usuario solo es accesible por su direccion virtual en
 * el PD de la tarea actual (el kernel corre bajo ese PD), asi que los
 * punteros de ring 3 se copian por paginas validadas con paging_is_user:
 * un puntero invalido nunca produce un #PF de kernel.
 *
 * SYS_EXIT y el fallo de exec matan la tarea: sched_kill_current libera
 * su espacio de usuario y la quita de la lista, y el handler enruta el
 * iret a task_stub_exit (bucle sti;hlt en ring 0: el siguiente tick del
 * PIT despierta la tarea y el scheduler la salta para siempre). */

#include <stdint.h>
#include <string.h>
#include "syscall.h"
#include "idt.h"
#include "kprint.h"
#include "task/task.h"
#include "mem/paging.h"
#include "mem/heap.h"
#include "elf.h"
#include "fs/mefs.h"

#define IDT_GATE_INT_DPL3 0xEE      /* presente, DPL=3, 32-bit int gate */

extern void isr128(void);           /* stub en kernel/isr.asm */
extern void task_stub_exit(void);

void syscall_init(void)
{
    /* Registra el gate 0x80 con DPL=3 para que ring 3 pueda int 0x80.
     * idt_set_gate es estatico en idt.c; se expone via esta funcion. */
    idt_register_dpl3(0x80, (uint32_t)&isr128);
}

/* Copia una cadena de usuario a un buffer del kernel, validando cada
 * pagina con paging_is_user (pd de la tarea actual). -1 si invalida o
 * demasiado larga. */
static int user_strcpy(char *dst, uint32_t dst_sz, const char *src,
                       uint32_t pd)
{
    uint32_t i;

    if (dst_sz == 0)
        return -1;
    for (i = 0; i < dst_sz - 1; i++) {
        uint32_t a = (uint32_t)src + i;
        if (a >= USER_VADDR_END || !paging_is_user(pd, a))
            return -1;
        dst[i] = *(const char *)a;
        if (dst[i] == 0)
            return 0;
    }
    return -1;
}

static void exit_current(registers_t *regs)
{
    sched_kill_current();
    /* No volver a ring 3: el epilogo del stub hara iret a task_stub_exit
     * en ring 0 (el marco lleva cs=0x08; sobra user_esp/user_ss). */
    regs->eip = (uint32_t)task_stub_exit;
    regs->cs = 0x08;
    regs->eflags = 0x202;
    regs->eax = 0;
}

static void sys_exec(const char *name, registers_t *regs)
{
    void *buf;
    uint32_t size, entry;

    size = (uint32_t)mefs_size(name);
    if (size == 0 || size > 0x100000) {
        regs->eax = -1;
        return;
    }
    buf = kmalloc(size);
    if (buf == NULL) {
        regs->eax = -1;
        return;
    }
    if (mefs_read(name, buf, size) != (int)size) {
        kfree(buf);
        regs->eax = -1;
        return;
    }
    if (elf_load_into(sched_current_cr3(), buf, size, &entry) != 0) {
        /* El ELF es invalido y el espacio de usuario anterior ya se
         * libero: la tarea no puede seguir -> se mata. */
        kfree(buf);
        exit_current(regs);
        return;
    }
    kfree(buf);

    /* Remapear la pila de usuario (elf_load_into libero el espacio) y
     * reiniciar esp: el programa nuevo arranca con la pila vacia. */
    paging_user_map(sched_current_cr3(), USER_ESP0_TOP - PAGE_SIZE, PAGE_SIZE);
    regs->esp = USER_ESP0_TOP;
    regs->user_esp = USER_ESP0_TOP;

    /* Continuar en el nuevo entry; eax=0 "exito" (nunca se usa). */
    regs->eip = entry;
    regs->eax = 0;
}

void syscall_handler(registers_t *regs)
{
    uint32_t pd = sched_current_cr3();

    switch (regs->eax) {
    case SYS_PRINT: {
        /* Copia validada a buffer del kernel: un puntero de ring 3
         * basura no puede hacer fallar al kernel. */
        char buf[256];
        if (user_strcpy(buf, sizeof(buf), (const char *)regs->ebx, pd) == 0)
            kprint(buf);
        break;
    }
    case SYS_EXIT:
        exit_current(regs);
        break;
    case SYS_FORK:
        regs->eax = (uint32_t)task_fork(regs);
        break;
    case SYS_EXEC: {
        char name[32];
        if (user_strcpy(name, sizeof(name), (const char *)regs->ebx, pd) == 0)
            sys_exec(name, regs);
        else
            regs->eax = -1;
        break;
    }
    case SYS_GETPID:
        regs->eax = sched_current_pid();
        break;
    default:
        break;
    }
}
