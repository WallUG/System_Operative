/* MyOS - user/win32/gdi32.c
 * gdi32.dll: modulo Win32 fijo (ring 3) en 0xB4000000.
 * GDI de dibujo (Fase 18 / Hito B slice 2): los HDC los fabrica user32
 * (GetDC/ReleaseDC -> myos_dc_t con puntero al buffer del cliente).
 * Aqui se dibuja: texto (fuente 8x16 del LFB), rectangulos, lineas,
 * pinceles y boligrafos en una tabla de objetos GDI.
 *
 * Objetos: stock = handle index+1 (como Windows); creados = slot*4+0x1000
 * de la tabla gdi_obj[]. */

#include <stdint.h>
#include "gdi_dc.h"
#include "font8x16.h"

#define SYS_GFXINFO 15

/* --- syscalls --- */

static int sys_gfxinfo(uint32_t *info)
{
    int r;
    __asm__ volatile("int $0x80"
                     : "=a"(r)
                     : "a"(SYS_GFXINFO), "b"(info)
                     : "memory");
    return r;
}

/* --- constantes GDI (wingdi.h) --- */

#define MM_TEXT         1

#define WHITE_BRUSH     0
#define LTGRAY_BRUSH    1
#define GRAY_BRUSH      2
#define DKGRAY_BRUSH    3
#define BLACK_BRUSH     4
#define NULL_BRUSH      5
#define WHITE_PEN       6
#define BLACK_PEN       7
#define NULL_PEN        8
#define OEM_FIXED_FONT  10
#define ANSI_FIXED_FONT 11
#define ANSI_VAR_FONT   12
#define SYSTEM_FONT     13
#define DEVICE_DEFAULT_FONT 14
#define SYSTEM_FIXED_FONT 16
#define DEFAULT_GUI_FONT 17

#define ANSI_CHARSET    0
#define DEFAULT_CHARSET 1

#define FIXED_PITCH     0x01
#define FF_MODERN       0x30

#define LOGPIXELSX      88
#define LOGPIXELSY      90
#define BITSPIXEL       12
#define PLANES          14
#define HORZRES         8
#define VERTRES         10
#define HORZSIZE        4
#define VERTSIZE        6

#define PS_SOLID        0
#define PS_NULL         5

#define COLORREF_RGB(r, g, b) (((uint32_t)(r)) | ((uint32_t)(g) << 8) | \
                               ((uint32_t)(b) << 16))

#define STOCK_HANDLE(i) ((uint32_t)(i) + 1)

/* colores de los stock brushes/pens */
static uint32_t stock_color(int i)
{
    switch (i) {
    case WHITE_BRUSH: case WHITE_PEN: return 0x00FFFFFFu;
    case LTGRAY_BRUSH: return 0x00C0C0C0u;
    case GRAY_BRUSH:  return 0x00808080u;
    case DKGRAY_BRUSH: return 0x00404040u;
    default:          return 0x00000000u;  /* BLACK y NULL */
    }
}

/* --- tabla de objetos GDI creados --- */

#define GDI_OBJ_MAX 32
#define GDI_OBJ_TYPE_BRUSH 1
#define GDI_OBJ_TYPE_PEN   2
#define GDI_OBJ_TYPE_FONT  3

typedef struct {
    uint32_t used;
    uint32_t type;
    uint32_t color;
    uint32_t style;     /* pens: PS_SOLID/PS_NULL */
} gdi_obj_t;

static gdi_obj_t gdi_obj[GDI_OBJ_MAX];

#define OBJ_HANDLE(slot) ((uint32_t)(slot) * 4 + 0x1000)
#define OBJ_INDEX(h)     (((h) - 0x1000) / 4)

static gdi_obj_t *obj_lookup(uint32_t h)
{
    uint32_t i;
    if (h < 0x1000 || h >= 0x1000 + GDI_OBJ_MAX * 4)
        return 0;
    i = OBJ_INDEX(h);
    if (i >= GDI_OBJ_MAX || !gdi_obj[i].used)
        return 0;
    return &gdi_obj[i];
}

