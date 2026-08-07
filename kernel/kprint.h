/* MyOS - kernel/kprint.h */
#ifndef MYOS_KPRINT_H
#define MYOS_KPRINT_H

#include <stdint.h>

/* Imprime un entero decimal sin signo en VGA y COM1 (ambos). */
void kprint_uint(uint32_t n);

/* Imprime una cadena en VGA y COM1. */
void kprint(const char *s);

/* Imprime un uint32 en hexadecimal (0x + 8 digitos) en VGA y COM1. */
void kprint_hex32(uint32_t n);

#endif
