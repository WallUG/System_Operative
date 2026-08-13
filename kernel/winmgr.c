/* MyOS - kernel/winmgr.c
 * Gestor de ventanas en el kernel (Fase 16, opcion B).
 *
 * Modelo:
 *  - La app pinta su area cliente en un buffer propio (SYS_MALLOC) y lo
 *    registra con SYS_WINCREATE. El kernel guarda {va, sz, pd}.
 *  - El kernel compone: fondo snapshot -> ventanas en orden z (marco,
 *    titulo, boton X del kernel + blit del cliente leido del usuario).
 *  - El kernel consume los eventos de WM: BUTTON_DOWN en el titulo
 *    inicia drag (raise previo), MOVE/UP lo completan, el boton X se
 *    entrega como EV_WINCLOSE a la app. Los clics en el area cliente se
 *    entregan sin tocar (la ventana se sube al top primero).
 *
 * Limites aceptados en esta fase: composicion completa (fondo + todas
 * las ventanas) en cada cambio; el cursor del raton se re-dibuja al
 * final de cada composicion (lo pisaria el blit). */

#include <stdint.h>
#include <string.h>
#include "winmgr.h"
#include "mem/heap.h"
#include "mem/paging.h"
#include "drivers/vbe.h"
#include "drivers/mouse.h"
#include "kprint.h"

#define C_FRAME     0x00C0C0C0u
#define C_TITLE     0x00000088u
#define C_TITLE_TX  0x00FFFFFFu
#define C_X_BG      0x00CC3333u
#define C_X_TX      0x00FFFFFFu
#define C_MENU      0x00D0D0D0u
#define C_MENU_TX   0x00000000u

#include "drivers/font8x16.h"

/* Los colores se definen en formato logico 0x00RRGGBB, pero el LFB
 * (VBE 32bpp) espera el byte bajo = R (BGRx8888 en memoria). El
 * swap R<->B se aplica SOLO al escribir el LFB: las constantes
 * conservan su semantica logica. Verificado contra QEMU 10.2.2:
 * 0x00000088 escrito directo se muestra (136,0,0); con el swap
 * 0x00880000 -> (0,0,136) azul correcto. */
static inline uint32_t px_disp(uint32_t c)
{
    return ((c & 0x0000FFFFu) << 8) |
           ((c >> 16) & 0x000000FFu) |
           (c & 0xFF000000u);
}

typedef struct {
    int      id;
    char     title[24];
    int      x, y, w, h;          /* rect total (marco incluido)       */
    int      cx, cy, cw, ch;      /* area cliente                       */
    uint32_t buf_va;              /* buffer del cliente (ring 3)       */
    uint32_t buf_sz;
    uint32_t pd;                  /* PD de la app duena                 */
    uint32_t flags;               /* WM_FLAG_*                          */
    int      z;                   /* 0 = fondo; mayor = mas arriba      */
    int      visible;
    int      dragging;
    int      drag_dx, drag_dy;    /* offset del clic dentro de la vent */
    int      has_menu;            /* Fase D: barra de menu activa      */
    int      menu_n;              /* top-levels visibles               */
    char     menu_tx[8][24];
    int      has_toolbar;         /* Fase 20-B: barra de herramientas  */
    int      tb_n;                /* numero de botones                 */
    char     tb_tx[12][12];       /* etiquetas de los botones           */
} win_t;

static win_t wins[WM_MAX_WINS];
static int wm_next_id = 1;
static uint32_t *wm_background;   /* snapshot del LFB (kmalloc)         */
static int wm_active = 0;         /* hay alguna ventana visible         */

/* Fase 17: cola de eventos por app (PD). Cada app solo recibe los
 * eventos de sus ventanas (y del foco); el escritorio es otra app mas. */
#define WM_MAX_CLIENTS 4
typedef struct {
    uint32_t pd;
    mouse_event_t q[EV_QUEUE_MAX];
    int head, tail;
    int used;
} wm_client_t;
static wm_client_t wm_clients[WM_MAX_CLIENTS];
static uint32_t wm_focus_pd = 0;  /* destino del teclado / fondo       */