static uint32_t obj_alloc(uint32_t type, uint32_t color, uint32_t style)
{
    uint32_t i;
    for (i = 0; i < GDI_OBJ_MAX; i++)
        if (!gdi_obj[i].used) {
            gdi_obj[i].used = 1;
            gdi_obj[i].type = type;
            gdi_obj[i].color = color;
            gdi_obj[i].style = style;
            return OBJ_HANDLE(i);
        }
    return 0;
}

/* --- pixelado en el buffer del DC (formato LFB, BGRx en memoria) --- */

static myos_dc_t *dc_check(uint32_t hdc)
{
    if (hdc == 0)
        return 0;
    if (((myos_dc_t *)hdc)->magic != GDI_DC_MAGIC)
        return 0;
    return (myos_dc_t *)hdc;
}

static inline uint32_t px_disp(uint32_t c)
{
    return ((c & 0x000000FFu) << 16) |
           (c & 0x0000FF00u) |
           ((c >> 16) & 0x000000FFu) |
           (c & 0xFF000000u);
}

static void dc_dirty(myos_dc_t *dc, int x, int y, int w, int h);

/* pinta un pixel en coords locales del DC (clips al buffer) */
static void dc_px(myos_dc_t *dc, int x, int y, uint32_t c)
{
    int ax, ay;
    if (dc->buf == 0)
        return;
    ax = dc->ox + x;
    ay = dc->oy + y;
    if (ax < 0 || ay < 0 || ax >= dc->cw || ay >= dc->ch)
        return;
    ((uint32_t *)dc->buf)[(uint32_t)ay * (uint32_t)dc->cw + (uint32_t)ax] =
        px_disp(c);
    dc_dirty(dc, ax, ay, 1, 1);
}

static void dc_rect(myos_dc_t *dc, int x, int y, int w, int h, uint32_t c)
{
    int i, j;
    dc_dirty(dc, dc->ox + x, dc->oy + y, w, h);
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            dc_px(dc, x + i, y + j, c);
}

/* Fase 20-D: expande el rect sucio del DC (coordenadas buffer). */
static void dc_dirty(myos_dc_t *dc, int x, int y, int w, int h)
{
    int x2 = x + w, y2 = y + h;
    if (w <= 0 || h <= 0)
        return;
    if (dc->dirty_w == 0 || dc->dirty_h == 0) {
        dc->dirty_x = x;
        dc->dirty_y = y;
        dc->dirty_w = w;
        dc->dirty_h = h;
        return;
    }
    if (x < dc->dirty_x) dc->dirty_x = x;
    if (y < dc->dirty_y) dc->dirty_y = y;
    if (x2 > dc->dirty_x + dc->dirty_w) dc->dirty_w = x2 - dc->dirty_x;
    if (y2 > dc->dirty_y + dc->dirty_h) dc->dirty_h = y2 - dc->dirty_y;
}

/* --- stock objects --- */

uint32_t __attribute__((stdcall)) GetStockObject(int index)
{
    switch (index) {
    case WHITE_BRUSH: case LTGRAY_BRUSH: case GRAY_BRUSH:
    case DKGRAY_BRUSH: case BLACK_BRUSH: case NULL_BRUSH:
    case WHITE_PEN: case BLACK_PEN: case NULL_PEN:
    case OEM_FIXED_FONT: case ANSI_FIXED_FONT: case ANSI_VAR_FONT:
    case SYSTEM_FONT: case DEVICE_DEFAULT_FONT: case SYSTEM_FIXED_FONT:
    case DEFAULT_GUI_FONT:
        return STOCK_HANDLE(index);
    default:
        return 0;
    }
}

/* color efectivo de un brush (stock o creado) */
static uint32_t brush_color(uint32_t h)
{
    gdi_obj_t *o;
    int idx;
    if (h == 0)
        return 0x00FFFFFFu;
    if (h < 0x1000) {
        idx = (int)h - 1;
        if (idx >= WHITE_BRUSH && idx <= NULL_BRUSH)
            return stock_color(idx);
        return 0x00FFFFFFu;
    }
    o = obj_lookup(h);
    if (o && o->type == GDI_OBJ_TYPE_BRUSH)
        return o->color;
    return 0x00FFFFFFu;
}

/* color efectivo de un pen (stock o creado); NULL_PEN devuelve 0x80000000
 * (bandera "no dibujar") */
