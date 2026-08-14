#ifndef MYOS_BOOTSCREEN_H
#define MYOS_BOOTSCREEN_H

/* Pantalla de carga (Fase 22): dibuja al LFB la barra de progreso del
 * arranque. bootscreen_start() pinta la base; bootscreen_status() la
 * actualiza con la fase actual; bootscreen_done() restaura la consola. */
void bootscreen_start(void);
void bootscreen_status(const char *fase, int pct);
void bootscreen_done(void);
/* 1 mientras la pantalla de carga cubre el LFB: kprint solo va al
 * serial para no pisar la animacion (Fase 22-fix). */
int bootscreen_active(void);

#endif