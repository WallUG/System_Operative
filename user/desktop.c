/* MyOS - user/desktop.c
 * Escritorio (Fase 17 + 21 + Fase 24-P3.1): dos ventanas del WM creadas
 * por la app:
 *   - Fondo (WM_FLAG_BG | NOFRAME, pantalla completa): wallpaper con
 *     iconos por app (Fase 24-P3.1). Los iconos se extraen del .rsrc
 *     del .exe (RT_GROUP_ICON -> RT_ICON, 32bpp/8bpp/4bpp) leyendo el
 *     archivo del MEFS; si no hay recursos se dibuja un icono generico
 *     (carpeta/burbuja/pantalla segun el tipo de app).
 *   - Barra de tareas (WM_FLAG_FIXED | NOFRAME, borde inferior) con un
 *     boton por app (Fase 21): clic lanza la app (fork + exec).
 * Doble clic sobre un icono del escritorio lanza la app (ventana de
 * 300 ms entre dos UPs sobre el mismo icono; SYS_TICKS, 100 Hz).
 * Un clic simple selecciona (resaltado). 'q' cierra el escritorio.
 * Teclas de TEST (headless): 'i' inyecta un clic simple sintetico
 * sobre el icono 0, 'd' un doble clic (los eventos pasan por la cola
 * global y wm_route como si fueran del raton). */

#include <stdint.h>
#include "winlib.h"

#define TB_Y   572            /* barra de tareas pegada al borde inferior */
#define TB_H   28

#define COLOR_DESK 0x00304860u    /* fondo del escritorio */
#define COLOR_STRIP 0x003C5C78u   /* franjas de fondo */
#define COLOR_HINT  0x00C0D0E0u
#define COLOR_TB_BG 0x00202838u   /* fondo de la barra de tareas */
#define COLOR_TB_ED 0x00406080u   /* borde superior de la barra */
#define COLOR_BTN   0x00406090u   /* boton normal */
#define COLOR_BTN_H 0x005078B0u   /* boton presionado */
#define COLOR_LBL   0x00FFFFFFu
#define COLOR_LBL_P 0x00E0F0FFu

#define ICON_SZ   32             /* iconos del escritorio (Fase 24-P3.1) */
#define ICON_PAD  72             /* paso horizontal entre iconos */
#define ICON_X0   24
#define ICON_Y0   24
#define ICON_LBL_Y (ICON_Y0 + ICON_SZ + 4)
#define ICON_BOX_W (ICON_SZ + 8) /* caja de hit/seleccion (icono + label) */
#define ICON_BOX_H (ICON_SZ + 24)
#define DBL_TICKS 30             /* doble clic: < 300 ms a 100 ticks/s */

#define COLOR_SEL    0x0030506Cu /* resaltado de seleccion */
#define COLOR_SEL_ED 0x006080A0u

#define MAX_APPS    4
#define MAX_PE_SIZE 0x100000u    /* metapad.exe ~195 KB, tope de DREAD */
#define RES_ANY     0xFFFFFFFFu

typedef struct {
    const char *label;
    const char *file;       /* .elf nativo o .exe Win32 */
    int ftype;              /* 0 = .rsrc, 1 = carpeta, 2 = burbuja,
                               3 = pantalla (fallbacks procedurales) */
} app_t;

static const app_t apps[MAX_APPS] = {
    { "EXPLORADOR",  "explorer.elf",  1 },
    { "METAPAD",     "metapad.exe",   0 },
    { "MENSAJE",     "messagebox.exe", 2 },
    { "DEMO",        "win_demo.elf",  3 },
};

static uint32_t scr_w, scr_h;
static uint32_t *g_bg;              /* buffer del fondo (para repintar) */
static int g_idb;                   /* id de la ventana de fondo */
static int sel_icon = -1;           /* icono seleccionado */
static int last_icon = -1;          /* ultimo clic (deteccion de doble) */
static uint32_t last_ticks;
static int pending_x;               /* test: cerrar metapad con el X */
static uint32_t x_ticks;
static int x_tries;

#define X_PERIOD 250                /* 2.5 s entre inyecciones del X */
#define X_TRIES  14                 /* ventana de ~35 s para el cierre */

/* Iconos ARGB 32x32 (1 = hay bitmap, de .rsrc o fallback). */
static uint32_t icon_px[MAX_APPS][ICON_SZ * ICON_SZ];
static int icon_have[MAX_APPS];

