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
#include "mem/uheap.h"
#include "drivers/timer.h"
#include "elf.h"
#include "pe.h"
#include "win32.h"
#include "drivers/serial.h"
#include "drivers/keyboard.h"
#include "drivers/mouse.h"
#include "drivers/vbe.h"
#include "winmgr.h"
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

/* Copia bytes de memoria de usuario al kernel (validando paginas). */
static int user_memcpy_in(void *dst, const void *src, uint32_t n,
                          uint32_t pd)
{
    const uint8_t *s = (const uint8_t *)src;
    uint8_t *d = (uint8_t *)dst;
    uint32_t a, i;

    for (i = 0; i < n; i++) {
        a = (uint32_t)s + i;
        if (a >= USER_VADDR_END || !paging_is_user(pd, a))
            return -1;
        d[i] = s[i];
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
    uint32_t size, entry, base = 0;
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
    r = is_pe ? pe_load_into(sched_current_cr3(), buf, size, &entry, &base)
              : elf_load_into(sched_current_cr3(), buf, size, &entry);
    if (r != 0) {
        /* El ELF es invalido y el espacio de usuario anterior ya se
         * libero: la tarea no puede seguir -> se mata. */
        kfree(buf);
        exit_current(regs);
        return;
    }
    kfree(buf);
    sched_set_exe_name(name);   /* GetModuleFileNameA usa este nombre */
    sched_set_exe_base(base);

    /* Remapear los modulos Win32 fijos y la pila de usuario
     * (elf_load_into libero el espacio) y reiniciar esp: el programa
     * arranca con la pila vacia. */
    win32_map_all(sched_current_cr3());
    paging_user_map(sched_current_cr3(), USER_ESP0_TOP - USER_STACK_SIZE,
                    USER_STACK_SIZE);
    win32_crt_ret_init(sched_current_cr3());
    win32_tib_set_cmdline(sched_current_cr3(), name);
    regs->esp = USER_ESP0_INIT;
    regs->user_esp = USER_ESP0_INIT;
    sched_user_heap_set(USER_HEAP_BASE);    /* heap nuevo por proceso */
    uheap_reset();

    /* exec reescribio las tablas de pagina del CR3 actual (paging_free_user_
     * space + elf_load_into + win32_map_all) sin cambiar de tarea: el fetch
     * del entry podria usar traducciones TLB viejas que apuntan a frames
     * liberados/reusados. Flush forzado con mov cr3,cr3. */
    paging_switch(sched_current_cr3());

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
                /* El gate de int 0x80 deshabilita IF: hlt con IF=0
                 * congelaria todo el sistema (el PIT no despierta).
                 * Re-habilitar durante la espera (el scheduler
                 * reanuda este handler en el mismo punto). */
                sti();
                halt();
                cli();
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
        /* Fase 23-C10: heap first-fit por proceso sobre
         * [USER_HEAP_BASE, USER_HEAP_END) con split/coalesce y
         * expansion por el bump bajo demanda (kernel/mem/uheap.c). */
        regs->eax = (uint32_t)uheap_alloc(pd, regs->ebx);
        break;
    }
    case SYS_FREE:
        uheap_free(pd, (void *)regs->ebx);
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
    case SYS_FCREATE: { /* ebx=nombre: crea archivo vacio */
        char name[32];
        if (user_strcpy(name, sizeof(name), (const char *)regs->ebx, pd) != 0)
            regs->eax = -1;
        else
            regs->eax = (int32_t)mefs_create(name);
        break;
    }
    case SYS_MKDIR: { /* ebx=nombre, ecx=parent: crea subdirectorio */
        char name[32];
        if (user_strcpy(name, sizeof(name), (const char *)regs->ebx, pd) != 0)
            regs->eax = -1;
        else
            regs->eax = (int32_t)mefs_mkdir(name, regs->ecx);
        break;
    }
    case SYS_FCREATE_IN: { /* ebx=parent, ecx=nombre: archivo en subdir */
        char name[32];
        if (user_strcpy(name, sizeof(name), (const char *)regs->ecx, pd) != 0)
            regs->eax = -1;
        else
            regs->eax = (int32_t)mefs_create_in(regs->ebx, name);
        break;
    }
    case SYS_THREADCREATE: { /* Fase 24-P2.2: ebx=fn, ecx=param, edx=&tid */
        uint32_t fn = regs->ebx, param = regs->ecx;
        uint32_t *tid = (uint32_t *)regs->edx;
        uint32_t ret_va = win32_resolve("kernel32.dll", "_thread_ret");
        int pid = task_create_thread(sched_current_cr3(), fn, param,
                                     ret_va);
        if (tid != 0)
            user_memcpy_out(tid, &pid, 4, pd);
        regs->eax = (int32_t)pid;
        break;
    }
    case SYS_THREADEXIT: { /* Fase 24-P2.2: termina el hilo actual */
        (void)regs->ebx;
        exit_current(regs);
        break;
    }
    case SYS_TICKS: { /* Fase 24-P3.1: ticks del timer (100 Hz), para
                       * detectar doble clic en el escritorio */
        regs->eax = timer_get_ticks();
        break;
    }
    case SYS_FWRITE: { /* ebx=nombre, ecx=buf, edx=len: sobrescribe */
        char name[32];
        void *tmp;
        uint32_t len = regs->edx;
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
        if (user_memcpy_in(tmp, (const void *)regs->ecx, len, pd) != 0) {
            kfree(tmp);
            regs->eax = -1;
            break;
        }
        regs->eax = (int32_t)mefs_write(name, tmp, len);
        kfree(tmp);
        break;
    }
    case SYS_FDELETE: { /* ebx=nombre: elimina archivo */
        char name[32];
        if (user_strcpy(name, sizeof(name), (const char *)regs->ebx, pd) != 0)
            regs->eax = -1;
        else
            regs->eax = (int32_t)mefs_delete(name);
        break;
    }
    case SYS_FLUSH: { /* persiste superbloque + directorio al disco */
        regs->eax = (int32_t)mefs_flush();
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
    case SYS_DLISTDIR: { /* ebx=parent, ecx=idx, edx=&{name,size,flags} */
        uint32_t parent = regs->ebx;
        uint32_t idx = regs->ecx;
        uint32_t out[6];
        char name[MEFS_NAME_LEN];
        uint32_t size, flags;
        if (mefs_ls(parent, idx, name, &size, &flags) != 0) {
            regs->eax = -1;
            break;
        }
        for (int i = 0; i < MEFS_NAME_LEN; i++)
            ((uint8_t *)out)[i] = (uint8_t)name[i];
        out[4] = size;
        out[5] = flags;
        if (user_memcpy_out((char *)regs->edx, (const char *)out,
                            sizeof(out), pd) != 0) {
            regs->eax = -1;
            break;
        }
        regs->eax = 0;
        break;
    }
    case SYS_DPARENT: { /* ebx=idx -> parent */
        regs->eax = mefs_parent(regs->ebx);
        break;
    }
    case SYS_DLOOKUP: { /* ebx=parent, ecx=name -> indice */
        char name[MEFS_NAME_LEN];
        if (user_strcpy(name, sizeof(name), (const char *)regs->ecx,
                        pd) != 0) {
            regs->eax = -1;
            break;
        }
        regs->eax = (uint32_t)mefs_lookup(regs->ebx, name);
        break;
    }
    case SYS_SELFNAME: { /* ebx=buf, ecx=max: nombre del ejecutable
                          * actual (kernel32.GetModuleFileNameA) */
        char *buf = (char *)regs->ebx;
        uint32_t max = regs->ecx;
        char tmp[32];
        uint32_t n = 0;
        if (max == 0) {
            regs->eax = -1;
            break;
        }
        sched_get_exe_name(tmp, sizeof(tmp));
        while (n < max - 1 && tmp[n])
            n++;
        if (user_memcpy_out(buf, tmp, n, pd) != 0) {
            regs->eax = -1;
            break;
        }
        buf[n] = 0;
        regs->eax = (int32_t)n;
        break;
    }
    case SYS_GFXINFO: { /* ebx=&struct{lfb_va,w,h,bpp} (user32, Fase 12) */
        uint32_t info[4] = { VBE_LFB_USER_VA, VBE_SCREEN_W,
                             VBE_SCREEN_H, VBE_SCREEN_BPP };
        if (user_memcpy_out((char *)regs->ebx, (const char *)info,
                            sizeof(info), pd) != 0) {
            regs->eax = -1;
            break;
        }
        regs->eax = 0;
        break;
    }
    case SYS_MOUSEINFO: { /* ebx=&struct{x,y,buttons} (Fase 14) */
        int mi[3];
        mouse_read(&mi[0], &mi[1], &mi[2]);
        if (user_memcpy_out((char *)regs->ebx, (const char *)mi,
                            sizeof(mi), pd) != 0) {
            regs->eax = -1;
            break;
        }
        regs->eax = 0;
        break;
    }
    case SYS_EVENT: { /* ebx=&struct{type,x,y,buttons,key}; -1 si vacio */
        mouse_event_t ev;
        int done = 0;
        /* Fase 16/17: el WM consume/transforma los eventos y los
         * enruta por PD (cada app recibe solo los de sus ventanas y
         * del foco). Sin ventanas el evento va crudo al llamador. */
        while (!done && mouse_event_dequeue(&ev) == 0) {
            int r = wm_route(&ev);
            if (r == WM_ROUTE_CONSUMED)
                continue;
            if (r == WM_ROUTE_RAW) {
                if (user_memcpy_out((char *)regs->ebx, (const char *)&ev,
                                    sizeof(ev), pd) == 0)
                    regs->eax = 0;
                else
                    regs->eax = -1;
                done = 1;
                break;
            }
            wm_event_deliver((uint32_t)r, &ev);
        }
        if (done)
            break;
        if (wm_event_claim(pd, &ev) == 0) {
            if (user_memcpy_out((char *)regs->ebx, (const char *)&ev,
                                sizeof(ev), pd) == 0)
                regs->eax = 0;
            else
                regs->eax = -1;
        } else {
            regs->eax = -1;
        }
        break;
    }
    case SYS_WINCREATE: { /* ebx=&{title*,x,y,w,h,buf_va,buf_sz,flags}->id */
        struct {
            uint32_t title, x, y, w, h, buf_va, buf_sz, flags;
        } a;
        char title[24];
        int r;
        if (user_memcpy_in(&a, (const void *)regs->ebx, sizeof(a), pd) != 0 ||
            user_strcpy(title, sizeof(title), (const char *)a.title,
                        pd) != 0) {
            regs->eax = -1;
            break;
        }
        r = wm_create(title, (int)a.x, (int)a.y, (int)a.w, (int)a.h,
                      a.buf_va, a.buf_sz, a.flags, pd);
        regs->eax = r;
        break;
    }
    case SYS_WINCLOSE:   /* ebx=id (solo la app duena puede cerrar) */
        regs->eax = wm_close((int)regs->ebx, pd);
        break;
    case SYS_WINMOVE: {  /* ebx=id, ecx=dx, edx=dy */
        regs->eax = wm_move((int)regs->ebx, (int)regs->ecx, (int)regs->edx);
        break;
    }
    case SYS_WINUPDATE: { /* ebx=id, ecx=&rect{x,y,w,h} opcional (Fase 20-D:
                       * blit por regiones; 0 = cliente completo) */
        int32_t rect[4];
        if (regs->ecx != 0) {
            if (user_memcpy_in(rect, (const void *)regs->ecx,
                               sizeof(rect), pd) != 0) {
                regs->eax = -1;
                break;
            }
            regs->eax = wm_update_rect((int)regs->ebx, rect);
        } else {
            regs->eax = wm_update((int)regs->ebx);
        }
        break;
    }
    case SYS_WININFO: {  /* ebx=id, ecx=&{x,y,w,h,cx,cy,cw,ch} */
        uint32_t info[8];
        if (wm_info((int)regs->ebx, info) != 0 ||
            user_memcpy_out((char *)regs->ecx, (const char *)info,
                            sizeof(info), pd) != 0) {
            regs->eax = -1;
            break;
        }
        regs->eax = 0;
        break;
    }
    case SYS_WINTITLE: { /* ebx=id, ecx=title* (validado por PD) */
        char title[24];
        if (user_strcpy(title, sizeof(title), (const char *)regs->ecx,
                        pd) != 0) {
            regs->eax = -1;
            break;
        }
        regs->eax = wm_set_title((int)regs->ebx, title);
        break;
    }
    case SYS_EXEBASE:
        regs->eax = sched_current_exe_base();
        break;
    case SYS_MOUSE_INJECT: { /* ebx=&mouse_event_t: evento sintetico.
                              * Solo para tests (el monitor de QEMU no
                              * inyecta PS/2 fiable en headless). Pasa
                              * por la cola global y wm_route como uno
                              * real. */
        mouse_event_t ev;
        if (user_memcpy_in(&ev, (const void *)regs->ebx, sizeof(ev), pd)
            != 0) {
            regs->eax = -1;
            break;
        }
        if (ev.type == EV_MOVE)
            mouse_set_pos(ev.x, ev.y);
        mouse_event_push(ev.type, ev.x, ev.y, ev.buttons, ev.key);
        regs->eax = 0;
        break;
    }
    case SYS_MENUBAR: { /* ebx=id, ecx=on, edx=flat* (validado por PD) */
        char flat[200];
        if (regs->ecx && regs->edx) {
            if (user_strcpy(flat, sizeof(flat), (const char *)regs->edx,
                            pd) != 0) {
                regs->eax = -1;
                break;
            }
            regs->eax = wm_set_menu((int)regs->ebx, (int)regs->ecx, flat);
        } else {
            regs->eax = wm_set_menu((int)regs->ebx, (int)regs->ecx, 0);
        }
        break;
    }
    case SYS_TOOLBAR: { /* ebx=id, ecx=on, edx=flat* (validado por PD) */
        char flat[200];
        if (regs->ecx && regs->edx) {
            if (user_strcpy(flat, sizeof(flat), (const char *)regs->edx,
                            pd) != 0) {
                regs->eax = -1;
                break;
            }
            regs->eax = wm_set_toolbar((int)regs->ebx, (int)regs->ecx, flat);
        } else {
            regs->eax = wm_set_toolbar((int)regs->ebx, (int)regs->ecx, 0);
        }
        break;
    }
    case SYS_DLLBASE: { /* ebx=nombre_dll -> base real de carga */
        char name[32];
        if (user_strcpy(name, sizeof(name), (const char *)regs->ebx,
                        pd) != 0) {
            regs->eax = 0;
            break;
        }
        regs->eax = win32_module_base(name);
        break;
    }
    case SYS_GETPROC: { /* ebx=base_dll, ecx=nombre -> VA export */
        char name[64];
        if (user_strcpy(name, sizeof(name), (const char *)regs->ecx,
                        pd) != 0) {
            regs->eax = 0;
            break;
        }
        regs->eax = win32_resolve_base(regs->ebx, name);
        break;
    }
    default:
        break;
    }
}