static uint32_t pen_color(uint32_t h, int *draw)
{
    gdi_obj_t *o;
    int idx;
    *draw = 1;
    if (h == 0) {
        *draw = 0;
        return 0;
    }
    if (h < 0x1000) {
        idx = (int)h - 1;
        if (idx == NULL_PEN) {
            *draw = 0;
            return 0;
        }
        if (idx >= WHITE_PEN && idx <= NULL_PEN)
            return stock_color(idx);
        *draw = 0;
        return 0;
    }
    o = obj_lookup(h);
    if (o && o->type == GDI_OBJ_TYPE_PEN) {
        if (o->style == PS_NULL)
            *draw = 0;
        return o->color;
    }
    *draw = 0;
    return 0;
}

/* --- objetos --- */

uint32_t __attribute__((stdcall)) CreateSolidBrush(uint32_t color)
{
    return obj_alloc(GDI_OBJ_TYPE_BRUSH, color, PS_SOLID);
}

uint32_t __attribute__((stdcall)) CreatePen(int style, int width, uint32_t color)
{
    (void)width;
    return obj_alloc(GDI_OBJ_TYPE_PEN, color,
                     (uint32_t)(style == PS_NULL ? PS_NULL : PS_SOLID));
}

uint32_t __attribute__((stdcall)) CreateFontIndirectA(const void *logfont)
{
    (void)logfont;
    return obj_alloc(GDI_OBJ_TYPE_FONT, 0, 0);
}

uint32_t __attribute__((stdcall)) SelectObject(uint32_t hdc, uint32_t obj)
{
    myos_dc_t *dc = dc_check(hdc);
    gdi_obj_t *o;
    uint32_t prev;
    if (dc == 0)
        return STOCK_HANDLE(SYSTEM_FONT);
    if (obj == 0)
        return 0;
    if (obj >= 0x1000) {
        o = obj_lookup(obj);
        if (o == 0)
            return 0;
        switch (o->type) {
        case GDI_OBJ_TYPE_BRUSH:
            prev = dc->brush;
            dc->brush = obj;
            return prev ? prev : STOCK_HANDLE(WHITE_BRUSH);
        case GDI_OBJ_TYPE_PEN:
            prev = dc->pen;
            dc->pen = obj;
            return prev ? prev : STOCK_HANDLE(BLACK_PEN);
        default:
            prev = dc->font;
            dc->font = obj;
            return prev ? prev : STOCK_HANDLE(SYSTEM_FONT);
        }
    }
    if (obj >= WHITE_BRUSH + 1 && obj <= NULL_BRUSH + 1) {
        prev = dc->brush;
        dc->brush = obj;
        return prev ? prev : STOCK_HANDLE(WHITE_BRUSH);
    }
    if (obj >= WHITE_PEN + 1 && obj <= NULL_PEN + 1) {
        prev = dc->pen;
        dc->pen = obj;
        return prev ? prev : STOCK_HANDLE(BLACK_PEN);
    }
    /* fuente stock (incluye DEFAULT_GUI_FONT): no afecta al dibujo */
    prev = dc->font;
    dc->font = obj;
    return prev ? prev : STOCK_HANDLE(SYSTEM_FONT);
}

uint32_t __attribute__((stdcall)) DeleteObject(uint32_t obj)
{
    gdi_obj_t *o = obj_lookup(obj);
    if (obj < 0x1000)
        return 0;                      /* stock: nunca se borran */
    if (o == 0)
        return 0;
    o->used = 0;
    return 1;
}

/* --- atributos del DC --- */

uint32_t __attribute__((stdcall)) SetTextColor(uint32_t hdc, uint32_t c)
{
    myos_dc_t *dc = dc_check(hdc);
    if (dc == 0)
        return 0;
    dc->fg = c;
    return c;
}

uint32_t __attribute__((stdcall)) GetTextColor(uint32_t hdc)
{
    myos_dc_t *dc = dc_check(hdc);
    return dc ? dc->fg : 0;
}

uint32_t __attribute__((stdcall)) SetBkColor(uint32_t hdc, uint32_t c)
{
    myos_dc_t *dc = dc_check(hdc);
    if (dc == 0)
        return 0;
    dc->bg = c;
    return c;
}