/* --- lectura LE --- */
static uint32_t rd16(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8);
}
static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Contexto de un PE cargado en memoria (archivo completo): rva de la
 * seccion .rsrc, su offset crudo en el archivo y la base cruda. */
typedef struct {
    const uint8_t *img;
    uint32_t dir_rva;   /* RVA de la seccion .rsrc */
    uint32_t raw_off;   /* offset crudo de la seccion en el archivo */
} pe_t;

/* Camina el arbol de recursos (tipo -> id -> lang). Los offsets de los
 * directorios son relativos a la seccion .rsrc; los de los datos son
 * RVA absolutos de imagen. Devuelve puntero a los datos y tamano (0
 * si no existe). RES_ANY en id acepta la primera entrada numerica. */
static const uint8_t *res_find(const pe_t *pe, uint32_t type,
                               uint32_t id, uint32_t *size)
{
    const uint8_t *img = pe->img;
    uint32_t base = (uint32_t)(uintptr_t)(img + pe->raw_off);
    const uint8_t *dir = img + pe->raw_off;
    int level;

    for (level = 0; level < 3; level++) {
        uint32_t cnt = rd16(dir + 12) + rd16(dir + 14);
        uint32_t i;

        for (i = 0; i < cnt; i++) {
            const uint8_t *e = dir + 16 + i * 8;
            uint32_t name = rd32(e);
            uint32_t target = rd32(e + 4) & 0x7FFFFFFF;
            uint32_t want = (level == 0) ? type : id;

            if (level == 2) {       /* nivel lang: vale cualquiera */
                const uint8_t *d = img + pe->raw_off + target;
                *size = rd32(d + 4);
                /* OffsetToData es un RVA absoluto de imagen. */
                return img + pe->raw_off + (rd32(d) - pe->dir_rva);
            }
            if ((name & 0x80000000) == 0 &&
                (want == RES_ANY || name == want)) {
                dir = (const uint8_t *)(uintptr_t)(base + target);
                break;
            }
        }
        if (i == cnt)
            return 0;
    }
    return 0;
}

/* Extrae el bitmap del icono (BITMAPINFOHEADER + XOR [+ paleta] + AND)
 * a ARGB 32x32 (escala nearest-neighbor). 32bpp usa el alpha del XOR;
 * 4/8bpp paleta + AND mask. */
static int icon_extract(const uint8_t *idat, uint32_t isize, uint32_t *dst)
{
    int w, hh, bpp, colors, topdown, i, j;
    uint32_t pal_off, xor_off, and_off, pitch, and_pitch;

    if (isize < 40)
        return 0;
    w = (int)rd32(idat + 4);
    if (w <= 0 || w > 64)
        return 0;
    bpp = (int)rd16(idat + 14);
    if (bpp != 32 && bpp != 8 && bpp != 4)
        return 0;
    {
        int bih = (int)rd32(idat + 8);
        if (bih < 0) { hh = -bih; topdown = 1; }
        else         { hh = bih / 2; topdown = 0; }
    }
    if (hh <= 0 || hh > 64)
        return 0;
    colors = (int)rd32(idat + 32);
    if (colors == 0)
        colors = (bpp == 32) ? 0 : (bpp == 8 ? 256 : 16);
    pal_off = 40;
    if (bpp == 32) {
        xor_off = pal_off;
        pitch = (uint32_t)w * 4;
    } else {
        xor_off = pal_off + (uint32_t)colors * 4;
        pitch = (bpp == 8) ? (uint32_t)((w + 3) & ~3)
                           : (uint32_t)(((w + 1) / 2 + 3) & ~3);
    }
    and_off = xor_off + (uint32_t)hh * pitch;
    and_pitch = (uint32_t)(((w + 31) / 32) * 4);

    for (j = 0; j < ICON_SZ; j++) {
        int srcy = j * hh / ICON_SZ;
        int arow = topdown ? srcy : (hh - 1 - srcy);

        for (i = 0; i < ICON_SZ; i++) {
            int srcx = i * w / ICON_SZ;
            uint32_t r = 0, g = 0, b = 0, a = 255;

            if (bpp == 32) {
                uint32_t o = xor_off + (uint32_t)arow * pitch +
                             (uint32_t)srcx * 4;
                if (o + 4 > isize)
                    return 0;
                b = idat[o]; g = idat[o + 1]; r = idat[o + 2];
                a = idat[o + 3];
            } else {
                uint32_t p = 0;
                if (bpp == 8) {
                    uint32_t o = xor_off + (uint32_t)arow * pitch +
                                 (uint32_t)srcx;
                    if (o < isize)
                        p = idat[o];
                } else {
                    uint32_t o = xor_off + (uint32_t)arow * pitch +
                                 (uint32_t)srcx / 2;
                    if (o < isize)
                        p = (srcx & 1) ? (idat[o] & 0x0F) : (idat[o] >> 4);
                }
                {
                    uint32_t pa = pal_off + p * 4;
                    if (pa + 4 > isize)
                        return 0;
                    b = idat[pa]; g = idat[pa + 1]; r = idat[pa + 2];
                }
                {
                    uint32_t o = and_off + (uint32_t)arow * and_pitch +
                                 (uint32_t)srcx / 8;
                    if (o < isize && (idat[o] & (0x80u >> (srcx & 7))))
                        a = 0;      /* AND mask: transparente */
                }
            }
            dst[j * ICON_SZ + i] = (a << 24) | (r << 16) | (g << 8) | b;
        }
    }
    return 1;
}

