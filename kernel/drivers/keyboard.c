/* MyOS - kernel/drivers/keyboard.c
 * Teclado PS/2 (IRQ1), scancode set 1. El IRQ1 handler lee 0x60 y mete el
 * caracter en un buffer circular (el resto del kernel lo consulta con
 * keyboard_read). Teclas especiales (shift/alt/arrows) se ignoran por
 * ahora: solo keydown, tecla base US. */

#include <stdint.h>
#include "keyboard.h"
#include "io.h"

#define KBD_DATA   0x60
#define KBD_BUF    64

static volatile char kbd_buffer[KBD_BUF];
static volatile int kbd_head = 0;   /* posicion de escritura */
static volatile int kbd_tail = 0;   /* posicion de lectura */

/* Scancode set 1 (US QWERTY), tecla base sin shift. 0 = sin caracter. */
static const char scancode_table[128] = {
    /* 0x00-0x0F */
    0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=',
    '\b', '\t',
    /* 0x10-0x1F */
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', 0,
    'a', 's',
    /* 0x20-0x2F */
    'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\', 'z', 'x',
    'c', 'v',
    /* 0x30-0x3F */
    'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' ', 0, 0, 0, 0, 0, 0,
    /* 0x40-0x4F (F6-F10, keypad 7..1) */
    0, 0, 0, 0, 0, 0, 0, '7', '8', '9', '-', '4', '5', '6', '+', '1',
    /* 0x50-0x5F (keypad 2,3,0,., F11, F12) */
    '2', '3', '0', '.', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* 0x60-0x7F: no definidos en set 1 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

void keyboard_init(void)
{
    kbd_head = 0;
    kbd_tail = 0;
}

void keyboard_irq(void)
{
    uint8_t scancode = inb(KBD_DATA);

    /* bit 7 = key release: ignorar (solo keydown) */
    if (scancode & 0x80)
        return;

    char c = scancode_table[scancode & 0x7F];
    if (c == 0)
        return;

    int next = (kbd_head + 1) % KBD_BUF;
    if (next != kbd_tail) {         /* buffer lleno: descartar */
        kbd_buffer[kbd_head] = c;
        kbd_head = next;
    }
}

int keyboard_read(void)
{
    if (kbd_head == kbd_tail)
        return -1;
    char c = kbd_buffer[kbd_tail];
    kbd_tail = (kbd_tail + 1) % KBD_BUF;
    return c;
}
