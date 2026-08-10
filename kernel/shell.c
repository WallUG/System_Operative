/* MyOS - kernel/shell.c
 * Shell interactiva minima (Fase 6): bucle leer linea del teclado ->
 * parsear -> comando interno (help, ls, cat, echo, ver) o lanzar un
 * ELF de usuario desde el FS ("run <archivo>", Fase 7). */
#include <stdint.h>
#include <string.h>
#include "shell.h"
#include "kprint.h"
#include "io.h"
#include "drivers/vga.h"
#include "drivers/serial.h"
#include "drivers/keyboard.h"
#include "drivers/timer.h"
#include "fs/mefs.h"
#include "mem/pmm.h"
#include "mem/heap.h"
#include "elf.h"
#include "pe.h"
#include "win32.h"
#include "task/task.h"

static volatile int shell_run = 1;

static void echo_line(const char *line)
{
    kprint(line);
    kprint("\n");
}

/* Imprime un byte como 2 digitos hex (xx, sin 0x). */
static void hex_byte(uint8_t b)
{
    static const char *digits = "0123456789ABCDEF";
    char s[3];
    s[0] = digits[b >> 4];
    s[1] = digits[b & 0xF];
    s[2] = 0;
    kprint(s);
}

static void read_line(char *line, int max)
{
    int pos = 0;
    line[0] = 0;
    for (;;) {
        /* Si corre una tarea de usuario, no robar su entrada de
         * consola: esperar (hlt) a que termine o ceda. */
        if (sched_user_busy()) {
            halt();
            continue;
        }
        int c = keyboard_read();
        if (c < 0)
            c = serial_read_char();     /* entrada alternativa por COM1 */
        if (c < 0) {
            halt();
            continue;
        }
        if (c == '\n') {
            line[pos] = 0;
            kprint("\n");
            return;
        }
        if (c == '\b' || c == 0x7F) {
            if (pos > 0) {
                pos--;
                kprint("\b \b");
            }
            continue;
        }
        if (pos < max - 1) {
            line[pos++] = (char)c;
            line[pos] = 0;
            char buf[2] = { (char)c, 0 };
            kprint(buf);
        }
    }
}

void shell_loop(void)
{
    char line[128];

    kprint("\nMyOS shell - escribe 'help' para ayuda\n");
    while (shell_run) {
        kprint("myos> ");
        read_line(line, sizeof(line));

        if (strcmp(line, "help") == 0) {
            kprint("  help       esta ayuda\n");
            kprint("  ls         lista archivos del FS\n");
            kprint("  cat <f>    muestra el contenido de <f>\n");
            kprint("  echo <txt> repite el texto\n");
            kprint("  ver <f>    muestra <f> como hex+ascii\n");
            kprint("  run <f>    ejecuta el binario de usuario <f>\n");
            kprint("  pf         provoca un #PF intencional\n");
        } else if (strcmp(line, "ls") == 0) {
            int n = mefs_file_count();
            char name[MEFS_NAME_LEN];
            kprint_uint((uint32_t)n);
            kprint(" archivo(s):\n");
            for (int i = 0; i < n; i++) {
                if (mefs_list((uint32_t)i, name) == 0) {
                    kprint("  ");
                    kprint(name);
                    kprint("  (");
                    kprint_uint((uint32_t)mefs_size(name));
                    kprint(" B)\n");
                }
            }
        } else if (strncmp(line, "cat ", 4) == 0) {
            static uint8_t buf[2048];
            int n = mefs_read(line + 4, buf, sizeof(buf));
            if (n < 0) {
                kprint("archivo no encontrado\n");
            } else {
                buf[n] = 0;
                kprint((char *)buf);
                if (n == 0 || ((char *)buf)[n - 1] != '\n')
                    kprint("\n");
            }
        } else if (strncmp(line, "echo ", 5) == 0) {
            echo_line(line + 5);
        } else if (strncmp(line, "ver ", 4) == 0) {
            static uint8_t buf[512];
            int n = mefs_read(line + 4, buf, sizeof(buf));
            if (n < 0) {
                kprint("archivo no encontrado\n");
            } else {
                for (int i = 0; i < n; i++) {
                    if (i % 16 == 0) {
                        kprint_hex32((uint32_t)i);
                        kprint("  ");
                    }
                    hex_byte(buf[i]);
                    if (i % 16 == 15)
                        kprint("\n");
                    else
                        kprint(" ");
                }
                if (n % 16)
                    kprint("\n");
            }
        } else if (strncmp(line, "run ", 4) == 0) {
            /* leer el ELF/PE del FS a un buffer del kernel, mapearlo en
             * un PD de usuario aislado y crear la tarea (ring 3).
             * Primer token = archivo; el resto de la linea es la linea
             * de comandos real (GetCommandLineA -> argc/argv). */
            char exe_nm[32];
            char cmdline[WIN32_TIB_CMDLINE_LEN];
            int kk = 0, cl = 0;
            while (kk < 31 && line[4 + kk] && line[4 + kk] != ' '
                   && line[4 + kk] != '\t') {
                exe_nm[kk] = line[4 + kk];
                kk++;
            }
            exe_nm[kk] = 0;
            while (cl < (int)(WIN32_TIB_CMDLINE_LEN - 1) && line[4 + cl]) {
                cmdline[cl] = line[4 + cl];
                cl++;
            }
            cmdline[cl] = 0;
            uint32_t size = (uint32_t)mefs_size(exe_nm);
            if (size == 0 || size > 0x100000) {
                kprint("archivo no encontrado o demasiado grande\n");
                continue;
            }
            void *buf = kmalloc(size);
            if (buf == NULL) {
                kprint("memoria insuficiente\n");
                continue;
            }
            if (mefs_read(exe_nm, buf, size) != (int)size) {
                kprint("error leyendo archivo\n");
                kfree(buf);
                continue;
            }
            uint32_t pd, entry;
            int is_pe = (size >= 2 && ((uint8_t *)buf)[0] == 'M'
                         && ((uint8_t *)buf)[1] == 'Z');
            int r = is_pe ? pe_load(buf, size, &pd, &entry)
                          : elf_load(buf, size, &pd, &entry);
            if (r != 0) {
                kprint(is_pe ? "PE invalido\n" : "ELF invalido\n");
                kfree(buf);
                continue;
            }
            int mapr = win32_map_all(pd);
            kprint_uint((uint32_t)mapr);
            kprint("(map) ");
            kfree(buf);
            if (task_create_user("user", exe_nm, cmdline, pd, entry) < 0) {
                paging_free_pd(pd);
                kprint("no se pudo crear la tarea\n");
                continue;
            }
            kprint("ok(create)\n");
            kprint("Ejecutando ");
            kprint(exe_nm);
            kprint(" en ring 3 (PD aislado)...\n");
        } else if (strcmp(line, "pf") == 0) {
            kprint("Provocando #PF: escritura en 0x50000000 (no mapeado)...\n");
            *(volatile uint32_t *)0x50000000u = 0xDEADBEEFu;
        } else if (line[0] != 0) {
            kprint("comando no encontrado\n");
        }
    }
}
