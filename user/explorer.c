/* MyOS - user/explorer.c
 * Explorador de archivos (app grafica lanzada por el escritorio, Fase 17):
 * ventana del WM con la lista de archivos del MEFS (SYS_DLIST), clic
 * para seleccionar, Enter para abrir el archivo en modo visor (SYS_DREAD,
 * texto multilinea), 'b' para volver a la lista, 'q' o X para cerrar. */

#include <stdint.h>
#include "winlib.h"

#define EXW     580             /* tamano de la ventana */
#define EXH     380
#define FRAME   2
#define TITLE   20
#define MAXF    32              /* entradas de directorio */
#define HDR_Y   20              /* fila inicial de los items */
#define ROW_H   16

#define COLOR_CL  0x00202040u   /* cliente */
#define COLOR_ROW 0x00505090u   /* fila seleccionada */
#define COLOR_TX  0x00C0C0C0u
#define COLOR_SZ  0x00FFFF80u
#define COLOR_LBL 0x00FFFFFFu
#define COLOR_HDR 0x00E0E0FFu
#define COLOR_ERR 0x00FF9090u

typedef struct {
    char     name[16];
    uint32_t size;
} fent_t;

static fent_t files[MAXF];
static int nfiles;

static void log_line(const char *s)
{
    sys_write(s, wl_strlen(s));
}

static void paint_list(uint32_t *buf, int cw, int ch, int sel)
{
    int i;

    wl_fillrect(buf, cw, ch, 0, 0, cw, ch, COLOR_CL);
    wl_drawtext(buf, cw, ch, 4, 2,
                "MEFS (clic: seleccionar, Enter: ver, b: lista, q: cerrar)",
                COLOR_HDR);
    wl_fillrect(buf, cw, ch, 2, HDR_Y - 2, cw - 4, 1, COLOR_HDR);
    for (i = 0; i < nfiles; i++) {
        char sz[16];
        int y = HDR_Y + i * ROW_H;

        wl_dec(sz, files[i].size);
        if (i == sel)
            wl_fillrect(buf, cw, ch, 0, y, cw, ROW_H, COLOR_ROW);
        wl_drawtext(buf, cw, ch, 4, y + 1, files[i].name,
                    i == sel ? COLOR_LBL : COLOR_TX);
        wl_drawtext(buf, cw, ch, 272, y + 1, sz, COLOR_SZ);
    }
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

int _start(void)
{
    uint32_t info[4], a[8], ev[5], winfo[8];
    uint32_t *buf;
    int id, i, sel = 0, pressed = 0, visor = 0;
    uint32_t cx, cy, cw, ch;
    char *tbuf = (char *)0;

    log_line("exp: explorador iniciando\n");
    if (sys_gfxinfo(info) != 0) {
        log_line("exp: sin modo grafico\n");
        return 1;
    }

    for (i = 0; i < MAXF; i++) {
        uint32_t sz = 0;
        if (sys_dlist((uint32_t)i, files[nfiles].name, &sz) != 0)
            break;
        files[nfiles].size = sz;
        nfiles++;
    }

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

    {
        char l[48];
        uint32_t p = 0, sl;
        const char *pre = "exp: lista = ";
        while (*pre)
            l[p++] = *pre++;
        sl = wl_dec(l + p, (uint32_t)nfiles);
        p += sl;
        l[p++] = '\n';
        l[p] = 0;
        sys_write(l, p);
    }

    paint_list(buf, (int)cw, (int)ch, sel);
    sys_winupdate(id);
    log_line("exp: ventana creada id=");
    {
        char d[12];
        uint32_t sl = wl_dec(d, (uint32_t)id);
        sys_write(d, sl);
    }
    sys_write("\n", 1);

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
                    paint_list(buf, (int)cw, (int)ch, sel);
                    sys_winupdate(id);
                }
                break;
            }
            if (ev[4] == '\n' && nfiles > 0) {
                const char *nm = files[sel].name;
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
                        log_line("exp: DREAD fallo\n");
                        paint_text(buf, (int)cw, (int)ch, nm,
                                   "(error de lectura)", 18, 0);
                    } else {
                        paint_text(buf, (int)cw, (int)ch, nm, tbuf, got,
                                   files[sel].size > 2600);
                    }
                }
                sys_winupdate(id);
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
                    {
                        char l[64];
                        const char *pre = "exp: seleccionado ";
                        uint32_t p2 = 0, sl;
                        while (*pre)
                            l[p2++] = *pre++;
                        sl = wl_dec(l + p2, (uint32_t)item);
                        p2 += sl;
                        l[p2++] = ' ';
                        l[p2++] = '\n';
                        l[p2] = 0;
                        sys_write(l, p2);
                    }
                }
                pressed = 0;
                if (!visor) {
                    paint_list(buf, (int)cw, (int)ch, sel);
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