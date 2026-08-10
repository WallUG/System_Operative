/* MyOS - kernel/drivers/mouse.c
 * Raton PS/2 (IRQ12), Fase 13. Paquete estandar de 3 bytes:
 *   byte0: bit0 L, bit1 R, bit2 M, bit3 = 1 (sync), bit4 signo X,
 *          bit5 signo Y, bit6 overflow X, bit7 overflow Y.
 *   byte1: delta X con signo; byte2: delta Y con signo (positivo =
 *          abajo en pantalla: y -= dy).
 * El handler es minimo (solo puertos y aritmetica, sin kprint): actualiza
 * la posicion (saturada a la pantalla) y los botones, y redibuja el cursor
 * en el LFB con save/restore de la zona que pisa. */

#include <stdint.h>
#include "mouse.h"
#include "io.h"
#include "vbe.h"

#define PS2_DATA    0x60
#define PS2_CMD     0x64
#define PS2_STAT_OBF   0x01        /* 0x60 tiene un byte listo */
#define PS2_STAT_IBF   0x02        /* no escribir hasta que se limpie */

#define MOUSE_CUR_W 8
#define MOUSE_CUR_H 8

#define EV_QUEUE_MAX 64

static volatile int mouse_x = 0;
static volatile int mouse_y = 0;
static volatile int mouse_buttons = 0;

static int mouse_state = 0;         /* 0..2: bytes del paquete pendientes */
static uint8_t mouse_pkt[3];
static uint8_t mouse_buttons_prev = 0;

static mouse_event_t ev_queue[EV_QUEUE_MAX];
static volatile int ev_head = 0;    /* posicion de escritura */
static volatile int ev_tail = 0;    /* posicion de lectura */

static void event_push(int type, int x, int y, int buttons, int key)
{
    int next = (ev_head + 1) % EV_QUEUE_MAX;
    if (next == ev_tail)            /* cola llena: descartar */
        return;
    ev_queue[ev_head].type = type;
    ev_queue[ev_head].x = x;
    ev_queue[ev_head].y = y;
    ev_queue[ev_head].buttons = buttons;
    ev_queue[ev_head].key = key;
    ev_head = next;
}

/* Flecha 8x8 (blanco sobre lo que haya; el fondo queda transparente). */
static const unsigned char cursor_bits[MOUSE_CUR_H] = {
    0x80, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC, 0xFE, 0xFF
};

static uint32_t cursor_save[MOUSE_CUR_W * MOUSE_CUR_H];
static int cursor_drawn = 0;
static int cursor_sx = 0, cursor_sy = 0;

static void ps2_wait_out(void)
{
    while (inb(PS2_CMD) & PS2_STAT_IBF)
        ;
}

static void ps2_wait_in(void)
{
    while (!(inb(PS2_CMD) & PS2_STAT_OBF))
        ;
}

void mouse_init(void)
{
    uint8_t config;

    /* Drenar basura pendiente en 0x60. */
    while (inb(PS2_CMD) & PS2_STAT_OBF)
        inb(PS2_DATA);

    /* 1. Activar el dispositivo auxiliar (0xA8). */
    ps2_wait_out();
    outb(PS2_CMD, 0xA8);

    /* 2. Config byte: habilitar IRQ12 (bit 1) y quitar translate (bit 5),
     *    que convierte el paquete de 3 bytes a scancodes. */
    ps2_wait_out();
    outb(PS2_CMD, 0x20);
    ps2_wait_in();
    config = inb(PS2_DATA);
    config |= 0x02;
    config &= ~0x20;
    ps2_wait_out();
    outb(PS2_CMD, 0x60);
    ps2_wait_out();
    outb(PS2_DATA, config);

    /* 3. Raton: defaults (0xF6) y modo stream (0xF4), vía 0xD4. */
    ps2_wait_out();
    outb(PS2_CMD, 0xD4);
    ps2_wait_out();
    outb(PS2_DATA, 0xF6);
    ps2_wait_out();
    outb(PS2_CMD, 0xD4);
    ps2_wait_out();
    outb(PS2_DATA, 0xF4);

    /* 4. Drenar los dos acks (0xFA) para que no corrompan paquetes. */
    ps2_wait_in();
    inb(PS2_DATA);
    ps2_wait_in();
    inb(PS2_DATA);

    mouse_state = 0;
    mouse_x = VBE_SCREEN_W / 2;
    mouse_y = VBE_SCREEN_H / 2;
    mouse_buttons = 0;
}

