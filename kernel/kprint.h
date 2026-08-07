/* MyOS - kernel/kprint.h */
#ifndef MYOS_KPRINT_H
#define MYOS_KPRINT_H

#include <stdint.h>

/* Imprime un entero decimal sin signo en VGA y COM1 (ambos). */
void kprint_uint(uint32_t n);

#endif
