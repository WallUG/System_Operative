/* MyOS - kernel/shell.h */
#ifndef MYOS_SHELL_H
#define MYOS_SHELL_H

/* Bucle de la shell interactiva (Fase 6). No retorna. */
void shell_loop(void);

/* Fase 22: ejecuta un ELF/PE como tarea de usuario (nucleo de "run"). */
void shell_run_file(const char *line);

/* Fase 22: autoboot del escritorio (cuenta atras cancelable). */
void shell_autoboot(void);

#endif
