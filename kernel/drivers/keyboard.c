/* MyOS - kernel/drivers/keyboard.c
 * Teclado PS/2 (IRQ1), scancode set 1. El IRQ1 handler decodifica la
 * tecla y empuja un EV_KEY a la cola grafica (mouse_event_push_key):
 *  - teclas imprimibles: key = caracter ASCII (mayusculas con shift o
 *    caps, simbolos con shift).
 *  - teclas especiales: key = 0x100 + indice (VK_*_SPECIAL abajo).
 *  - el campo `buttons` del evento lleva los modificadores pulsados:
 *    bit0=ctrl, bit1=alt, bit2=shift (para que el WM/EDIT distinga
 *    Ctrl+A de 'a'). El teclado de consola (keyboard_read) sigue
 *    recibiendo los caracteres imprimibles.
 * La tabla de scancodes del set 1 es la estandar US. */

#include <stdint.h>
#include "keyboard.h"
#include "io.h"
#include "mouse.h"

#define KBD_DATA   0x60
#define KBD_BUF    64

static volatile char kbd_buffer[KBD_BUF];
static volatile int kbd_head = 0;   /* posicion de escritura */
static volatile int kbd_tail = 0;   /* posicion de lectura */
static volatile int kbd_shift = 0;  /* Shift izq/der pulsado */
static volatile int kbd_ctrl = 0;   /* Ctrl pulsado */
static volatile int kbd_alt = 0;    /* Alt pulsado */
static volatile int kbd_caps = 0;   /* bloqueo de mayusculas */

/* Scancodes set 1 de make/break de los modificadores. */
#define KBD_SC_LSHIFT_DOWN 0x2A
#define KBD_SC_LSHIFT_UP   0xAA
#define KBD_SC_RSHIFT_DOWN 0x36
#define KBD_SC_RSHIFT_UP   0xB6
#define KBD_SC_LCTRL_DOWN  0x1D
#define KBD_SC_LCTRL_UP    0x9D
#define KBD_SC_LALT_DOWN   0x38
#define KBD_SC_LALT_UP     0xB8
#define KBD_SC_CAPS_DOWN   0x3A
#define KBD_SC_CAPS_UP     0xBA

/* Codigos de teclas especiales entregados en EV_KEY (key = 0x100 + n).
 * Coinciden con los VK de Windows salvo el offset. */
#define VK_SPECIAL_LEFT   0x100
#define VK_SPECIAL_RIGHT  0x101
#define VK_SPECIAL_UP     0x102
#define VK_SPECIAL_DOWN   0x103
#define VK_SPECIAL_HOME   0x104
#define VK_SPECIAL_END    0x105
#define VK_SPECIAL_PGUP   0x106
#define VK_SPECIAL_PGDN   0x107
#define VK_SPECIAL_DEL    0x108
#define VK_SPECIAL_INS    0x109
/* Teclas de funcion: se entregan con su VK de Windows real (0x70..0x7B)
 * para que los aceleradores (Ctrl+F2 = Save As) los reconozcan. */
#define VK_F1  0x70
#define VK_F2  0x71
#define VK_F3  0x72
#define VK_F4  0x73
#define VK_F5  0x74
#define VK_F6  0x75
#define VK_F7  0x76
#define VK_F8  0x77
#define VK_F9  0x78
#define VK_F10 0x79
#define VK_F11 0x7A
#define VK_F12 0x7B

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
    /* 0x40-0x4F (F1-F10, keypad 7..1; F1-F5 no tienen caracter) */
    0, 0, 0, 0, 0, 0, 0, '7', '8', '9', '-', '4', '5', '6', '+', '1',
    /* 0x50-0x5F (keypad 2,3,0,., F11, F12) */
    '2', '3', '0', '.', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* 0x60-0x7F: no definidos en set 1 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

/* Caracteres con shift para digitos/simbolos (misma posicion que la
 * tabla base: '1'->'!', '-',->'_', etc.). */
static char shifted_char(char c)
{
    switch (c) {
    case '1': return '!';
    case '2': return '@';
    case '3': return '#';
    case '4': return '$';
    case '5': return '%';
    case '6': return '^';
    case '7': return '&';
    case '8': return '*';
    case '9': return '(';
    case '0': return ')';
    case '-': return '_';
    case '=': return '+';
    case '[': return '{';
    case ']': return '}';
    case '\\': return '|';
    case ';': return ':';
    case '\'': return '"';
    case '`': return '~';
    case ',': return '<';
    case '.': return '>';
    case '/': return '?';
    default:
        if (c >= 'a' && c <= 'z')
            return c - 'a' + 'A';
        return c;
    }
}

