/* MyOS - user/explorer.c
 * Explorador de archivos (app grafica, Fase 17 + Fase 21):
 * ventana del WM con la lista de entradas del directorio actual
 * (SYS_DLISTDIR: archivos y subdirectorios con '/'), clic para
 * seleccionar, Enter para:
 *   - subdirectorio: navegar (cd)
 *   - .exe / .elf : lanzar la app (fork + exec)
 *   - otro        : ver el contenido (SYS_DREAD, texto multilinea)
 * 'b' para subir al directorio padre, 'q' o X para cerrar. */

#include <stdint.h>
#include "winlib.h"

#define EXW     580             /* tamano de la ventana */
#define EXH     380
#define FRAME   2
#define TITLE   20
#define MAXF    64              /* entradas de directorio */
#define HDR_Y   20              /* fila inicial de los items */
#define ROW_H   16

#define COLOR_CL  0x00202040u   /* cliente */
#define COLOR_ROW 0x00505090u   /* fila seleccionada */
#define COLOR_TX  0x00C0C0C0u
#define COLOR_SZ  0x00FFFF80u
#define COLOR_LBL 0x00FFFFFFu
#define COLOR_HDR 0x00E0E0FFu
#define COLOR_ERR 0x00FF9090u
#define COLOR_DIR 0x00A0E0FFu

typedef struct {
    char     name[16];
    uint32_t size;
    uint32_t flags;
} fent_t;

static fent_t files[MAXF];
static int nfiles;
static int scroll_off = 0;      /* primera fila visible (Fase 22-fix) */

static void log_line(const char *s)
{
    sys_write(s, wl_strlen(s));
}

/* Carga las entradas del directorio 'parent' (MEFS_ROOT = raiz). */
static void load_dir(uint32_t parent)
{
    int i;
    nfiles = 0;
    for (i = 0; i < MAXF; i++) {
        uint32_t sz = 0, fl = 0;
        if (sys_dlistdir(parent, (uint32_t)i, files[nfiles].name, &sz, &fl)
            != 0)
            break;
        files[nfiles].size = sz;
        files[nfiles].flags = fl;
        nfiles++;
    }
}

static void paint_list(uint32_t *buf, int cw, int ch, int sel,
                       uint32_t cwd)
{
    int i, first, vis = (ch - HDR_Y) / ROW_H;

    /* Fase 22-fix: scroll. La vista muestra las filas [scroll_off,
     * scroll_off+vis); la seleccion siempre queda visible. */
    if (sel < scroll_off)
        scroll_off = sel;
    else if (sel >= scroll_off + vis && vis > 0)
        scroll_off = sel - vis + 1;
    if (scroll_off > nfiles - vis && nfiles > vis)
        scroll_off = nfiles - vis;
    if (scroll_off < 0)
        scroll_off = 0;
    first = scroll_off;

    wl_fillrect(buf, cw, ch, 0, 0, cw, ch, COLOR_CL);
    wl_drawtext(buf, cw, ch, 4, 2,
                "MEFS (Enter: abrir/cd, b: subir, q: cerrar)", COLOR_HDR);
    wl_fillrect(buf, cw, ch, 2, HDR_Y - 2, cw - 4, 1, COLOR_HDR);
    for (i = first; i < nfiles && i < first + vis; i++) {
        char sz[16];
        int y = HDR_Y + (i - first) * ROW_H;
        uint32_t col = (files[i].flags & 1) ? COLOR_DIR : COLOR_TX;

        if (i == sel)
            wl_fillrect(buf, cw, ch, 0, y, cw, ROW_H, COLOR_ROW);
        wl_drawtext(buf, cw, ch, 4, y + 1, files[i].name,
                    i == sel ? COLOR_LBL : col);
        if (files[i].flags & 1) {
            wl_drawtext(buf, cw, ch, 4 + 8 * 15, y + 1, "/", COLOR_LBL);
        } else {
            wl_dec(sz, files[i].size);
            wl_drawtext(buf, cw, ch, 272, y + 1, sz, COLOR_SZ);
        }
    }
    (void)cwd;
}

/* Modo visor: dibuja hasta lines_max lineas de texto (las filas caben
 * cw/8 chars) y una barra de estado con el nombre del archivo. */
