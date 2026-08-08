/* MyOS - libc/string.c
 * Implementacion freestanding de operaciones de memoria y strings.
 * No usar instrucciones del host: compilar con -m32 -ffreestanding.
 * memmove debe solapar solapamientos correctamente; memcpy puede asumir
 * regiones disjuntas (mas rapido). */

#include "string.h"

void *memset(void *dest, int val, size_t len)
{
    unsigned char *d = (unsigned char *)dest;
    while (len--)
        *d++ = (unsigned char)val;
    return dest;
}

void *memcpy(void *dest, const void *src, size_t len)
{
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    while (len--)
        *d++ = *s++;
    return dest;
}

void *memmove(void *dest, const void *src, size_t len)
{
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    if (d < s) {
        while (len--)
            *d++ = *s++;
    } else if (d > s) {
        d += len;
        s += len;
        while (len--)
            *--d = *--s;
    }
    return dest;
}

int memcmp(const void *a, const void *b, size_t len)
{
    const unsigned char *x = (const unsigned char *)a;
    const unsigned char *y = (const unsigned char *)b;
    while (len--) {
        if (*x != *y)
            return (int)*x - (int)*y;
        x++;
        y++;
    }
    return 0;
}

size_t strlen(const char *s)
{
    size_t n = 0;
    while (s[n])
        n++;
    return n;
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t len)
{
    while (len-- && *a && *a == *b) {
        a++;
        b++;
    }
    if (len == (size_t)-1)
        return 0;
    return (unsigned char)*a - (unsigned char)*b;
}