uint32_t __attribute__((stdcall)) GetBkColor(uint32_t hdc)
{
    myos_dc_t *dc = dc_check(hdc);
    return dc ? dc->bg : 0;
}

uint32_t __attribute__((stdcall)) SetBkMode(uint32_t hdc, uint32_t mode)
{
    myos_dc_t *dc = dc_check(hdc);
    if (dc == 0)
        return 0;
    dc->bk_mode = mode;
    return mode;
}

uint32_t __attribute__((stdcall)) GetBkMode(uint32_t hdc)
{
    myos_dc_t *dc = dc_check(hdc);
    return dc ? dc->bk_mode : 0;
}

uint32_t __attribute__((stdcall)) SetMapMode(uint32_t hdc, uint32_t mode)
{
    (void)hdc;
    (void)mode;
    return MM_TEXT;
}

/* --- texto --- */

uint32_t __attribute__((stdcall)) TextOutA(uint32_t hdc, int x, int y, const char *s, int n)
{
    myos_dc_t *dc = dc_check(hdc);
    int i, k;
    if (dc == 0 || s == 0)
        return 0;
    for (i = 0; i < n; i++) {
        char c = s[i];
        const unsigned char *g;
        if (c < 32 || c > 126)
            c = '?';
        g = font8x16_basic[c - 32];
        for (k = 0; k < 16; k++) {
            int yy = y + k, kk;
            for (kk = 0; kk < 8; kk++)
                if (g[k] & (0x80u >> kk)) {
                    if (dc->bk_mode != GDI_BK_TRANSPARENT)
                        dc_px(dc, x + i * 8 + kk, yy, dc->bg);
                    dc_px(dc, x + i * 8 + kk, yy, dc->fg);
                } else if (dc->bk_mode != GDI_BK_TRANSPARENT) {
                    dc_px(dc, x + i * 8 + kk, yy, dc->bg);
                }
        }
    }
    return 1;
}

uint32_t __attribute__((stdcall)) GetTextExtentPoint32A(uint32_t hdc, const char *s, int n, int32_t *sz)
{
    (void)hdc;
    (void)s;
    if (sz == 0)
        return 0;
    sz[0] = n * 8;
    sz[1] = 16;
    return 1;
}

/* --- figuras --- */

uint32_t __attribute__((stdcall)) FillRect(uint32_t hdc, const int32_t *rc, uint32_t brush)
{
    myos_dc_t *dc = dc_check(hdc);
    uint32_t c;
    int x, y, w, h;
    if (dc == 0 || rc == 0)
        return 0;
    c = brush_color(brush);
    x = rc[0];
    y = rc[1];
    w = rc[2] - rc[0];
    h = rc[3] - rc[1];
    if (w < 0) { x += w; w = -w; }
    if (h < 0) { y += h; h = -h; }
    dc_rect(dc, x, y, w, h, c);
    return 1;
}

uint32_t __attribute__((stdcall)) PatBlt(uint32_t hdc, int x, int y, int w, int h, uint32_t rop)
{
    myos_dc_t *dc = dc_check(hdc);
    (void)rop;
    if (dc == 0)
        return 0;
    dc_rect(dc, x, y, w, h, brush_color(dc->brush));
    return 1;
}

uint32_t __attribute__((stdcall)) Rectangle(uint32_t hdc, int l, int t, int r, int b)
{
    myos_dc_t *dc = dc_check(hdc);
    int draw;
    uint32_t c;
    int x, y;
    if (dc == 0)
        return 0;
    if (l > r) { int t2 = l; l = r; r = t2; }
    if (t > b) { int t2 = t; t = b; b = t2; }
    dc_rect(dc, l, t, r - l, b - t, brush_color(dc->brush));
    c = pen_color(dc->pen, &draw);
    if (draw) {
        for (x = l; x <= r; x++) {
            dc_px(dc, x, t, c);
            dc_px(dc, x, b, c);
        }
        for (y = t; y <= b; y++) {
            dc_px(dc, l, y, c);
            dc_px(dc, r, y, c);
        }
    }
    return 1;
}