static void paint_text(uint32_t *buf, int cw, int ch, const char *file,
                       const char *text, int len, int truncated)
{
    int y = 2, i;
    char row[80];
    int p = 0;
    char status[64];
    int s;

    wl_fillrect(buf, cw, ch, 0, 0, cw, ch, 0x00101020);
    wl_drawtext(buf, cw, ch, 4, 2,
                "Visor (b: volver, q: cerrar)", COLOR_HDR);
    wl_fillrect(buf, cw, ch, 2, HDR_Y - 2, cw - 4, 1, COLOR_HDR);
    y = HDR_Y;
    for (i = 0; i < len && y + 16 < ch; i++) {
        char c = text[i];
        if (c == '\n') {
            row[p] = 0;
            wl_drawtext(buf, cw, ch, 4, y, row, COLOR_TX);
            p = 0;
            y += ROW_H;
            continue;
        }
        if (p < (int)sizeof(row) - 1)
            row[p++] = c;
    }
    row[p] = 0;
    if (p > 0)
        wl_drawtext(buf, cw, ch, 4, y, row, COLOR_TX);

    /* Barra de estado inferior */
    s = 0;
    {
        const char *pre = "abierto: ";
        while (*pre && s < 62)
            status[s++] = *pre++;
        i = 0;
        while (file[i] && i < 16 && s < 62)
            status[s++] = file[i++];
        if (truncated && s < 62) {
            const char *tr = " (truncado)";
            int t = 0;
            while (tr[t] && s < 62)
                status[s++] = tr[t++];
        }
    }
    status[s] = 0;
    wl_fillrect(buf, cw, ch, 0, ch - 18, cw, 18, COLOR_ROW);
    wl_drawtext(buf, cw, ch, 4, ch - 16, status, COLOR_LBL);
}

/* ¿El nombre termina en .exe o .elf? (lanzable) */
static int is_app(const char *name)
{
    int len = 0;
    while (name[len])
        len++;
    if (len < 5)
        return 0;
    return ((name[len-4] == '.' && name[len-3] == 'e' &&
             name[len-2] == 'l' && name[len-1] == 'f') ||
            (name[len-4] == '.' && name[len-3] == 'e' &&
             name[len-2] == 'x' && name[len-1] == 'e'));
}

/* Lanza un .exe/.elf como proceso hijo (fork+exec). */
static void launch_app(const char *name)
{
    int kid;
    char l[64];
    const char *pre = "exp: lanzando ";
    uint32_t p = 0;
    while (*pre)
        l[p++] = *pre++;
    {
        int i = 0;
        while (name[i] && i < 30 && p < 62)
            l[p++] = name[i++];
    }
    l[p++] = '\n';
    l[p] = 0;
    sys_write(l, p);

    kid = sys_fork();
    if (kid == 0) {
        if (sys_exec(name) != 0)
            log_line("exp: exec fallo\n");
        sys_exit(2);
    }
}