void keyboard_init(void)
{
    kbd_head = 0;
    kbd_tail = 0;
    kbd_shift = 0;

    /* El bootloader (enable_a20) deja el teclado deshabilitado en el 8042
     * (comando 0xAD) y nunca lo re-habilita: sin esto el 8042 ignora el
     * teclado (ni IRQ1 ni datos en 0x60). */
    while (inb(0x64) & 0x02)        /* esperar input buffer libre */
        ;
    outb(0x64, 0xAE);               /* enable keyboard */
    while (inb(0x64) & 0x01)        /* drenar bytes pendientes */
        inb(0x60);

    /* Habilitar la IRQ del teclado (bit 0) en el byte de config del 8042.
     * QEMU no levanta IRQ1 mientras ese bit este a 0 (el raton lo hace en
     * mouse_init con el bit 1): sin esto los scancodes se acumulan en 0x60
     * sin interrumpir. */
    while (inb(0x64) & 0x02)
        ;
    outb(0x64, 0x20);               /* lee config byte */
    while (!(inb(0x64) & 0x01))
        ;
    {
        uint8_t config = inb(0x60);
        while (inb(0x64) & 0x02)
            ;
        outb(0x64, 0x60);           /* escribe config byte */
        while (inb(0x64) & 0x02)
            ;
        outb(0x60, config | 0x01);
    }
}

void keyboard_irq(void)
{
    uint8_t scancode = inb(KBD_DATA);

    /* Scancodes extendidos (prefijo 0xE0): teclas de edicion, enter del
     * teclado numerico, etc. Se ignoran (la tecla base coincide). */
    static int ext = 0;
    if (scancode == 0xE0) {
        ext = 1;
        return;
    }

    switch (scancode) {
    case KBD_SC_LSHIFT_DOWN:
    case KBD_SC_RSHIFT_DOWN:
        kbd_shift = 1;
        ext = 0;
        return;
    case KBD_SC_LSHIFT_UP:
    case KBD_SC_RSHIFT_UP:
        kbd_shift = 0;
        ext = 0;
        return;
    case KBD_SC_LCTRL_DOWN:
        kbd_ctrl = 1;
        ext = 0;
        return;
    case KBD_SC_LCTRL_UP:
        kbd_ctrl = 0;
        ext = 0;
        return;
    case KBD_SC_LALT_DOWN:
        kbd_alt = 1;
        ext = 0;
        return;
    case KBD_SC_LALT_UP:
        kbd_alt = 0;
        ext = 0;
        return;
    case KBD_SC_CAPS_DOWN:
        if (!ext)
            kbd_caps = !kbd_caps;
        ext = 0;
        return;
    case KBD_SC_CAPS_UP:
        ext = 0;
        return;
    default:
        break;
    }
    /* bit 7 = key release: solo refrescar estado */
    if (scancode & 0x80) {
        ext = 0;
        return;
    }

    /* Teclas especiales (sin caracter): flechas y edicion. El scancode
     * es el del set 1 (0x47..0x53, igual en el keypad y extendidas). */
    {
        int special = -1;
        switch (scancode) {
        case 0x4B: special = VK_SPECIAL_LEFT; break;
        case 0x4D: special = VK_SPECIAL_RIGHT; break;
        case 0x48: special = VK_SPECIAL_UP; break;
        case 0x50: special = VK_SPECIAL_DOWN; break;
        case 0x47: special = VK_SPECIAL_HOME; break;
        case 0x4F: special = VK_SPECIAL_END; break;
        case 0x49: special = VK_SPECIAL_PGUP; break;
        case 0x51: special = VK_SPECIAL_PGDN; break;
        case 0x53: special = VK_SPECIAL_DEL; break;
        case 0x52: special = VK_SPECIAL_INS; break;
        case 0x3B: special = VK_F1; break;
        case 0x3C: special = VK_F2; break;
        case 0x3D: special = VK_F3; break;
        case 0x3E: special = VK_F4; break;
        case 0x3F: special = VK_F5; break;
        case 0x40: special = VK_F6; break;
        case 0x41: special = VK_F7; break;
        case 0x42: special = VK_F8; break;
        case 0x43: special = VK_F9; break;
        case 0x44: special = VK_F10; break;
        case 0x57: special = VK_F11; break;
        case 0x58: special = VK_F12; break;
        default: break;
        }
        if (special >= 0) {
            int mods = (kbd_ctrl ? 1 : 0) | (kbd_alt ? 2 : 0) |
                       (kbd_shift ? 4 : 0);
            mouse_event_push_key_ext(special, mods);
            ext = 0;
            return;
        }
    }

    char c = scancode_table[scancode & 0x7F];
    ext = 0;
    if (c == 0)
        return;
    if (kbd_shift) {
        c = shifted_char(c);
    } else if (kbd_caps && c >= 'a' && c <= 'z') {
        c -= 'a' - 'A';
    }
    {
        int mods = (kbd_ctrl ? 1 : 0) | (kbd_alt ? 2 : 0) |
                   (kbd_shift ? 4 : 0);
        mouse_event_push_key_ext(c, mods);
    }

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
