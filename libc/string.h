/* MyOS - libc/string.h
 * Mini-libc freestanding. Estas funciones deben existir porque el
 * compilador puede generar llamadas implicitas a ellas. */

#ifndef MYOS_STRING_H
#define MYOS_STRING_H

#include <stddef.h>

void *memset(void *dest, int val, size_t len);
void *memcpy(void *dest, const void *src, size_t len);
void *memmove(void *dest, const void *src, size_t len);
int   memcmp(const void *a, const void *b, size_t len);
size_t strlen(const char *s);
int   strcmp(const char *a, const char *b);
int   strncmp(const char *a, const char *b, size_t len);

#endif