int _start(void)
{
    uint32_t info[4], a[8], ev[5], winfo[8];
    uint32_t *buf;
    int id, i, sel = 0, pressed = 0, visor = 0;
    uint32_t cx, cy, cw, ch, cwd = MEFS_ROOT;
    char *tbuf = (char *)0;

    log_line("exp: explorador iniciando\n");
    if (sys_gfxinfo(info) != 0) {
        log_line("exp: sin modo grafico\n");
        return 1;
    }

    load_dir(cwd);

    buf = (uint32_t *)sys_malloc((EXW - 2 * FRAME) * (EXH - TITLE - FRAME) * 4);
    if (buf == (void *)0) {
        log_line("exp: malloc cliente fallo\n");
        return 1;
    }

    a[0] = (uint32_t)"Explorador de archivos"; a[1] = 100; a[2] = 70;
    a[3] = EXW; a[4] = EXH; a[5] = (uint32_t)buf;
    a[6] = (EXW - 2 * FRAME) * (EXH - TITLE - FRAME) * 4;
    a[7] = 0;
    id = sys_wincreate(a);
    if (id < 0) {
        log_line("exp: crear ventana fallo\n");
        return 1;
    }
    if (sys_wininfo(id, winfo) == 0) {
        cx = winfo[4];
        cy = winfo[5];
        cw = winfo[6];
        ch = winfo[7];
    } else {
        cx = 100 + FRAME;
        cy = 70 + TITLE;
        cw = EXW - 2 * FRAME;
        ch = EXH - TITLE - FRAME;
    }

    paint_list(buf, (int)cw, (int)ch, sel, cwd);
    sys_winupdate(id);

    for (;;) {
        int item;

        if (sys_event(ev) != 0)
            continue;
        switch (ev[0]) {
        case EV_WINCLOSE:
            sys_winclose((int)ev[4]);
            goto fin;
        case EV_KEY:
            if (ev[4] == 'q')
                goto fin;
            if (visor) {
                if (ev[4] == 'b') {
                    visor = 0;
                    paint_list(buf, (int)cw, (int)ch, sel, cwd);
                    sys_winupdate(id);
                }
                break;
            }
            if (ev[4] == 'b') {
                uint32_t p2 = sys_dparent(cwd);
                if (p2 != cwd || cwd == MEFS_ROOT) {
                    cwd = p2;
                    sel = 0;
                    scroll_off = 0;
                    load_dir(cwd);
                    paint_list(buf, (int)cw, (int)ch, sel, cwd);
                    sys_winupdate(id);
                }
                break;
            }
            if (ev[4] == 'u' || ev[4] == 'k' || ev[4] == 0x102 /*up*/) {
                if (sel > 0)
                    sel--;
                paint_list(buf, (int)cw, (int)ch, sel, cwd);
                sys_winupdate(id);
                break;
            }
            if (ev[4] == 'd' || ev[4] == 'j' || ev[4] == 0x103 /*down*/) {
                if (sel < nfiles - 1)
                    sel++;
                paint_list(buf, (int)cw, (int)ch, sel, cwd);
                sys_winupdate(id);
                break;
            }
            if (ev[4] == '\n' && nfiles > 0) {
                const char *nm = files[sel].name;
                if (files[sel].flags & 1) {
                    /* subdirectorio: navegar */
                    cwd = (uint32_t)sys_dlookup(cwd, nm);
                    sel = 0;
                    scroll_off = 0;
                    load_dir(cwd);
                    paint_list(buf, (int)cw, (int)ch, sel, cwd);
                    sys_winupdate(id);
                } else if (is_app(nm)) {
                    launch_app(nm);
                } else {
                    uint32_t need;
                    visor = 1;
                    need = files[sel].size;
                    if (need > 2600)
                        need = 2600;
                    if (tbuf == (char *)0)
                        tbuf = (char *)sys_malloc(2600);
                    if (tbuf == (char *)0 || need == 0) {
                        paint_text(buf, (int)cw, (int)ch, nm, "(vacio)", 7, 0);
                    } else {
                        int got = sys_dread(nm, tbuf, 0, need + 1);
                        if (got < 0) {
                            paint_text(buf, (int)cw, (int)ch, nm,
                                       "(error de lectura)", 18, 0);
                        } else {
                            paint_text(buf, (int)cw, (int)ch, nm, tbuf, got,
                                       files[sel].size > 2600);
                        }
                    }
                    sys_winupdate(id);
                }
            }
            break;
        case EV_BUTTON_DOWN:
            item = (int)((int)ev[2] - (int)cy - HDR_Y) / ROW_H;
            if (item >= 0 && item < nfiles &&
                wl_point_in((int)ev[1], (int)ev[2],
                            (int)cx, (int)cy + HDR_Y, (int)cw,
                            nfiles * ROW_H)) {
                pressed = 1;
            }
            break;
        case EV_BUTTON_UP:
            if (pressed) {
                item = (int)((int)ev[2] - (int)cy - HDR_Y) / ROW_H;
                if (item >= 0 && item < nfiles &&
                    wl_point_in((int)ev[1], (int)ev[2],
                                (int)cx, (int)cy + HDR_Y, (int)cw,
                                nfiles * ROW_H)) {
                    sel = item;
                }
                pressed = 0;
                if (!visor) {
                    paint_list(buf, (int)cw, (int)ch, sel, cwd);
                    sys_winupdate(id);
                }
            }
            break;
        }
    }

fin:
    log_line("exp: fin\n");
    return 0;
}