static win_t *wm_hit(int px, int py)
{
    int i, best = -1, bestz = -1;
    for (i = 0; i < WM_MAX_WINS; i++) {
        int z;
        if (!wins[i].visible || (wins[i].flags & WM_FLAG_BG))
            continue;
        z = wins[i].z + (wins[i].flags & WM_FLAG_FIXED ? 0x1000 : 0);
        if (z <= bestz)
            continue;
        if (px >= wins[i].x && px < wins[i].x + wins[i].w &&
            py >= wins[i].y && py < wins[i].y + wins[i].h) {
            best = i;
            bestz = z;
        }
    }
    return best >= 0 ? &wins[best] : NULL;
}

static win_t *wm_topmost(void)
{
    int i, best = -1, bestz = -1;
    for (i = 0; i < WM_MAX_WINS; i++) {
        int z;
        if (!wins[i].visible || (wins[i].flags & WM_FLAG_BG))
            continue;
        z = wins[i].z + (wins[i].flags & WM_FLAG_FIXED ? 0x1000 : 0);
        if (z > bestz) {
            best = i;
            bestz = z;
        }
    }
    return best >= 0 ? &wins[best] : NULL;
}

/* Ventana superior NO fija (excluye la taskbar): destino del teclado
 * cuando hay una app abierta. La barra de tareas es la mas alta del
 * z-order (FIXED = +0x1000) y si se usara wm_topmost() el teclado
 * nunca llegaria a las apps del escritorio. */
static win_t *wm_topmost_app(void)
{
    int i, best = -1, bestz = -1;
    for (i = 0; i < WM_MAX_WINS; i++) {
        if (!wins[i].visible || (wins[i].flags & (WM_FLAG_FIXED | WM_FLAG_BG)))
            continue;
        if (wins[i].z > bestz) {
            best = i;
            bestz = wins[i].z;
        }
    }
    return best >= 0 ? &wins[best] : NULL;
}

static win_t *wm_find(int id)
{
    int i;
    for (i = 0; i < WM_MAX_WINS; i++)
        if (wins[i].visible && wins[i].id == id)
            return &wins[i];
    return NULL;
}

static win_t *wm_dragging(void)
{
    int i;
    for (i = 0; i < WM_MAX_WINS; i++)
        if (wins[i].visible && wins[i].dragging)
            return &wins[i];
    return NULL;
}

/* Sube la ventana al top del z-order (reordena los demas hacia abajo).
 * Las ventanas fijas (taskbar) quedan siempre arriba: se ignoran al
 * calcular el top. */
static void wm_raise(win_t *w)
{
    int top = -1, i;
    if (w->flags & (WM_FLAG_FIXED | WM_FLAG_BG))
        return;
    for (i = 0; i < WM_MAX_WINS; i++)
        if (wins[i].visible && !(wins[i].flags & WM_FLAG_FIXED) &&
            wins[i].z > top)
            top = wins[i].z;
    for (i = 0; i < WM_MAX_WINS; i++) {
        if (wins[i].visible && !(wins[i].flags & WM_FLAG_FIXED) &&
            wins[i].z > w->z)
            wins[i].z--;
    }
    w->z = top + 1;             /* por encima de todos los no fijos */
}

/* Primer arranque: snapshot del LFB actual (la consola vgafx queda como
 * fondo bajo las ventanas). */
static void wm_ensure_bg(void)
{
    if (wm_background || !vbe_graphics_active)
        return;
    wm_background = kmalloc(VBE_SCREEN_W * VBE_SCREEN_H * 4);
    if (wm_background) {
        memcpy(wm_background, (void *)vbe_lfb_phys,
               VBE_SCREEN_W * VBE_SCREEN_H * 4);
    }
}

/* Blit del area cliente desde el buffer de la app (validado por pagina
 * con el PD de la app) hacia el LFB. */
