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
#include "io.h"
#include "syscall.h"
#include "idt.h"
#include "kprint.h"
#include "task/task.h"
#include "mem/paging.h"
#include "mem/heap.h"
#include "elf.h"
#include "pe.h"
#include "win32.h"
#include "drivers/serial.h"
#include "drivers/keyboard.h"
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

/* Copia bytes del kernel a memoria de usuario (validando paginas). */
static int user_memcpy_out(void *dst, const void *src, uint32_t n,
                           uint32_t pd)
{
    const uint8_t *s = (const uint8_t *)src;
    uint32_t a, i;

    for (i = 0; i < n; i++) {
        a = (uint32_t)dst + i;
        if (a >= USER_VADDR_END || !paging_is_user(pd, a))
            return -1;
        *(uint8_t *)a = s[i];
    }
    return 0;
}

static void exit_current(registers_t *regs)
{
    kprint("exit:");
    kprint_uint(regs->ebx);
    kprint("\n");
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
    int is_pe, r;

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
    is_pe = (size >= 2 && ((uint8_t *)buf)[0] == 'M'
             && ((uint8_t *)buf)[1] == 'Z');
    r = is_pe ? pe_load_into(sched_current_cr3(), buf, size, &entry)
              : elf_load_into(sched_current_cr3(), buf, size, &entry);
    if (r != 0) {
        /* El ELF es invalido y el espacio de usuario anterior ya se
         * libero: la tarea no puede seguir -> se mata. */
        kfree(buf);
        exit_current(regs);
        return;
    }
    kfree(buf);

    /* Remapear los modulos Win32 fijos y la pila de usuario
     * (elf_load_into libero el espacio) y reiniciar esp: el programa
     * arranca con la pila vacia. */
    win32_map_all(sched_current_cr3());
    paging_user_map(sched_current_cr3(), USER_ESP0_TOP - USER_STACK_SIZE,
                    USER_STACK_SIZE);
    win32_crt_ret_init(sched_current_cr3());
    regs->esp = USER_ESP0_INIT;
    regs->user_esp = USER_ESP0_INIT;
    sched_user_heap_set(USER_HEAP_BASE);    /* heap nuevo (bump en 0) */

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
    case SYS_READ: {
        /* Lee una linea desde teclado o serial (igual que el shell).
         * ecx = tamano maximo; devuelve el numero de chars leidos
         * (sin el '\n'). Bloquea con halt() hasta entrada nueva. */
        char *buf = (char *)regs->ebx;
        uint32_t max = regs->ecx;
        uint32_t n = 0;
        char echo[2];
        if (max == 0) {
            regs->eax = 0;
            break;
        }
        for (;;) {
            int c = keyboard_read();
            if (c < 0)
                c = serial_read_char();
            if (c < 0) {
                halt();
                continue;
            }
            if (c == '\n' || c == '\r')
                break;
            if (n >= max - 1)      /* desbordar: descartar, no pasarse */
                continue;
            if (!paging_is_user(pd, (uint32_t)buf + n)) {
                regs->eax = -1;
                return;
            }
            buf[n++] = (char)c;
            echo[0] = (char)c;
            echo[1] = 0;
            kprint(echo);           /* echo de consola */
        }
        buf[n] = 0;
        regs->eax = (int32_t)n;
        break;
    }
    case SYS_WRITE: { /* escribe len bytes exactos (sin \0 obligatorio) */
        const uint8_t *s = (const uint8_t *)regs->ebx;
        uint32_t len = regs->ecx;
        uint32_t i, a;
        char cbuf[2];
        if (len > 4096) {
            regs->eax = -1;
            break;
        }
        for (i = 0; i < len; i++) {
            a = (uint32_t)s + i;
            if (a >= USER_VADDR_END || !paging_is_user(pd, a)) {
                regs->eax = -1;
                return;
            }
            cbuf[0] = *(const char *)a;
            cbuf[1] = 0;
            kprint(cbuf);
        }
        regs->eax = (int32_t)len;
        break;
    }
    case SYS_FSIZE: {
        char name[32];
        if (user_strcpy(name, sizeof(name), (const char *)regs->ebx, pd) == 0)
            regs->eax = (int32_t)mefs_size(name);
        else
            regs->eax = -1;
        break;
    }
    case SYS_FREAD: { /* ebx=nombre, ecx=dest buffer, edx=input: bytes */
        char name[32];
        char *dest = (char *)regs->ecx;
        uint32_t len = regs->edx;
        void *tmp;
        int32_t got;
        if (user_strcpy(name, sizeof(name), (const char *)regs->ebx, pd) != 0
            || len > 0x100000) {
            regs->eax = -1;
            break;
        }
        tmp = kmalloc(len ? len : 1);
        if (tmp == NULL) {
            regs->eax = -1;
            break;
        }
        got = (int32_t)mefs_read(name, tmp, len);
        if (got < 0) {
            kfree(tmp);
            regs->eax = -1;
            break;
        }
        if (user_memcpy_out(dest, tmp, (uint32_t)got, pd) != 0)
            regs->eax = -1;
        else
            regs->eax = got;
        kfree(tmp);
        break;
    }
    case SYS_MALLOC: {
        /* bump allocator sobre [USER_HEAP_BASE, USER_HEAP_END) con
         * paginas USER mapeadas bajo demanda. Sin free (SYS_FREE no-op),
         * pero suficiente para consola/archivos por ahora. */
        uint32_t size = regs->ebx;
        uint32_t cur = sched_user_heap();
        uint32_t next;
        if (size == 0)
            size = 16;
        next = PAGE_ALIGN(cur + size);
        if (next > USER_HEAP_END) {
            regs->eax = 0;
            break;
        }
        if (paging_is_user(pd, cur) == 0 &&
            paging_user_map(pd, cur, next - cur) != 0) {
            regs->eax = 0;
            break;
        }
        sched_user_heap_set(next);
        regs->eax = cur;
        break;
    }
    case SYS_FREE:
        regs->eax = 0;
        break;
    case SYS_DREAD: { /* ebx=nombre, ecx=buf, edx=off, esi=max: lectura
                       * posicional (soporte ReadFile de Win32) */
        char name[32];
        char *dest = (char *)regs->ecx;
        uint32_t off = regs->edx, len = regs->esi;
        void *tmp;
        int32_t got;
        if (user_strcpy(name, sizeof(name), (const char *)regs->ebx, pd) != 0
            || len > 0x100000) {
            regs->eax = -1;
            break;
        }
        tmp = kmalloc(len ? len : 1);
        if (tmp == NULL) {
            regs->eax = -1;
            break;
        }
        got = (int32_t)mefs_read_off(name, tmp, off, len);
        if (got < 0) {
            kfree(tmp);
            regs->eax = -1;
            break;
        }
        if (user_memcpy_out(dest, tmp, (uint32_t)got, pd) != 0)
            regs->eax = -1;
        else
            regs->eax = got;
        kfree(tmp);
        break;
    }
    case SYS_DLIST: { /* ebx=idx dir, ecx=name[16], edx=&size: entrada
                       * del directorio (FindFirstFile/FindNextFile) */
        uint32_t idx = regs->ebx;
        char *name = (char *)regs->ecx;
        uint32_t *size = (uint32_t *)regs->edx;
        char tmp_name[16];
        uint32_t tmp_size;
        if (mefs_dir_get(idx, tmp_name, &tmp_size) != 0) {
            regs->eax = -1;         /* no hay mas entradas */
            break;
        }
        if (user_memcpy_out(name, tmp_name, 16, pd) != 0 ||
            user_memcpy_out(size, &tmp_size, 4, pd) != 0) {
            regs->eax = -1;
            break;
        }
        regs->eax = 0;
        break;
    }
    default:
        break;
    }
}