/* Carga el primer icono 32x32 (preferentemente 32bpp) del .rsrc del
 * .exe indicado, leyendo el archivo del MEFS. 1 si lo extrajo. */
static int pe_icon_load(const char *file, uint32_t *dst)
{
    const uint8_t *img;
    uint32_t fsize, pe_off, dir_rva, sec_rva = 0, raw_off = 0;
    int nsec, i, got;

    fsize = (uint32_t)sys_fsize(file);
    if (fsize == 0 || fsize > MAX_PE_SIZE)
        return 0;
    img = (const uint8_t *)sys_malloc(fsize);
    if (img == 0)
        return 0;
    got = sys_dread(file, (char *)img, 0, fsize);
    if (got != (int)fsize || img[0] != 'M' || img[1] != 'Z')
        return 0;
    pe_off = rd32(img + 0x3C);
    if (rd16(img + pe_off + 24) != 0x10B)   /* PE32 */
        return 0;
    dir_rva = rd32(img + pe_off + 24 + 96 + 2 * 8);   /* dir 2: recursos */
    if (dir_rva == 0)
        return 0;
    nsec = (int)rd16(img + pe_off + 6);
    for (i = 0; i < nsec; i++) {
        const uint8_t *s = img + pe_off + 24 + rd16(img + pe_off + 20) +
                           i * 40;
        uint32_t vr = rd32(s + 12), vs = rd32(s + 8), ro = rd32(s + 20);
        if (vr <= dir_rva && dir_rva < vr + vs) {
            sec_rva = vr;
            raw_off = ro;
            break;
        }
    }
    if (sec_rva == 0)
        return 0;
    {
        pe_t pe;
        const uint8_t *g, *idat;
        uint32_t gsz = 0, isz = 0, n, e, bc, best = 0, best_bc = 0;
        uint16_t nID;

        pe.img = img;
        pe.dir_rva = dir_rva;
        pe.raw_off = raw_off;
        g = res_find(&pe, 14, RES_ANY, &gsz);   /* RT_GROUP_ICON */
        if (g == 0 || gsz < 6)
            return 0;
        n = rd16(g + 4);
        for (e = 0; e < n && e < 32; e++) {
            bc = rd16(g + 6 + e * 14 + 6);
            if (g[6 + e * 14] == 32 && g[6 + e * 14 + 1] == 32 &&
                bc > best_bc) {
                best_bc = bc;
                best = e;
            }
        }
        if (best_bc == 0)                   /* sin 32x32 en el grupo */
            return 0;
        nID = (uint16_t)rd16(g + 6 + best * 14 + 12);
        idat = res_find(&pe, 3, (uint32_t)nID, &isz);   /* RT_ICON */
        if (idat == 0)
            return 0;
        return icon_extract(idat, isz, dst);
    }
}

/* --- dibujo procedural de iconos fallback (ARGB 32x32) --- */

static void ipx(int i, int x, int y, uint32_t argb)
{
    if (x < 0 || y < 0 || x >= ICON_SZ || y >= ICON_SZ)
        return;
    icon_px[i][y * ICON_SZ + x] = argb;
}