static void dc_line(myos_dc_t *dc, int x0, int y0, int x1, int y1, uint32_t c)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int dy = y1 > y0 ? y1 - y0 : y0 - y1;
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy, e2;
    for (;;) {
        dc_px(dc, x0, y0, c);
        if (x0 == x1 && y0 == y1)
            break;
        e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx)  { err += dx; y0 += sy; }
    }
}

uint32_t __attribute__((stdcall)) MoveToEx(uint32_t hdc, int x, int y, uint32_t old)
{
    myos_dc_t *dc = dc_check(hdc);
    if (dc == 0)
        return 0;
    if (old)
        ((int32_t *)old)[0] = dc->pen_x;
    dc->pen_x = x;
    dc->pen_y = y;
    return 1;
}

uint32_t __attribute__((stdcall)) LineTo(uint32_t hdc, int x, int y)
{
    myos_dc_t *dc = dc_check(hdc);
    int draw;
    uint32_t c;
    if (dc == 0)
        return 0;
    c = pen_color(dc->pen, &draw);
    if (draw)
        dc_line(dc, dc->pen_x, dc->pen_y, x, y, c);
    dc->pen_x = x;
    dc->pen_y = y;
    return 1;
}

/* --- metrica de la fuente 8x16 (VGA) --- */

typedef struct {
    int32_t tmHeight, tmAscent, tmDescent, tmInternalLeading;
    int32_t tmExternalLeading, tmAveCharWidth, tmMaxCharWidth, tmWeight;
    int32_t tmItalic, tmUnderline, tmStruckOut, tmFirstChar;
    int32_t tmLastChar, tmDefaultChar, tmBreakChar, tmPitchAndFamily;
    int32_t tmCharSet, tmOverhang, tmDigitizedAspectX, tmDigitizedAspectY;
} TEXTMETRICA;

uint32_t __attribute__((stdcall)) GetTextMetricsA(uint32_t hdc, TEXTMETRICA *tm)
{
    (void)hdc;
    if (tm == 0)
        return 0;
    tm->tmHeight = 16;
    tm->tmAscent = 13;
    tm->tmDescent = 3;
    tm->tmInternalLeading = 3;
    tm->tmExternalLeading = 0;
    tm->tmAveCharWidth = 8;
    tm->tmMaxCharWidth = 8;
    tm->tmWeight = 400;
    tm->tmItalic = 0;
    tm->tmUnderline = 0;
    tm->tmStruckOut = 0;
    tm->tmFirstChar = 32;
    tm->tmLastChar = 126;
    tm->tmDefaultChar = '?';
    tm->tmBreakChar = ' ';
    tm->tmPitchAndFamily = FIXED_PITCH | FF_MODERN;
    tm->tmCharSet = ANSI_CHARSET;
    tm->tmOverhang = 0;
    tm->tmDigitizedAspectX = 96;
    tm->tmDigitizedAspectY = 96;
    return 1;
}

uint32_t __attribute__((stdcall)) GetTextFaceA(uint32_t hdc, uint32_t count, char *buf)
{
    static const char face[] = "MyOS Terminal";
    uint32_t i;
    (void)hdc;
    if (buf == 0)
        return 0;
    for (i = 0; i < count - 1 && face[i]; i++)
        buf[i] = face[i];
    buf[i] = 0;
    return i;
}

uint32_t __attribute__((stdcall)) GetCharWidthA(uint32_t hdc, uint32_t first, uint32_t last,
                       int32_t *widths)
{
    uint32_t i;
    (void)hdc;
    if (widths == 0)
        return 0;
    for (i = first; i <= last; i++)
        widths[i - first] = 8;
    return 1;
}

/* GetDeviceCaps: un display VBE 800x600x32 (o el modo real del LFB). */
uint32_t __attribute__((stdcall)) GetDeviceCaps(uint32_t hdc, uint32_t index)
{
    uint32_t info[4];
    (void)hdc;
    if (sys_gfxinfo(info) == 0) {
        uint32_t w = info[1], h = info[2];
        switch (index) {
        case HORZRES: return w;
        case VERTRES: return h;
        case BITSPIXEL: return 32;
        case PLANES: return 1;
        case LOGPIXELSX: return 96;
        case LOGPIXELSY: return 96;
        case HORZSIZE: return (w * 254 + 959) / 960;
        case VERTSIZE: return (h * 254 + 959) / 960;
        }
    } else {
        switch (index) {
        case HORZRES: return 800;
        case VERTRES: return 600;
        case BITSPIXEL: return 32;
        case PLANES: return 1;
        case LOGPIXELSX: return 96;
        case LOGPIXELSY: return 96;
        }
    }
    return 0;
}