void mouse_irq(void)
{
    uint8_t b = inb(PS2_DATA);

    if (mouse_state == 0) {
        if (!(b & 0x08))            /* bit3 = sync; si no, descartar */
            return;
        mouse_pkt[0] = b;
        mouse_state = 1;
        return;
    }
    if (mouse_state == 1) {
        mouse_pkt[1] = b;
        mouse_state = 2;
        return;
    }
    {
        int dx, dy;
        mouse_pkt[2] = b;
        mouse_state = 0;
        if (mouse_pkt[0] & 0xC0)    /* overflow X/Y: paquete invalido */
            return;
        mouse_buttons = mouse_pkt[0] & 0x07;
        if (mouse_buttons != mouse_buttons_prev) {
            if (mouse_buttons & ~mouse_buttons_prev)
                event_push(EV_BUTTON_DOWN, mouse_x, mouse_y,
                           mouse_buttons, 0);
            if (mouse_buttons_prev & ~mouse_buttons)
                event_push(EV_BUTTON_UP, mouse_x, mouse_y,
                           mouse_buttons, 0);
            mouse_buttons_prev = mouse_buttons;
        }
        dx = (int8_t)mouse_pkt[1];
        dy = (int8_t)mouse_pkt[2];
        mouse_x += dx;
        mouse_y -= dy;              /* +Y del PS/2 = abajo en pantalla */
        if (mouse_x < 0)
            mouse_x = 0;
        if (mouse_x >= VBE_SCREEN_W)
            mouse_x = VBE_SCREEN_W - 1;
        if (mouse_y < 0)
            mouse_y = 0;
        if (mouse_y >= VBE_SCREEN_H)
            mouse_y = VBE_SCREEN_H - 1;
        event_push(EV_MOVE, mouse_x, mouse_y, mouse_buttons, 0);
        mouse_draw_cursor();
    }
}

void mouse_draw_cursor(void)
{
    volatile uint32_t *lfb;
    int i, j;

    if (!vbe_graphics_active)
        return;
    lfb = (volatile uint32_t *)vbe_lfb_phys;

    /* Restaurar la zona que pisaba el cursor anterior. */
    if (cursor_drawn) {
        for (j = 0; j < MOUSE_CUR_H; j++)
            for (i = 0; i < MOUSE_CUR_W; i++)
                lfb[(cursor_sy + j) * VBE_SCREEN_W + (cursor_sx + i)]
                    = cursor_save[j * MOUSE_CUR_W + i];
    }

    /* Guardar y dibujar en la nueva posicion. */
    for (j = 0; j < MOUSE_CUR_H; j++) {
        int yy = mouse_y + j;
        if (yy >= VBE_SCREEN_H)
            break;
        for (i = 0; i < MOUSE_CUR_W; i++) {
            int xx = mouse_x + i;
            if (xx >= VBE_SCREEN_W)
                break;
            cursor_save[j * MOUSE_CUR_W + i] =
                lfb[yy * VBE_SCREEN_W + xx];
            if (cursor_bits[j] & (0x80u >> i))
                lfb[yy * VBE_SCREEN_W + xx] = 0x00FFFFFFu;
        }
    }
    cursor_sx = mouse_x;
    cursor_sy = mouse_y;
    cursor_drawn = 1;
}

int mouse_read(int *x, int *y, int *buttons)
{
    *x = mouse_x;
    *y = mouse_y;
    *buttons = mouse_buttons;
    return 0;
}

void mouse_event_push_key(int key)
{
    event_push(EV_KEY, 0, 0, 0, key);
}

int mouse_event_dequeue(mouse_event_t *ev)
{
    if (ev_head == ev_tail)
        return -1;
    *ev = ev_queue[ev_tail];
    ev_tail = (ev_tail + 1) % EV_QUEUE_MAX;
    return 0;
}