static void wm_blit_client(const win_t *w)
{
    volatile uint32_t *lfb = (volatile uint32_t *)vbe_lfb_phys;
    uint32_t y;

    if (!w->buf_va)
        return;
    for (y = 0; y < (uint32_t)w->ch; y++) {
        uint8_t *dst = (uint8_t *)lfb +
                       ((uint32_t)(w->cy + y) * VBE_SCREEN_W + w->cx) * 4;
        uint32_t row = y * (uint32_t)w->cw * 4;  /* linea del buffer     */
        uint32_t n = (uint32_t)w->cw * 4;
        while (n > 0) {
            uint32_t va = w->buf_va + row;
            uint32_t chunk = 0x1000 - (va & 0xFFF);
            uint32_t base = va & ~0xFFFu;
            uint32_t off  = va & 0xFFFu;
            uint32_t frame;
            if (chunk > n)
                chunk = n;
            frame = paging_user_frame(w->pd, base);
            if (frame == 0) {
                row += n;
                n = 0;
                break;
            }
            uint8_t *src = (uint8_t *)(frame + off);
            memcpy(dst, src, chunk);
            dst += chunk;
            row += chunk;
            n -= chunk;
        }
    }
}

/* Recalcula el area cliente segun el marco/titulo y la barra de menu
 * (Fase D: la franja de menu baja el cliente 20px). */
static void wm_layout(win_t *w)
{
    if (w->flags & WM_FLAG_NOFRAME) {
        w->cx = w->x;
        w->cy = w->y;
        w->cw = w->w;
        w->ch = w->h;
    } else {
        w->cx = w->x + WM_FRAME;
        w->cy = w->y + WM_TITLE_H + (w->has_menu ? WM_MENU_H : 0) +
                (w->has_toolbar ? WM_TOOLBAR_H : 0);
        w->cw = w->w - 2 * WM_FRAME;
        w->ch = w->h - WM_TITLE_H - (w->has_menu ? WM_MENU_H : 0) -
                (w->has_toolbar ? WM_TOOLBAR_H : 0) - WM_FRAME;
    }
}

/* Texto 8x16 al LFB (fuente del kernel, igual que user32). */
static void wm_putpixel(int x, int y, uint32_t c)
{
    volatile uint32_t *lfb = (volatile uint32_t *)vbe_lfb_phys;
    if (x < 0 || y < 0 || x >= VBE_SCREEN_W || y >= VBE_SCREEN_H)
        return;
    lfb[y * VBE_SCREEN_W + x] = px_disp(c);
}

static uint32_t wm_strlen(const char *s)
{
    uint32_t n = 0;
    while (s && s[n])
        n++;
    return n;
}

static void wm_text(int x, int y, const char *s, uint32_t c)
{
    const unsigned char *g;
    int i, j;

    while (s && *s) {
        char ch = *s;
        if (ch < 32 || ch > 126)
            ch = '?';
        g = font8x16_basic[(unsigned char)ch - 32];
        for (j = 0; j < 16; j++)
            for (i = 0; i < 8; i++)
                if (g[j] & (0x80u >> i))
                    wm_putpixel(x + i, y + j, c);
        x += 8;
        s++;
    }
}