uint32_t __attribute__((stdcall)) DeleteDC(uint32_t hdc)
{
    myos_dc_t *dc = dc_check(hdc);
    if (dc)
        dc->magic = 0;
    else if (hdc)
        return 0;              /* no es nuestro DC: error silencioso */
    return 1;
}

/* --- impresion: stubs que devuelven exito (PrintDlgA de comdlg32
 * devuelve 0, asi que metapad no llega a imprimir). --- */

uint32_t __attribute__((stdcall)) StartDocA(uint32_t hdc, const void *docinfo)
{
    (void)hdc;
    (void)docinfo;
    return 1;
}

uint32_t __attribute__((stdcall)) EndDoc(uint32_t hdc)
{
    (void)hdc;
    return 1;
}

uint32_t __attribute__((stdcall)) StartPage(uint32_t hdc)
{
    (void)hdc;
    return 1;
}

uint32_t __attribute__((stdcall)) EndPage(uint32_t hdc)
{
    (void)hdc;
    return 1;
}

uint32_t __attribute__((stdcall)) AbortDoc(uint32_t hdc)
{
    (void)hdc;
    return 1;
}

uint32_t __attribute__((stdcall)) SetAbortProc(uint32_t hdc, uint32_t proc)
{
    (void)hdc;
    (void)proc;
    return 1;
}

/* --- tabla de exports (kernel/win32.c la copia) --- */

typedef struct {
    char     name[32];
    uint32_t fn;
} win32_export_t;

win32_export_t __exports[] __attribute__((section(".exports"))) = {
    { "GetStockObject",          (uint32_t)&GetStockObject },
    { "CreateSolidBrush",        (uint32_t)&CreateSolidBrush },
    { "CreatePen",               (uint32_t)&CreatePen },
    { "CreateFontIndirectA",     (uint32_t)&CreateFontIndirectA },
    { "SelectObject",            (uint32_t)&SelectObject },
    { "DeleteObject",            (uint32_t)&DeleteObject },
    { "SetTextColor",            (uint32_t)&SetTextColor },
    { "GetTextColor",            (uint32_t)&GetTextColor },
    { "SetBkColor",              (uint32_t)&SetBkColor },
    { "GetBkColor",              (uint32_t)&GetBkColor },
    { "SetBkMode",               (uint32_t)&SetBkMode },
    { "GetBkMode",               (uint32_t)&GetBkMode },
    { "SetMapMode",              (uint32_t)&SetMapMode },
    { "TextOutA",                (uint32_t)&TextOutA },
    { "GetTextExtentPoint32A",   (uint32_t)&GetTextExtentPoint32A },
    { "FillRect",                (uint32_t)&FillRect },
    { "PatBlt",                  (uint32_t)&PatBlt },
    { "Rectangle",               (uint32_t)&Rectangle },
    { "MoveToEx",                (uint32_t)&MoveToEx },
    { "LineTo",                  (uint32_t)&LineTo },
    { "GetTextMetricsA",         (uint32_t)&GetTextMetricsA },
    { "GetTextFaceA",            (uint32_t)&GetTextFaceA },
    { "GetCharWidthA",           (uint32_t)&GetCharWidthA },
    { "GetDeviceCaps",           (uint32_t)&GetDeviceCaps },
    { "DeleteDC",                (uint32_t)&DeleteDC },
    { "StartDocA",               (uint32_t)&StartDocA },
    { "EndDoc",                  (uint32_t)&EndDoc },
    { "StartPage",               (uint32_t)&StartPage },
    { "EndPage",                 (uint32_t)&EndPage },
    { "AbortDoc",                (uint32_t)&AbortDoc },
    { "SetAbortProc",            (uint32_t)&SetAbortProc },
    { "", 0 },
};