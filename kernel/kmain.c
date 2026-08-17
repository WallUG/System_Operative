/* MyOS - kernel/kmain.c
 * Punto de entrada del kernel en C (llamado desde kernel/entry.asm).
 * Fase 7: kmain recibe el puntero a boot_info (0x7000) como argumento
 * cdecl; segun el modo (disco/CD) el FS se sirve desde ATA o desde la
 * imagen MEFS copiada a RAM por el bootloader (mefs_init_mem). */

#include <stdint.h>
#include "io.h"
#include "bootinfo.h"
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
#include "drivers/ac97.h"
#include "drivers/rtl8139.h"
#include "drivers/net.h"
#include "drivers/keyboard.h"
#include "drivers/vbe.h"
#include "drivers/vgafx.h"
#include "drivers/mouse.h"
#include "task/task.h"
#include "gdt.h"
#include "syscall.h"
#include "fs/mefs.h"
#include "shell.h"
#include "win32.h"
#include "bootscreen.h"

/* Fase 22: pequena espera para que la barra de carga sea visible. */
static void bs_delay(void)
{
    volatile uint32_t n;
    for (n = 0; n < 12000000u; n++)
        ;
}

/* --- Fase 5 (historial): tareas demo del scheduler round-robin. ---
 * Se eliminaron en la Fase 22 (limpieza del boot): solo ensuciaban la
 * consola con T-A/T-B. El scheduler funciona igual con la shell como
 * tarea idle. */

void kmain(uint32_t boot_info_ptr)
{
    const bootinfo_t *bi = (const bootinfo_t *)boot_info_ptr;
    serial_init();

    /* Fase 12: activar VBE (800x600x32) lo antes posible; si hay LFB el
     * kernel pasa a la consola grafica (vgafx) y 0xB8000 queda oculto. */
    vbe_init();
    if (vbe_graphics_active)
        vgafx_init();
    else
        vga_init();
    mouse_init();                   /* raton PS/2 (IRQ12), Fase 13 */
    if (vbe_graphics_active)
        mouse_draw_cursor();

    /* --- Fase 22: pantalla de carga (barra de progreso) --- */
    bootscreen_start();
    bootscreen_status("Cargando drivers (video, raton, teclado)...", 15);
    bs_delay();

    kprint("MyOS 0.4.0 - gestion de memoria\n");

    /* --- Memoria fisica: E820 -> PMM bitmap -> heap --- */
    bootscreen_status("Configurando memoria (E820, PMM, heap)...", 30);
    bs_delay();
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
        kprint("PMM: bitmap en .bss, frames libres = ");
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
    bootscreen_status("Activando paginacion...", 45);
    bs_delay();
    paging_init();
    /* Mapear el LFB (0xFD000000, fuera del identity de 1 GiB) antes de
     * imprimir: con VBE activo la consola grafica escribe en el LFB. */
    paging_map_kernel_lfb();
    {
        volatile uint32_t *p = (volatile uint32_t *)0x1000000;  /* 16 MiB */
        *p = 0x11223344u;
        kprint("Paginacion: PD en ");
        kprint_hex32(paging_pd_addr());
        kprint(", write/read en 16 MiB -> ");
        kprint_hex32(*p);
        kprint("\n");
    }

    /* --- Video VBE (Fase 12): dispi -> 800x600x32, LFB mapeado --- */
    if (vbe_graphics_active) {
        kprint("VBE: 800x600x32 activo, LFB 0x");
        kprint_hex32(vbe_lfb_phys);
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
    bootscreen_status("Inicializando interrupciones (PIT, IRQs)...", 60);
    bs_delay();
    idt_init();
    pic_remap();
    timer_init(100);            /* 100 ticks/segundo */

    /* Fase 24-P4: audio AC'97 (QEMU -device AC97). El beep de
     * arranque valida codec + DMA; sin dispositivo PCI no hace nada.
     * El ping de red va tras sti(): sus polls usan timer_get_ticks. */
    if (ac97_init() == 0)
        ac97_beep(150);

    /* Fase 24-P4: red RTL8139 (QEMU -netdev user). Solo el init aqui;
     * el ping (que espera respuestas con el timer) se hace al final
     * del arranque, con las interrupciones activas. */
    rtl8139_init();
    keyboard_init();

    sti();                      /* solo ahora: IDT y PIC listos */

    /* --- Fase 6: GDT+TSS (ring 3), syscalls, FS y shell --- */
    gdt_init();                     /* segmentos de usuario + TSS */
    syscall_init();                 /* gate int 0x80 con DPL=3 */

    /* --- Fase 6/7: FS. Origen segun boot_info: ATA (disco) o imagen
     * en RAM (CD; mefs_init_mem). Sin boot_info valido: ATA (fallback
     * para lanzar el kernel desnudo desde el depurador). */
    bootscreen_status("Inicializando filesystem MEFS...", 75);
    bs_delay();
    {
        int ok = 0;
        if (bi && bi->magic == BOOTINFO_MAGIC && bi->mode == BOOTINFO_MODE_CD)
            ok = mefs_init_mem((const uint8_t *)bi->fs_source,
                               bi->fs_size) == 0;
        else
            ok = mefs_init() == 0;

        if (ok) {
            kprint("MEFS: ");
            kprint_uint((uint32_t)mefs_file_count());
            kprint(" archivo(s)");
            if (bi && bi->magic == BOOTINFO_MAGIC &&
                bi->mode == BOOTINFO_MODE_CD)
                kprint(" (RAM)\n");
            else
                kprint(" en disco\n");
        } else {
            kprint("MEFS: error leyendo filesystem\n");
        }
    }

    /* --- Soporte Windows: modulos Win32 ring 3 fijos (Fase 8) --- */
    bootscreen_status("Cargando DLLs Win32 (kernel32, user32, gdi32)...", 88);
    bs_delay();
    win32_init();

    /* --- Fase 5: multitarea round-robin (IRQ0) ---
     * kmain continua como tarea idle (hlt); las apps de usuario corren
     * como tareas ring 3 con su PD. */
    bootscreen_status("Iniciando multitarea...", 95);
    bs_delay();
    sched_init();
    sched_start();
    bootscreen_status("Listo. Iniciando shell...", 100);
    bs_delay();
    bootscreen_done();

    /* Fase 24-P4: con las IRQs activas, validar TX+RX de la red
     * (ARP + ICMP echo contra el gateway del user-net de QEMU). */
    if (rtl8139_ready()) {
        net_init_stack();
        net_ping();
    }
    shell_loop();
}