/* Dibuja una ventana: marco/titulo/X (si no es NOFRAME) + cliente. */
static void wm_draw_one(const win_t *w)
{
    volatile uint32_t *lfb = (volatile uint32_t *)vbe_lfb_phys;
    int j;

    if (!(w->flags & WM_FLAG_NOFRAME)) {
        for (j = 0; j < WM_FRAME; j++) {
            int k;
            for (k = w->x; k < w->x + w->w; k++) {
                if (k >= 0 && k < VBE_SCREEN_W && w->y + j >= 0 &&
                    w->y + j < VBE_SCREEN_H)
                    lfb[(w->y + j) * VBE_SCREEN_W + k] = px_disp(C_FRAME);
                if (k >= 0 && k < VBE_SCREEN_W &&
                    w->y + w->h - 1 - j >= 0 &&
                    w->y + w->h - 1 - j < VBE_SCREEN_H)
                    lfb[(w->y + w->h - 1 - j) * VBE_SCREEN_W + k] =
                        px_disp(C_FRAME);
            }
            for (k = w->y; k < w->y + w->h; k++) {
                if (k >= 0 && k < VBE_SCREEN_H && w->x + j >= 0 &&
                    w->x + j < VBE_SCREEN_W)
                    lfb[k * VBE_SCREEN_W + w->x + j] = px_disp(C_FRAME);
                if (k >= 0 && k < VBE_SCREEN_H &&
                    w->x + w->w - 1 - j >= 0 &&
                    w->x + w->w - 1 - j < VBE_SCREEN_W)
                    lfb[k * VBE_SCREEN_W + w->x + w->w - 1 - j] =
                        px_disp(C_FRAME);
            }
        }
        /* titulo */
        for (j = 0; j < WM_TITLE_H; j++) {
            int k;
            for (k = w->x + WM_FRAME; k < w->x + w->w - WM_FRAME; k++) {
                if (k >= 0 && k < VBE_SCREEN_W &&
                    w->y + j >= 0 && w->y + j < VBE_SCREEN_H)
                    lfb[(w->y + j) * VBE_SCREEN_W + k] =
                        px_disp(C_TITLE);
            }
        }
        wm_text(w->x + WM_FRAME + 4, w->y + 2, w->title, C_TITLE_TX);
        /* barra de menu (Fase D): franja + labels top-level */
        if (w->has_menu) {
            int k;
            for (j = 0; j < WM_MENU_H; j++)
                for (k = w->x + WM_FRAME; k < w->x + w->w - WM_FRAME; k++)
                    if (k >= 0 && k < VBE_SCREEN_W &&
                        w->y + WM_TITLE_H + j >= 0 &&
                        w->y + WM_TITLE_H + j < VBE_SCREEN_H)
                        lfb[(w->y + WM_TITLE_H + j) * VBE_SCREEN_W + k] =
                            px_disp(C_MENU);
            {
                int x = w->x + WM_FRAME + 4;
                for (k = 0; k < w->menu_n && k < 8; k++) {
                    if (w->menu_tx[k][0])
                        wm_text(x, w->y + WM_TITLE_H + 2,
                                w->menu_tx[k], C_MENU_TX);
                    x += 8 * (int)wm_strlen(w->menu_tx[k]) + 16;
                }
            }
        }
        /* barra de herramientas (Fase 20-B): franja + botones de texto */
        if (w->has_toolbar) {
            int y0 = w->y + WM_TITLE_H + (w->has_menu ? WM_MENU_H : 0);
            for (j = 0; j < WM_TOOLBAR_H; j++) {
                int k;
                for (k = w->x + WM_FRAME; k < w->x + w->w - WM_FRAME; k++)
                    if (k >= 0 && k < VBE_SCREEN_W && y0 + j >= 0 &&
                        y0 + j < VBE_SCREEN_H)
                        lfb[(y0 + j) * VBE_SCREEN_W + k] =
                            px_disp(C_MENU);
            }
            {
                int x = w->x + WM_FRAME + 4;
                for (int k = 0; k < w->tb_n && k < 12; k++) {
                    int len = (int)wm_strlen(w->tb_tx[k]);
                    int bw = len * 8 + 12;
                    int bx = x, by = y0 + 2;
                    int bj, bk;
                    for (bj = 0; bj < 18; bj++)
                        for (bk = bx; bk < bx + bw; bk++)
                            if (bk >= 0 && bk < VBE_SCREEN_W &&
                                by + bj >= 0 && by + bj < VBE_SCREEN_H)
                                lfb[(by + bj) * VBE_SCREEN_W + bk] =
                                    px_disp(C_FRAME);
                    if (w->tb_tx[k][0])
                        wm_text(bx + 6, by + 1, w->tb_tx[k], C_MENU_TX);
                    x += bw + 6;
                }
            }
        }
        /* boton X (16x16, arriba a la derecha) */
        {
            int bx0 = w->x + w->w - WM_FRAME - WM_X_BTN;
            for (j = 0; j < WM_X_BTN; j++) {
                int k;
                for (k = 0; k < WM_X_BTN; k++) {
                    int xx = bx0 + k, yy = w->y + WM_FRAME + j;
                    if (xx >= 0 && xx < VBE_SCREEN_W && yy >= 0 &&
                        yy < VBE_SCREEN_H)
                        lfb[yy * VBE_SCREEN_W + xx] = px_disp(C_X_BG);
                }
            }
        }
    }
    wm_blit_client(w);
}

/* Composicion completa: fondo snapshot + ventanas en orden z (las
 * fijas, taskbar, al final: siempre visibles) + cursor. */
