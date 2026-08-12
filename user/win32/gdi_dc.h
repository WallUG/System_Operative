/* MyOS - user/win32/gdi_dc.h
 * Estructura del DC de dibujo de gdi32.dll (slice 2 del Hito B).
 * user32 (GetDC/ReleaseDC) crea el DC apuntando al buffer del cliente
 * de la ventana (buf_va de SYS_WINCREATE, formato LFB: 0x00RRGGBB
 * logico, px_disp al escribir); gdi32 dibuja dentro de el.
 *
 * Un DC para un HIJO virtual (clases built-in como RichEdit20A) lleva
 * buf = buffer del PADRE y (ox, oy) = origen del hijo en el cliente
 * del padre: las coordenadas GDI del hijo se desplazan al buffer. */
#ifndef MYOS_GDI_DC_H
#define MYOS_GDI_DC_H

#include <stdint.h>

#define GDI_DC_MAGIC  0x3143444Du   /* 'DC1' + M */

#define GDI_BK_TRANSPARENT 1
#define GDI_BK_OPAQUE      2

typedef struct {
    uint32_t magic;
    uint32_t buf;          /* buffer del cliente (pixel = px_disp) */
    int      cw, ch;       /* tamano del cliente (buffer)         */
    int      ox, oy;       /* origen (hijo virtual: rect en padre) */
    uint32_t fg;           /* SetTextColor: 0x00RRGGBB logico      */
    uint32_t bg;           /* SetBkColor                           */
    int      bk_mode;      /* GDI_BK_OPAQUE/TRANSPARENT            */
    uint32_t font;         /* handle de fuente seleccionada        */
    uint32_t brush;        /* handle de brush seleccionado         */
    uint32_t pen;          /* handle de pen seleccionado           */
    int      pen_x, pen_y; /* posicion actual del pen              */
} myos_dc_t;

#endif
