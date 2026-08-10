/* MyOS - kernel/drivers/vga.c
 * Driver de texto VGA (modo 3, 80x25). Celda = 2 bytes: char + atributo.
 * Memoria en 0xB8000, cursor hardware en puertos 0x3D4/0x3D5.
 * Este driver vive en la direccion fija 0xB8000 (mapeo identity);
 * en Fase 5+ se puede abstraer detras de una interfaz de driver. */

#include <stdint.h>
#include "vga.h"
#include "io.h"
#include "string.h"
#include "vbe.h"
#include "vgafx.h"

#define VGA_BASE    0xB8000
#define VGA_WIDTH   80
#define VGA_HEIGHT  25

static int vga_row = 0;
static int vga_col = 0;
static uint8_t vga_attr = 0x0A;         /* verde claro sobre negro */

static void vga_move_cursor(void)
{
    uint16_t pos = (uint16_t)(vga_row * VGA_WIDTH + vga_col);
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)(pos >> 8));
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
}

static void vga_scroll(void)
{
    uint8_t *base = (uint8_t *)VGA_BASE;
    memmove(base, base + VGA_WIDTH * 2, VGA_WIDTH * 2 * (VGA_HEIGHT - 1));
    for (int i = 0; i < VGA_WIDTH; i++) {
        base[(VGA_HEIGHT - 1) * VGA_WIDTH * 2 + i * 2]     = ' ';
        base[(VGA_HEIGHT - 1) * VGA_WIDTH * 2 + i * 2 + 1] = vga_attr;
    }
}

void vga_init(void)
{
    vga_clear();
    vga_move_cursor();
}

void vga_clear(void)
{
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        ((uint16_t *)VGA_BASE)[i] = (uint16_t)' ' | ((uint16_t)vga_attr << 8);
    }
    vga_row = 0;
    vga_col = 0;
}

void vga_putc(char c)
{
    /* En modo grafico (VBE activo) el buffer 0xB8000 no se muestra:
     * dibujar en el LFB con la consola grafica. */
    if (vbe_graphics_active) {
        vgafx_putc(c);
        return;
    }
    switch (c) {
    case '\n':
        vga_row++;
        vga_col = 0;
        break;
    case '\r':
        vga_col = 0;
        break;
    case '\t':
        vga_col = (vga_col + 8) & ~7;
        break;
    case '\b':
        if (vga_col > 0)
            vga_col--;
        break;
    default:
        {
            uint16_t *cell = (uint16_t *)VGA_BASE + vga_row * VGA_WIDTH + vga_col;
            *cell = (uint16_t)c | ((uint16_t)vga_attr << 8);
            vga_col++;
        }
    }
    if (vga_col >= VGA_WIDTH) {
        vga_col = 0;
        vga_row++;
    }
    if (vga_row >= VGA_HEIGHT) {
        vga_scroll();
        vga_row = VGA_HEIGHT - 1;
    }
    vga_move_cursor();
}

void vga_puts(const char *s)
{
    while (*s)
        vga_putc(*s++);
}
