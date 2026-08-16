/* MyOS - kernel/drivers/ac97.h */
#ifndef AC97_H
#define AC97_H

int ac97_init(void);
void ac97_beep(uint32_t ms);
/* Fase 25-W2A: reproduce PCM 16-bit stereo (en RAM del kernel) a la
 * tasa dada, en trozos de 1 s (poll del SR). 0 = OK. */
int ac97_play_kernel(const uint8_t *data, uint32_t bytes, uint32_t rate);

#endif