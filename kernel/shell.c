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

/* Fase 20-A: directorio actual de la shell (indice de entrada dir,
 * MEFS_ROOT = raiz). MEFS_FS_SHELL_CAP = capacidad del FS al formatear. */
static uint32_t shell_cwd = MEFS_ROOT;
#define MEFS_FS_SHELL_CAP 1400

/* Devuelve el indice del padre de la entrada 'idx' (raiz si es root/dir). */
static uint32_t shell_cwd_parent(uint32_t idx)
{
    return mefs_parent(idx);
}

/* Imprime la ruta del cwd (nombre del dir actual). */
static void shell_print_path(uint32_t idx)
{
    char name[MEFS_NAME_LEN];
    if (idx == MEFS_ROOT) {
        kprint("/");
        return;
    }
    if (mefs_name(idx, name) != 0) {
        kprint("/?");
        return;
    }
    kprint("/");
    kprint(name);
}

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
    shell_autoboot();
    while (shell_run) {
        kprint("myos> ");
        read_line(line, sizeof(line));

        if (strcmp(line, "help") == 0) {
            kprint("  help       esta ayuda\n");
            kprint("  ls [d]     lista archivos del directorio actual\n");
            kprint("  cat <f>    muestra el contenido de <f>\n");
            kprint("  echo <txt> repite el texto\n");
            kprint("  ver <f>    muestra <f> como hex+ascii\n");
            kprint("  run <f>    ejecuta el binario de usuario <f>\n");
            kprint("  touch <f>  crea un archivo vacio (Fase E)\n");
            kprint("  write <f>  escribe 'hola desde MyOS!' en <f>\n");
            kprint("  rm <f>     elimina el archivo <f>\n");
            kprint("  mkdir <d>  crea un subdirectorio (Fase 20-A)\n");
            kprint("  cd <d>     cambia al subdirectorio (cd .. sube)\n");
            kprint("  pwd        muestra el directorio actual\n");
            kprint("  format     formatea el disco (borra todo)\n");
            kprint("  flush      persiste el FS al disco (Fase E)\n");
            kprint("  bootgui on|off  autoboot del escritorio (Fase 22)\n");
            kprint("  pf         provoca un #PF intencional\n");
        } else if (strcmp(line, "ls") == 0 || strncmp(line, "ls ", 3) == 0) {
            /* ls [dir]: lista el cwd o el subdirectorio dado */
            uint32_t d = shell_cwd;
            if (line[2] == ' ') {
                if (strcmp(line + 3, "..") == 0) {
                    d = shell_cwd_parent(shell_cwd);
                } else {
                    int idx = mefs_lookup(shell_cwd, line + 3);
                    if (idx < 0) {
                        kprint("no existe\n");
                        continue;
                    }
                    d = (uint32_t)idx;
                }
            }
            int n = 0;
            char name[MEFS_NAME_LEN];
            uint32_t sz, fl;
            while (mefs_ls(d, (uint32_t)n, name, &sz, &fl) == 0) {
                kprint("  ");
                kprint(name);
                if (fl & MEFS_FLAG_DIR)
                    kprint("/  ");
                else
                    kprint("   ");
                if (!(fl & MEFS_FLAG_DIR)) {
                    kprint_uint(sz);
                    kprint(" B");
                }
                kprint("\n");
                n++;
            }
            kprint_uint((uint32_t)n);
            kprint(" entrada(s)\n");
        } else if (strncmp(line, "mkdir ", 6) == 0) {
            if (mefs_mkdir(line + 6, shell_cwd) != 0)
                kprint("no se pudo crear dir\n");
            else
                kprint("dir creado\n");
        } else if (strncmp(line, "cd ", 3) == 0) {
            if (strcmp(line + 3, "..") == 0) {
                shell_cwd = shell_cwd_parent(shell_cwd);
                continue;
            }
            int idx = mefs_lookup(shell_cwd, line + 3);
            if (idx < 0) {
                kprint("no existe\n");
                continue;
            }
            shell_cwd = (uint32_t)idx;
        } else if (strcmp(line, "pwd") == 0) {
            shell_print_path(shell_cwd);
            kprint("\n");
        } else if (strcmp(line, "format") == 0) {
            if (mefs_format(MEFS_FS_SHELL_CAP) != 0)
                kprint("no se pudo formatear\n");
            else {
                shell_cwd = MEFS_ROOT;
                kprint("disco formateado\n");
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
        } else if (strncmp(line, "touch ", 6) == 0) {
            if (mefs_create(line + 6) != 0)
                kprint("no se pudo crear\n");
            else
                kprint("creado\n");
        } else if (strncmp(line, "write ", 6) == 0) {
            static const char msg[] = "hola desde MyOS!\n";
            if (mefs_size(line + 6) < 0)
                mefs_create(line + 6);
            if (mefs_write(line + 6, (const uint8_t *)msg,
                           (uint32_t)(sizeof(msg) - 1)) != 0)
                kprint("no se pudo escribir\n");
            else
                kprint("escrito\n");
        } else if (strncmp(line, "rm ", 3) == 0) {
            if (mefs_delete(line + 3) != 0)
                kprint("no se pudo eliminar\n");
            else
                kprint("eliminado\n");
        } else if (strcmp(line, "flush") == 0) {
            if (mefs_flush() != 0)
                kprint("no se pudo flushear\n");
            else
                kprint("FS flusheado al disco\n");
        } else if (strncmp(line, "bootgui", 7) == 0) {
            if (strncmp(line + 7, " on", 3) == 0) {
                mefs_set_boot_gui(1);
                kprint("autoboot del escritorio: ON (persistelo con flush)\n");
            } else if (strncmp(line + 7, " off", 4) == 0) {
                mefs_set_boot_gui(0);
                kprint("autoboot del escritorio: OFF (persistelo con flush)\n");
            } else {
                kprint("bootgui esta ");
                kprint(mefs_boot_gui() ? "ON" : "OFF");
                kprint("\n");
            }
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
            shell_run_file(line + 4);
        } else if (strcmp(line, "pf") == 0) {
            kprint("Provocando #PF: escritura en 0x50000000 (no mapeado)...\n");
            *(volatile uint32_t *)0x50000000u = 0xDEADBEEFu;
        } else if (line[0] != 0) {
            kprint("comando no encontrado\n");
        }
    }
}