static void ifill(int i, int x0, int y0, int w, int h, uint32_t argb)
{
    int x, y;
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++)
            ipx(i, x0 + x, y0 + y, argb);
}

static void irect(int i, int x0, int y0, int w, int h, uint32_t argb)
{
    int x, y;
    for (x = 0; x < w; x++) {
        ipx(i, x0 + x, y0, argb);
        ipx(i, x0 + x, y0 + h - 1, argb);
    }
    for (y = 0; y < h; y++) {
        ipx(i, x0, y0 + y, argb);
        ipx(i, x0 + w - 1, y0 + y, argb);
    }
}

static void itext(int i, int x, int y, char c, uint32_t argb)
{
    const unsigned char *g;
    int k, j;

    if (c < 32 || c > 126)
        c = '?';
    g = font8x16_basic[(unsigned char)c - 32];
    for (j = 0; j < 16; j++)
        for (k = 0; k < 8; k++)
            if (g[j] & (0x80u >> k))
                ipx(i, x + k, y + j, argb);
}

static void fallback_icon(int i)
{
    switch (apps[i].ftype) {
    case 1: {                       /* carpeta (explorador) */
        ifill(i, 4, 2, 14, 8, 0xFFB09030u);
        ifill(i, 2, 8, 28, 22, 0xFFD8B85Cu);
        irect(i, 2, 8, 28, 22, 0xFF7A6420u);
        ifill(i, 2, 8, 28, 3, 0xFFE8CE80u);
        break;
    }
    case 2: {                       /* burbuja de mensaje */
        ifill(i, 3, 6, 26, 20, 0xFF3A6CB8u);
        irect(i, 3, 6, 26, 20, 0xFF204080u);
        ifill(i, 13, 26, 7, 4, 0xFF204080u);
        itext(i, 12, 12, '!', 0xFFFFFFFFu);
        break;
    }
    case 3: {                       /* pantalla (demo) */
        ifill(i, 6, 4, 20, 18, 0xFF70A8E0u);
        irect(i, 6, 4, 20, 18, 0xFF205070u);
        ifill(i, 9, 8, 14, 6, 0xFF9CC8F0u);
        ifill(i, 12, 24, 8, 4, 0xFF205070u);
        ifill(i, 8, 28, 16, 2, 0xFF205070u);
        break;
    }
    default: {                      /* documento generico */
        ifill(i, 6, 2, 20, 28, 0xFFFFFFFFu);
        irect(i, 6, 2, 20, 28, 0xFF406090u);
        ifill(i, 10, 8, 12, 3, 0xFF406090u);
        ifill(i, 10, 14, 12, 3, 0xFF7090C0u);
        ifill(i, 10, 20, 12, 3, 0xFF406090u);
        break;
    }
    }
    icon_have[i] = 1;
}

/* --- dibujo del escritorio --- */

static void irect_bg(int x0, int y0, int w, int h, uint32_t c)
{
    int x, y;
    for (x = 0; x < w; x++) {
        wl_putpixel(g_bg, (int)scr_w, (int)scr_h, x0 + x, y0, c);
        wl_putpixel(g_bg, (int)scr_w, (int)scr_h, x0 + x, y0 + h - 1, c);
    }
    for (y = 0; y < h; y++) {
        wl_putpixel(g_bg, (int)scr_w, (int)scr_h, x0, y0 + y, c);
        wl_putpixel(g_bg, (int)scr_w, (int)scr_h, x0 + w - 1, y0 + y, c);
    }
}

static void tb_button(uint32_t *tb, int bx, int lbw, const char *label,
                      int pressed)
{
    uint32_t bg = pressed ? COLOR_BTN_H : COLOR_BTN;
    uint32_t fg = pressed ? COLOR_LBL_P : COLOR_LBL;
    uint32_t lw = wl_strlen(label) * 8;

    wl_fillrect(tb, (int)scr_w, (int)scr_h, bx, 4, lbw, TB_H - 8, bg);
    wl_fillrect(tb, (int)scr_w, (int)scr_h, bx, 4, lbw, 1, COLOR_TB_ED);
    wl_drawtext(tb, (int)scr_w, (int)scr_h,
                bx + (lbw - (int)lw) / 2, 4 + (TB_H - 8 - 16) / 2, label, fg);
}

/* Repinta una celda de icono completa (fondo de seleccion + icono +
 * label). */