static void wm_compose(void)
{
    volatile uint32_t *lfb = (volatile uint32_t *)vbe_lfb_phys;
    int zz, i;

    if (!vbe_graphics_active)
        return;

    if (wm_background) {
        memcpy((void *)lfb, wm_background, VBE_SCREEN_W * VBE_SCREEN_H * 4);
    }
    for (zz = 0; zz < WM_MAX_WINS; zz++) {
        for (i = 0; i < WM_MAX_WINS; i++) {
            const win_t *w = &wins[i];
            if (w->visible && !(w->flags & WM_FLAG_FIXED) && w->z == zz)
                wm_draw_one(w);
        }
    }
    for (i = 0; i < WM_MAX_WINS; i++) {
        const win_t *w = &wins[i];
        if (w->visible && (w->flags & WM_FLAG_FIXED))
            wm_draw_one(w);
    }
    /* el blit pisa el cursor: re-dibujarlo */
    mouse_cursor_invalidate();
    mouse_draw_cursor();
}

static win_t *wm_find_by_pd(uint32_t pd)
{
    int i;
    for (i = 0; i < WM_MAX_WINS; i++)
        if (wins[i].visible && wins[i].pd == pd)
            return &wins[i];
    return NULL;
}

/* Marca una ventana como cerrada. Si estaba en medio de un drag se
 * cancela: un drag huerfano consumiria todos los eventos siguientes. */
static void wm_remove_window(win_t *w)
{
    w->visible = 0;
    w->dragging = 0;
}

/* Recalcula wm_active tras cerrar ventanas; si no queda ninguna
 * restaura la consola (compose con el fondo) y libera el snapshot; si
 * el foco quedo huerfano lo reasigna al tope. */
static void wm_recompute(void)
{
    int any = 0, i;
    for (i = 0; i < WM_MAX_WINS; i++)
        if (wins[i].visible)
            any = 1;
    wm_active = any;
    if (!any) {
        if (wm_background) {
            wm_compose();       /* restaura la consola antes de liberar */
            kfree(wm_background);
            wm_background = NULL;
        }
        wm_focus_pd = 0;
        return;
    }
    if (wm_focus_pd == 0 || wm_find_by_pd(wm_focus_pd) == NULL) {
        win_t *t = wm_topmost();
        wm_focus_pd = t ? t->pd : 0;
    }
}

int wm_create(const char *title, int x, int y, int w, int h,
              uint32_t buf_va, uint32_t buf_sz, uint32_t flags,
              uint32_t pd)
{
    win_t *win = NULL;
    int i;

    if (!vbe_graphics_active || w < 50 || h < 20 ||
        w > VBE_SCREEN_W || h > VBE_SCREEN_H)
        return -1;
    for (i = 0; i < WM_MAX_WINS; i++)
        if (!wins[i].visible) {
            win = &wins[i];
            break;
        }
    if (!win)
        return -1;

    wm_ensure_bg();
    memset(win, 0, sizeof(*win));
    win->id = wm_next_id++;
    for (i = 0; title && title[i] && i < (int)sizeof(win->title) - 1; i++)
        win->title[i] = title[i];
    win->title[i] = 0;
    win->x = x;
    win->y = y;
    win->w = w;
    win->h = h;
    win->flags = flags;
    wm_layout(win);
    win->buf_va = buf_va;
    win->buf_sz = buf_sz;
    win->pd = pd;
    win->visible = 1;
    wm_focus_pd = pd;               /* la app que crea gana el foco    */
    wm_raise(win);
    wm_active = 1;
    wm_compose();
    return win->id;
}

int wm_close(int id, uint32_t pd)
{
    win_t *w = wm_find(id);
    if (!w || w->pd != pd)
        return -1;
    wm_remove_window(w);
    wm_recompute();
    wm_compose();
    return 0;
}

/* Fase 17: limpieza al morir una tarea. La llama sched_kill_current
 * con el CR3 de la tarea antes de liberar su espacio de usuario. */