/* Ejecuta un ELF/PE del FS como tarea de usuario (nucleo del comando
 * "run" de la shell; Fase 22 lo reutiliza para el autoboot). 'line' es
 * el archivo + linea de comandos opcional. */
void shell_run_file(const char *line)
{
    char exe_nm[32];
    char cmdline[WIN32_TIB_CMDLINE_LEN];
    int kk = 0, cl = 0;

    while (kk < 31 && line[kk] && line[kk] != ' ' && line[kk] != '\t') {
        exe_nm[kk] = line[kk];
        kk++;
    }
    exe_nm[kk] = 0;
    while (cl < (int)(WIN32_TIB_CMDLINE_LEN - 1) && line[cl]) {
        cmdline[cl] = line[cl];
        cl++;
    }
    cmdline[cl] = 0;

    uint32_t size = (uint32_t)mefs_size(exe_nm);
    if (size == 0 || size > 0x100000) {
        kprint("archivo no encontrado o demasiado grande\n");
        return;
    }
    void *buf = kmalloc(size);
    if (buf == NULL) {
        kprint("memoria insuficiente\n");
        return;
    }
    if (mefs_read(exe_nm, buf, size) != (int)size) {
        kprint("error leyendo archivo\n");
        kfree(buf);
        return;
    }
    uint32_t pd, entry, base = 0;
    int is_pe = (size >= 2 && ((uint8_t *)buf)[0] == 'M'
                 && ((uint8_t *)buf)[1] == 'Z');
    int r = is_pe ? pe_load(buf, size, &pd, &entry, &base)
                  : elf_load(buf, size, &pd, &entry);
    if (r != 0) {
        kprint(is_pe ? "PE invalido\n" : "ELF invalido\n");
        kfree(buf);
        return;
    }
    int mapr = win32_map_all(pd);
    kprint_uint((uint32_t)mapr);
    kprint("(map) ");
    kfree(buf);
    if (task_create_user("user", exe_nm, cmdline, pd, entry, base) < 0) {
        paging_free_pd(pd);
        kprint("no se pudo crear la tarea\n");
        return;
    }
    kprint("ok(create)\n");
    kprint("Ejecutando ");
    kprint(exe_nm);
    kprint(" en ring 3 (PD aislado)...\n");
}

/* --- Fase 22: autoboot del escritorio ---
 * Si el superbloque tiene el flag boot_gui y existe desktop.elf, espera
 * 3 s leyendo teclado/serial sin bloquear: si el usuario pulsa una
 * tecla entra en la consola; si no, lanza el escritorio y la shell
 * queda viva por debajo (al cerrar el desktop con 'q' el WM restaura
 * la consola y el prompt vuelve). */
void shell_autoboot(void)
{
    uint32_t start;
    int key;

    if (!mefs_boot_gui())
        return;
    if (mefs_size("desktop.elf") == 0)
        return;

    kprint("Autoboot: escritorio en 3 s (pulsa una tecla para la shell)...\n");
    start = timer_get_ticks();
    while (timer_get_ticks() - start < 300) {
        key = keyboard_read();
        if (key < 0)
            key = serial_read_char();
        if (key >= 0) {
            kprint("Autoboot cancelado - consola\n");
            return;
        }
        halt();
    }
    kprint("Autoboot: lanzando escritorio...\n");
    shell_run_file("desktop.elf");
}