static void paint_icon_cell(int i, int sel)
{
    int x0 = ICON_X0 + i * ICON_PAD;
    int y0 = ICON_Y0;
    int x, y;
    uint32_t lw = wl_strlen(apps[i].label) * 8;

    if (sel) {
        wl_fillrect(g_bg, (int)scr_w, (int)scr_h,
                    x0 - 4, y0 - 4, ICON_BOX_W, ICON_BOX_H, COLOR_SEL);
        irect_bg(x0 - 4, y0 - 4, ICON_BOX_W, ICON_BOX_H, COLOR_SEL_ED);
    } else {
        /* limpia la celda: fondo + franja vertical si cruza una */
        wl_fillrect(g_bg, (int)scr_w, (int)scr_h,
                    x0 - 4, y0 - 4, ICON_BOX_W, ICON_BOX_H, COLOR_DESK);
        for (x = 8; x < (int)scr_w; x += 96)
            wl_fillrect(g_bg, (int)scr_w, (int)scr_h,
                        x, y0 - 4, 2, ICON_BOX_H, COLOR_STRIP);
        for (y = 8; y < (int)scr_h; y += 96)
            if (y >= y0 - 4 && y < y0 - 4 + ICON_BOX_H)
                wl_fillrect(g_bg, (int)scr_w, (int)scr_h,
                            x0 - 4, y, ICON_BOX_W, 1, COLOR_STRIP);
    }
    if (icon_have[i]) {
        for (y = 0; y < ICON_SZ; y++)
            for (x = 0; x < ICON_SZ; x++) {
                uint32_t p = icon_px[i][y * ICON_SZ + x];
                if ((p >> 24) >= 0x80)
                    wl_putpixel(g_bg, (int)scr_w, (int)scr_h,
                                x0 + x, y0 + y, p & 0xFFFFFFu);
            }
    }
    wl_drawtext(g_bg, (int)scr_w, (int)scr_h,
                x0 + (ICON_SZ - (int)lw) / 2, ICON_LBL_Y,
                apps[i].label, sel ? COLOR_LBL_P : COLOR_LBL);
}

/* Redibuja la celda de seleccion en pantalla (blit por region). */
static void redraw_icon_cell(int i)
{
    sys_winupdate_rect(g_idb, ICON_X0 + i * ICON_PAD - 4, ICON_Y0 - 4,
                       ICON_BOX_W, ICON_BOX_H);
}

static void select_icon(int i)
{
    if (i == sel_icon)
        return;
    if (sel_icon >= 0) {
        paint_icon_cell(sel_icon, 0);
        redraw_icon_cell(sel_icon);
    }
    sel_icon = i;
    if (i >= 0) {
        paint_icon_cell(i, 1);
        redraw_icon_cell(i);
    }
}

static int hit_icon(int x, int y)
{
    int i;
    for (i = 0; i < MAX_APPS; i++)
        if (wl_point_in(x, y, ICON_X0 + i * ICON_PAD - 4, ICON_Y0 - 4,
                        ICON_BOX_W, ICON_BOX_H))
            return i;
    return -1;
}

/* Wallpaper en el buffer del fondo (el WM lo compone en z=0). */
static void paint_wallpaper(uint32_t *bg, int bw, int bh)
{
    int x, y;

    wl_fillrect(bg, bw, bh, 0, 0, bw, bh, COLOR_DESK);
    for (x = 8; x < bw; x += 96)
        wl_fillrect(bg, bw, bh, x, 0, 2, bh, COLOR_STRIP);
    for (y = 8; y < bh; y += 96)
        wl_fillrect(bg, bw, bh, 0, y, bw, 1, COLOR_STRIP);
    wl_drawtext(bg, bw, bh, 16, 536, "MyOS desktop (Fase 24)", COLOR_LBL);
    wl_drawtext(bg, bw, bh, 16, 554,
                "1-4 o doble clic lanzan, clic en la barra, q cierra",
                COLOR_HINT);
}

