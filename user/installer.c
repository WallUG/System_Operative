/* MyOS - user/installer.c
 * Instalador tipo Windows (Fase 23-A4): crea un directorio persistente
 * "installed/" en el FS y copia archivos a el con estructura.
 *
 * Pasos (todo persistente via flush al final):
 *   1. crea el subdirectorio "installed/" (SYS_MKDIR, parent = raiz).
 *   2. copia el contenido de "readme.txt" a "installed/readme_inst.txt"
 *      (SYS_DREAD del origen + SYS_FCREATE_IN + SYS_FWRITE al destino).
 *   3. crea "installed/version.txt" con un banner de la instalacion.
 *   4. flush() para persistir superbloque + directorio al disco.
 * Imprime el progreso al serial para el test del host. */

#include <stdint.h>
#include "winlib.h"

static void log_line(const char *s)
{
    sys_write(s, wl_strlen(s));
}

static void log_num(uint32_t v)
{
    char b[12];
    uint32_t n = wl_dec(b, v);
    sys_write(b, n);
}

int _start(void)
{
    char tmp[260];
    static const char banner[] =
        "MyOS Installer 1.0\n"
        "Copiando archivos a installed/ ...\n";
    int got;

    log_line("inst: instalador iniciando\n");

    /* 1) crear el subdirectorio destino en la raiz */
    if (sys_mkdir(MEFS_ROOT, "installed") != 0) {
        log_line("inst: mkdir installed fallo\n");
        return 1;
    }
    log_line("inst: creado installed/\n");

    /* 2) copiar readme.txt -> installed/readme_inst.txt */
    got = sys_dread("readme.txt", tmp, 0, 256);
    if (got < 0) {
        log_line("inst: no puedo leer readme.txt\n");
        return 1;
    }
    if (sys_fcreate_in((uint32_t)sys_dlookup(MEFS_ROOT, "installed"),
                       "readme_inst.txt") != 0) {
        log_line("inst: crear readme_inst.txt fallo\n");
        return 1;
    }
    if (sys_fwrite("readme_inst.txt", tmp, (uint32_t)got) != 0) {
        log_line("inst: escribir readme_inst.txt fallo\n");
        return 1;
    }
    log_line("inst: copiado readme.txt -> installed/readme_inst.txt (");
    log_num((uint32_t)got);
    log_line(" bytes)\n");

    /* 3) escribir version.txt con el banner */
    if (sys_fcreate_in((uint32_t)sys_dlookup(MEFS_ROOT, "installed"),
                       "version.txt") != 0) {
        log_line("inst: crear version.txt fallo\n");
        return 1;
    }
    if (sys_fwrite("version.txt", banner, sizeof(banner) - 1) != 0) {
        log_line("inst: escribir version.txt fallo\n");
        return 1;
    }
    log_line("inst: escrito installed/version.txt\n");

    /* 4) persistir */
    sys_flush();
    log_line("inst: flush ok - instalacion completa\n");
    return 0;
}