void wm_cleanup_pd(uint32_t pd)
{
    int i;
    if (pd == 0)
        return;
    for (i = 0; i < WM_MAX_WINS; i++)
        if (wins[i].visible && wins[i].pd == pd) {
            wm_remove_window(&wins[i]);
        }
    for (i = 0; i < WM_MAX_CLIENTS; i++)
        if (wm_clients[i].used && wm_clients[i].pd == pd) {
            wm_clients[i].used = 0;
            wm_clients[i].head = wm_clients[i].tail = 0;
        }
    wm_recompute();
    if (wm_active)
        wm_compose();
}

int wm_move(int id, int dx, int dy)
{
    win_t *w = wm_find(id);
    if (!w)
        return -1;
    w->x += dx;
    w->y += dy;
    wm_layout(w);
    wm_compose();
    return 0;
}

int wm_update(int id)
{
    win_t *w = wm_find(id);
    if (!w)
        return -1;
    wm_compose();
    return 0;
}

/* Cambia el titulo de la barra (SetWindowTextA de top-levels). */
int wm_set_title(int id, const char *title)
{
    win_t *w = wm_find(id);
    int i;

    if (!w)
        return -1;
    for (i = 0; title && title[i] && i < (int)sizeof(w->title) - 1; i++)
        w->title[i] = title[i];
    w->title[i] = 0;
    wm_compose();
    return 0;
}

/* Fase D: activa/desactiva la barra de menu y guarda los labels
 * top-level (flat: "File\0Edit\0...\0\0"). Reajusta el cliente. */
int wm_set_menu(int id, int on, const char *flat)
{
    win_t *w = wm_find(id);
    int i;

    if (!w)
        return -1;
    w->menu_n = 0;
    for (i = 0; i < 8; i++)
        w->menu_tx[i][0] = 0;
    if (on && flat) {
        const char *p = flat;
        while (*p && w->menu_n < 8) {
            int j = 0;
            while (*p && j < 23)
                w->menu_tx[w->menu_n][j++] = *p++;
            w->menu_tx[w->menu_n][j] = 0;
            while (*p)
                p++;
            p++;                /* salta el NUL */
            w->menu_n++;
        }
    }
    if (w->has_menu != on) {
        w->has_menu = on;
        wm_layout(w);
    }
    wm_compose();
    return 0;
}

int wm_set_toolbar(int id, int on, const char *flat)
{
    win_t *w = wm_find(id);
    int i;

    if (!w)
        return -1;
    w->tb_n = 0;
    for (i = 0; i < 12; i++)
        w->tb_tx[i][0] = 0;
    if (on && flat) {
        const char *p = flat;
        while (*p && w->tb_n < 12) {
            int j = 0;
            while (*p && j < 11)
                w->tb_tx[w->tb_n][j++] = *p++;
            w->tb_tx[w->tb_n][j] = 0;
            while (*p)
                p++;
            p++;                /* salta el NUL */
            w->tb_n++;
        }
    }
    if (w->has_toolbar != on) {
        w->has_toolbar = on;
        wm_layout(w);
    }
    wm_compose();
    return 0;
}

int wm_info(int id, uint32_t *out)
{
    win_t *w = wm_find(id);
    if (!w)
        return -1;
    out[0] = (uint32_t)w->x;
    out[1] = (uint32_t)w->y;
    out[2] = (uint32_t)w->w;
    out[3] = (uint32_t)w->h;
    out[4] = (uint32_t)w->cx;
    out[5] = (uint32_t)w->cy;
    out[6] = (uint32_t)w->cw;
    out[7] = (uint32_t)w->ch;
    return 0;
}

/* --- Fase 17: colas de eventos por app y enrutamiento --- */

static wm_client_t *wm_client_find(uint32_t pd)
{
    int i;
    for (i = 0; i < WM_MAX_CLIENTS; i++)
        if (wm_clients[i].used && wm_clients[i].pd == pd)
            return &wm_clients[i];
    for (i = 0; i < WM_MAX_CLIENTS; i++)
        if (!wm_clients[i].used) {
            wm_clients[i].used = 1;
            wm_clients[i].pd = pd;
            wm_clients[i].head = wm_clients[i].tail = 0;
            return &wm_clients[i];
        }
    return NULL;
}