/* Lanza la app i-esima como hijo (fork + exec). */
static void launch_app(int i)
{
    int kid;
    char l[64];
    const char *pre = "esc: lanzando ";
    uint32_t p = 0, k = 0;

    while (*pre)
        l[p++] = *pre++;
    while (apps[i].file[k] && k < 30 && p < 60)
        l[p++] = apps[i].file[k++];
    l[p++] = '\n';
    l[p] = 0;
    sys_write(l, p);

    kid = sys_fork();
    if (kid == 0) {
        /* Hijo: imagen nueva (exec libera el espacio heredado). */
        if (sys_exec(apps[i].file) != 0) {
            sys_write("esc: exec fallo\n", 16);
        }
        sys_exit(2);
    } else if (kid < 0) {
        sys_write("esc: fork fallo\n", 16);
    }
}

/* Log de texto plano al serial. */
static void log_str(const char *s)
{
    sys_write(s, wl_strlen(s));
}

/* Log con dec: mensaje + numero (sin newline). */
static void log_dec(const char *pre, uint32_t v)
{
    char b[40], n[12];
    uint32_t p = 0, k;

    while (*pre && p < 30)
        b[p++] = *pre++;
    k = wl_dec(n, v);
    while (k > 0 && p < 38)
        b[p++] = n[--k];
    b[p] = 0;
    sys_write(b, p);
}

