/* MyOS - kernel/kmain.c
 * Punto de entrada del kernel en C (llamado desde kernel/entry.asm).
 * Fase 4: memoria - E820 -> PMM bitmap -> paginacion PSE 4 MiB
 * (identity 0-1 GiB) -> heap first-fit -> #PF intencional. Mantiene
 * las demos de interrupciones de la Fase 3 (PIT + teclado). */

#include <stdint.h>
#include "io.h"
#include "idt.h"
#include "pic.h"
#include "kprint.h"
#include "mem/mmap.h"
#include "mem/pmm.h"
#include "mem/paging.h"
#include "mem/heap.h"
#include "drivers/vga.h"
#include "drivers/serial.h"
#include "drivers/timer.h"
#include "drivers/keyboard.h"
#include "task/task.h"

/* --- Fase 5: tareas de prueba del scheduler round-robin --- */
static volatile uint32_t cnt_a, cnt_b;

static void task_print_line(const char *name, uint32_t v)
{
    /* print atomico (IF off): evita intercalar caracteres entre tareas */
    __asm__ volatile("cli");
    kprint(name);
    kprint(": ");
    kprint_uint(v);
    kprint("\n");
    __asm__ volatile("sti");
}

static void task_a(void)
{
    uint32_t last = timer_get_ticks();
    for (;;) {
        cnt_a++;
        if (timer_get_ticks() - last >= 10) {
            last = timer_get_ticks();
            task_print_line("T-A", cnt_a);
        }
    }
}

static void task_b(void)
{
    uint32_t last = timer_get_ticks();
    for (;;) {
        cnt_b++;
        if (timer_get_ticks() - last >= 10) {
            last = timer_get_ticks();
            task_print_line("T-B", cnt_b);
        }
    }
}

void kmain(void)
{
    serial_init();
    vga_init();

    kprint("MyOS 0.4.0 - gestion de memoria\n");

    /* --- Memoria fisica: E820 -> PMM bitmap -> heap --- */
    mmap_init();
    pmm_init();
    heap_init();
    {
        uint32_t total_mb = (uint32_t)(mmap_total_usable() >> 20);
        kprint("E820: ");
        kprint_uint(e820_count());
        kprint(" entradas, RAM usable ~");
        kprint_uint(total_mb);
        kprint(" MiB\n");
        kprint("PMM: bitmap en 0x20000, frames libres = ");
        kprint_uint(pmm_free_count());
        kprint("\n");
    }

    /* --- Demo 1: PMM alloc/free round-trip --- */
    {
        uint32_t a = pmm_alloc_frame();
        uint32_t b = pmm_alloc_frame();
        uint32_t c = pmm_alloc_frame();
        kprint("PMM: frames ");
        kprint_hex32(a);
        kprint(" ");
        kprint_hex32(b);
        kprint(" ");
        kprint_hex32(c);
        kprint("\n");
        pmm_free_frame(b);
        pmm_free_frame(a);
        pmm_free_frame(c);
        kprint("PMM: liberados 3 frames, libres = ");
        kprint_uint(pmm_free_count());
        kprint("\n");
    }

    /* --- Paginacion: PSE, identity map 0-1 GiB con paginas de 4 MiB --- */
    paging_init();
    {
        volatile uint32_t *p = (volatile uint32_t *)0x1000000;  /* 16 MiB */
        *p = 0x11223344u;
        kprint("Paginacion: PD en ");
        kprint_hex32(paging_pd_addr());
        kprint(", write/read en 16 MiB -> ");
        kprint_hex32(*p);
        kprint("\n");
    }

    /* --- Heap: first-fit con split y reutilizacion --- */
    heap_dump();
    {
        void *b1 = kmalloc(1024);
        void *b2 = kmalloc(4096);
        void *b3 = kmalloc(64);
        kprint("Heap: kmalloc(1024)=");
        kprint_hex32((uint32_t)b1);
        kprint(" kmalloc(4096)=");
        kprint_hex32((uint32_t)b2);
        kprint(" kmalloc(64)=");
        kprint_hex32((uint32_t)b3);
        kprint("\n");
        kfree(b2);
        {
            void *b4 = kmalloc(2048);   /* reutiliza el bloque libre de b2 */
            kprint("Heap: kfree(4096) y kmalloc(2048)=");
            kprint_hex32((uint32_t)b4);
            kprint("\n");
            kfree(b4);
        }
        kfree(b1);
        kfree(b3);
        heap_dump();
    }

    /* --- Interrupciones (Fase 3): PIT + teclado --- */
    idt_init();
    pic_remap();
    timer_init(100);            /* 100 ticks/segundo */
    keyboard_init();

    sti();                      /* solo ahora: IDT y PIC listos */

    {
        uint32_t start = timer_get_ticks();
        while (timer_get_ticks() - start < 100)
            halt();             /* las IRQ despiertan el hlt */
        kprint("PIT OK: 100 ticks en 1 s. ticks=");
        kprint_uint(timer_get_ticks());
        kprint("\n");
    }

    {
        uint32_t start = timer_get_ticks();
        int keys = 0;
        kprint("Ventana de teclado (3 s) - pulsa algo...\n");
        while (timer_get_ticks() - start < 300) {
            int c = keyboard_read();
            if (c >= 0) {
                vga_putc((char)c);
                serial_putc((char)c);
                keys++;
            }
            halt();
        }
        kprint("\nTeclas recibidas: ");
        kprint_uint((uint32_t)keys);
        kprint("\n");
    }

    /* --- Fase 5: multitarea round-robin (IRQ0) ---
     * kmain continua como tarea idle (hlt); A y B son contadores que
     * imprimen cada ~10 ticks (100 ms). El scheduler cambia de tarea en
     * cada tick del PIT, alternando A/B/idle visiblemente. */
    sched_init();
    task_create("A", task_a);
    task_create("B", task_b);
    sched_start();

    {
        /* idle: dejar correr las tareas ~3 s, luego el demo de #PF */
        uint32_t start = timer_get_ticks();
        while (timer_get_ticks() - start < 300)
            halt();
    }

    /* --- Demo final: #PF intencional en 0x50000000 (no mapeado).
     * Con paginacion activa, PDE 320 = 0 -> la CPU lanza vector 14;
     * isr_handler lee CR2 y kpanic_page_fault imprime el diagnostico.
     * La escritura no deberia retornar nunca. --- */
    kprint("Provocando #PF: escritura en 0x50000000 (no mapeado)...\n");
    *(volatile uint32_t *)0x50000000u = 0xDEADBEEFu;

    for (;;) {
        halt();
    }
}