void wm_event_deliver(uint32_t pd, const mouse_event_t *ev)
{
    wm_client_t *c = wm_client_find(pd);
    int next;
    if (!c)
        return;
    next = (c->head + 1) % EV_QUEUE_MAX;
    if (next == c->tail)            /* cola llena: descartar */
        return;
    c->q[c->head] = *ev;
    c->head = next;
}

int wm_event_claim(uint32_t pd, mouse_event_t *ev)
{
    int i;
    for (i = 0; i < WM_MAX_CLIENTS; i++) {
        wm_client_t *c = &wm_clients[i];
        if (!c->used || c->pd != pd)
            continue;
        if (c->head == c->tail)
            return -1;
        *ev = c->q[c->tail];
        c->tail = (c->tail + 1) % EV_QUEUE_MAX;
        return 0;
    }
    return -1;
}

/* Devuelve 1 si hay alguna ventana visible (el escritorio manda sobre la
 * consola: vgafx suprime el dibujado en pantalla mientras exista). */
int wm_has_windows(void)
{
    return wm_active;
}

/* Devuelve el PD al que enrutar (WM_ROUTE_TO_PD), WM_ROUTE_CONSUMED o
 * WM_ROUTE_RAW (sin ventanas: el evento va al llamador). */
int wm_route(mouse_event_t *ev)
{
    win_t *w;

    if (!wm_active || !vbe_graphics_active)
        return WM_ROUTE_RAW;

    switch (ev->type) {
    case EV_BUTTON_DOWN:
        w = wm_dragging();
        if (w) {                    /* clic durante un drag: se ignora */
            w->dragging = 0;
            return WM_ROUTE_CONSUMED;
        }
        w = wm_hit(ev->x, ev->y);
        if (!w)                     /* fondo: al foco */
            return wm_focus_pd ? (int)wm_focus_pd : WM_ROUTE_RAW;
        wm_focus_pd = w->pd;
        if (w->flags & WM_FLAG_FIXED) {      /* taskbar: sin drag */
            return (int)w->pd;
        }
        if (ev->y >= w->cy) {       /* area cliente: raise + entregar */
            if (ev->x < w->x + w->w - WM_FRAME - WM_X_BTN)
                wm_raise(w);
            return (int)w->pd;
        }
        /* Fase D: clic en la barra de menu -> va a la app (ella abre el
         * desplegable), no es un drag de la ventana. */
        if (w->has_menu && ev->y >= w->y + WM_TITLE_H + WM_FRAME)
            return (int)w->pd;
        if (ev->x >= w->x + w->w - WM_FRAME - WM_X_BTN) {
            /* boton X: la app decide cerrar con SYS_WINCLOSE */
            ev->type = EV_WINCLOSE;
            ev->key = w->id;
            return (int)w->pd;
        }
        /* barra de titulo: iniciar arrastre */
        w->dragging = 1;
        w->drag_dx = ev->x - w->x;
        w->drag_dy = ev->y - w->y;
        return WM_ROUTE_CONSUMED;
    case EV_MOVE:
        w = wm_dragging();
        if (w) {
            int nx = ev->x - w->drag_dx;
            int ny = ev->y - w->drag_dy;
            if (nx != w->x || ny != w->y) {
                w->x = nx;
                w->y = ny;
                wm_layout(w);
                wm_compose();
            }
            return WM_ROUTE_CONSUMED;
        }
        w = wm_hit(ev->x, ev->y);   /* hover: al dueno de la ventana */
        if (w)
            return (int)w->pd;
        return wm_focus_pd ? (int)wm_focus_pd : WM_ROUTE_RAW;
    case EV_BUTTON_UP:
        w = wm_dragging();
        if (w) {
            w->dragging = 0;
            return WM_ROUTE_CONSUMED;
        }
        w = wm_hit(ev->x, ev->y);
        if (w)
            return (int)w->pd;
        return wm_focus_pd ? (int)wm_focus_pd : WM_ROUTE_RAW;
    case EV_KEY:
        w = wm_topmost_app();   /* la app abierta (no la taskbar) */
        if (!w)
            w = wm_topmost();
        if (!w)
            return WM_ROUTE_RAW;
        wm_focus_pd = w->pd;
        return (int)w->pd;
    default:
        return WM_ROUTE_RAW;
    }
}