int _start(void)
{
    uint32_t info[4], a[8], ev[5];
    uint32_t *bg, *tb;
    int idb, idt, i, nrsrc = 0;

    sys_write("esc: escritorio iniciando\n", 26);
    if (sys_gfxinfo(info) != 0) {
        sys_write("esc: sin modo grafico\n", 22);
        return 1;
    }
    scr_w = info[1];
    scr_h = info[2];

    bg = (uint32_t *)sys_malloc(scr_w * scr_h * 4);
    tb = (uint32_t *)sys_malloc(scr_w * TB_H * 4);
    if (bg == (void *)0 || tb == (void *)0) {
        sys_write("esc: malloc fallo\n", 18);
        return 1;
    }
    g_bg = bg;
    paint_wallpaper(bg, (int)scr_w, (int)scr_h);

    /* Iconos: .rsrc del .exe si existe, si no fallback procedural. */
    for (i = 0; i < MAX_APPS; i++) {
        if (apps[i].ftype == 0 &&
            pe_icon_load(apps[i].file, icon_px[i])) {
            icon_have[i] = 1;
            nrsrc++;
            sys_write("esc: icono rsrc ", 15);
            log_str(apps[i].file);
            sys_write("\n", 1);
        } else {
            fallback_icon(i);
            sys_write("esc: icono fallback ", 20);
            log_str(apps[i].file);
            sys_write("\n", 1);
        }
        paint_icon_cell(i, 0);
    }
    log_dec("esc: iconos ", (uint32_t)nrsrc);
    sys_write("/4\n", 3);

    /* Fondo: pantalla completa, siempre z=0, sin hit-test. */
    a[0] = (uint32_t)"Fondo"; a[1] = 0; a[2] = 0;
    a[3] = scr_w; a[4] = scr_h; a[5] = (uint32_t)bg;
    a[6] = scr_w * scr_h * 4;
    a[7] = WM_FLAG_BG | WM_FLAG_NOFRAME;
    idb = sys_wincreate(a);
    if (idb < 0) {
        sys_write("esc: fondo fallo\n", 17);
        return 1;
    }
    g_idb = idb;

    wl_fillrect(tb, (int)scr_w, (int)scr_h, 0, 0,
                (int)scr_w, TB_H, COLOR_TB_BG);
    wl_fillrect(tb, (int)scr_w, (int)scr_h, 0, 0,
                (int)scr_w, 1, COLOR_TB_ED);
    wl_drawtext(tb, (int)scr_w, (int)scr_h, (int)scr_w - 66, 6,
                "q:salir", COLOR_HINT);
    for (i = 0; i < MAX_APPS; i++)
        tb_button(tb, 6 + i * 136, 128, apps[i].label, 0);

    a[0] = (uint32_t)"Taskbar"; a[1] = 0; a[2] = TB_Y;
    a[3] = scr_w; a[4] = TB_H; a[5] = (uint32_t)tb;
    a[6] = scr_w * TB_H * 4;
    a[7] = WM_FLAG_FIXED | WM_FLAG_NOFRAME;
    idt = sys_wincreate(a);
    if (idt < 0) {
        sys_write("esc: taskbar fallo\n", 19);
        return 1;
    }
    sys_write("esc: escritorio listo\n", 22);
    log_dec("esc: ventanas idb=", (uint32_t)idb);
    log_dec(" idt=", (uint32_t)idt);
    sys_write("\n", 1);

    for (;;) {
        int ai = -1, hi;
        uint32_t t;

        if (pending_x && (sys_ticks() - x_ticks >= X_PERIOD)) {
            x_ticks = sys_ticks();
            if (++x_tries >= X_TRIES)
                pending_x = 0;
            sys_mouse_inject(EV_MOVE, 676, 43, 1, 0);
            sys_mouse_inject(EV_BUTTON_DOWN, 676, 43, 1, 0);
            sys_mouse_inject(EV_BUTTON_UP, 676, 43, 1, 0);
            sys_write("esc: inj X\n", 11);
        }

        if (sys_event(ev) != 0)
            continue;
        switch (ev[0]) {
        case EV_KEY:
            if (ev[4] == 'q')
                goto salir;
            if (ev[4] >= '1' && ev[4] <= '0' + MAX_APPS)
                launch_app((int)(ev[4] - '1'));
            if (ev[4] == 'i') {      /* test: clic simple en icono 1 */
                sys_mouse_inject(EV_MOVE, ICON_X0 + 16 + ICON_PAD,
                                 ICON_Y0 + 16, 1, 0);
                sys_mouse_inject(EV_BUTTON_DOWN, ICON_X0 + 16 + ICON_PAD,
                                 ICON_Y0 + 16, 1, 0);
                sys_mouse_inject(EV_BUTTON_UP, ICON_X0 + 16 + ICON_PAD,
                                 ICON_Y0 + 16, 1, 0);
                sys_write("esc: inj clic icono1\n", 20);
            }
            if (ev[4] == 'd') {      /* test: doble clic en icono 1 */
                sys_mouse_inject(EV_MOVE, ICON_X0 + 16 + ICON_PAD,
                                 ICON_Y0 + 16, 1, 0);
                sys_mouse_inject(EV_BUTTON_DOWN, ICON_X0 + 16 + ICON_PAD,
                                 ICON_Y0 + 16, 1, 0);
                sys_mouse_inject(EV_BUTTON_UP, ICON_X0 + 16 + ICON_PAD,
                                 ICON_Y0 + 16, 1, 0);
                sys_mouse_inject(EV_BUTTON_DOWN, ICON_X0 + 16 + ICON_PAD,
                                 ICON_Y0 + 16, 1, 0);
                sys_mouse_inject(EV_BUTTON_UP, ICON_X0 + 16 + ICON_PAD,
                                 ICON_Y0 + 16, 1, 0);
                sys_write("esc: inj doble icono1\n", 21);
                /* Tras lanzar metapad, inyectar clics periodicos en su
                 * boton X hasta que cierre (el teclado ya no llega al
                 * escritorio: metapad tiene el foco). */
                pending_x = 1;
                x_ticks = sys_ticks();
                x_tries = 0;
            }
            break;
        case EV_BUTTON_DOWN:
            for (i = 0; i < MAX_APPS; i++)
                if (wl_point_in((int)ev[1], (int)ev[2],
                                6 + i * 136, TB_Y, 128, TB_H))
                    ai = i;
            if (ai >= 0) {
                tb_button(tb, 6 + ai * 136, 128, apps[ai].label, 1);
                sys_winupdate(idt);
                break;
            }
            hi = hit_icon((int)ev[1], (int)ev[2]);
            select_icon(hi);
            break;
        case EV_BUTTON_UP:
            for (i = 0; i < MAX_APPS; i++)
                if (wl_point_in((int)ev[1], (int)ev[2],
                                6 + i * 136, TB_Y, 128, TB_H))
                    ai = i;
            if (ai >= 0) {
                tb_button(tb, 6 + ai * 136, 128, apps[ai].label, 0);
                sys_winupdate(idt);
                launch_app(ai);
                break;
            }
            hi = hit_icon((int)ev[1], (int)ev[2]);
            if (hi >= 0) {
                t = sys_ticks();
                if (hi == last_icon && t - last_ticks < DBL_TICKS) {
                    select_icon(-1);
                    last_icon = -1;
                    launch_app(hi);
                } else {
                    last_icon = hi;
                    last_ticks = t;
                }
            }
            break;
        }
    }

salir:
    sys_write("esc: fin del escritorio\n", 24);
    sys_winclose(idb);
    sys_winclose(idt);
    return 0;
}