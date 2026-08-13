/* MyOS - user/win32/comdlg32.c
 * comdlg32.dll: modulo Win32 fijo (ring 3) en 0xB6000000.
 * Fase C: GetOpenFileNameA real sobre la lista MEFS: ventana con la
 * lista de archivos (SYS_DLIST), navegacion por teclado (flechas,
 * PgUp/PgDn, Home/End, tipeo de prefijo), clic sobre una fila y
 * botones Abrir/Cancelar. Devuelve 1 con lpstrFile relleno o 0 al
 * cancelar (metapad comprueba el retorno y abre con 0x405a2e). */

#include <stdint.h>

#define SYS_WRITE   7
#define SYS_FSIZE   8
#define SYS_DLIST   13
#define SYS_GFXINFO 15
#define SYS_EVENT   17

#define EV_MOVE         1
#define EV_BUTTON_DOWN  2
#define EV_BUTTON_UP    3
#define EV_KEY          4

#define OFN_FILEMUSTEXIST 0x00001000
#define OFN_HIDEREADONLY  0x00000004

/* offsets del struct OPENFILENAME (32 bits) */
#define OFN_LPSTRFILTER   0x0C
#define OFN_NFILTERINDEX  0x18
#define OFN_LPSTRFILE     0x1C
#define OFN_NMAXFILE      0x20
#define OFN_LPSTRINITIAL  0x2C
#define OFN_LPSTRTITLE    0x30
#define OFN_FLAGS         0x34

#include "font8x16.h"

#define DIALOG_W  560
#define DIALOG_H  430
#define LIST_Y0   46
#define LIST_ROWS 16
#define ROW_H     20          /* 16 de fuente + 4 de separacion */
#define LIST_H    (LIST_ROWS * ROW_H)

#define COLOR_BG     0x00202040u
#define COLOR_FRAME  0x00C0C0C0u
#define COLOR_TITLE  0x00000088u
#define COLOR_TEXT   0x00FFFFFFu
#define COLOR_BTN    0x00888888u
#define COLOR_BTN_TX 0x00000000u
#define COLOR_SEL_BG 0x00FFFFFFu
#define COLOR_SEL_TX 0x000000A0u

static uint32_t *lfb;
static uint32_t scr_w, scr_h;

static inline uint32_t px_disp(uint32_t c)
{
    return ((c & 0x0000FFFFu) << 8) |
           ((c >> 16) & 0x000000FFu) |
           (c & 0xFF000000u);
}

static void putpixel(int x, int y, uint32_t c)
{
    if (x < 0 || y < 0 || x >= (int)scr_w || y >= (int)scr_h)
        return;
    lfb[y * scr_w + x] = px_disp(c);
}

static void fillrect(int x, int y, int w, int h, uint32_t c)
{
    int i, j;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            putpixel(x + i, y + j, c);
}

static void drawchar(int x, int y, char c, uint32_t fg)
{
    const unsigned char *g;
    int i, j;

    if (c < 32 || c > 126)
        c = '?';
    g = font8x16_basic[c - 32];
    for (j = 0; j < 16; j++)
        for (i = 0; i < 8; i++)
            if (g[j] & (0x80u >> i))
                putpixel(x + i, y + j, fg);
}

static void drawtext(int x, int y, const char *s, uint32_t fg)
{
    while (*s) {
        drawchar(x, y, *s, fg);
        x += 8;
        s++;
    }
}

static void drawtext_n(int x, int y, const char *s, int n, uint32_t fg)
{
    int i;
    for (i = 0; i < n && s[i]; i++) {
        drawchar(x, y, s[i], fg);
        x += 8;
    }
}

static uint32_t cdlg_len(const char *s)
{
    uint32_t n = 0;
    if (!s)
        return 0;
    while (s[n])
        n++;
    return n;
}

static int sys_dlist(uint32_t idx, char *name, uint32_t *size)
{
    int r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(SYS_DLIST), "b"(idx), "c"(name), "d"(size)
                     : "memory");
    return r;
}

static int sys_fsize(const char *name)
{
    int r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(SYS_FSIZE), "b"(name)
                     : "memory");
    return r;
}

static void dlg_write(const char *s, uint32_t len)
{
    int r;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(SYS_WRITE), "b"(s), "c"(len));
}

static int sys_gfxinfo(uint32_t *info)
{
    int r;
    __asm__ volatile("int $0x80"
                     : "=a"(r)
                     : "a"(SYS_GFXINFO), "b"(info)
                     : "memory");
    return r;
}

static int sys_event(uint32_t *ev)
{
    int r;
    __asm__ volatile("int $0x80"
                     : "=a"(r)
                     : "a"(SYS_EVENT), "b"(ev)
                     : "memory");
    return r;
}

/* --- lista de archivos del MEFS --- */

#define MAX_FILES 64
#define NAME_MAX  32

typedef struct {
    char     name[NAME_MAX];
    uint32_t size;
} cdlg_file_t;

static cdlg_file_t files[MAX_FILES];
static int file_count;

/* Filtro activo: lista de extensiones (p. ej. "txt" y "c") o vacio =
 * "*.*" (mostrar todo). Se extrae del bloque lpstrFilter + nFilterIndex
 * (formato Windows: pares "desc\0pat;pat2\0...\0\0", index 1-based). */
static int ext_count;
static char ext_list[8][8];

static void parse_filter(const char *filt, uint32_t idx)
{
    const char *p;
    uint32_t n = 0;
    ext_count = 0;

    if (filt == 0)
        return;
    /* p = patron del filtro activo (segunda cadena del par idx) */
    p = filt;
    while (n + 1 < idx) {
        while (*p)
            p++;
        p++;
        if (*p == 0)
            return;             /* fin de la lista */
        p++;
        n++;
    }
    if (n + 1 != idx)
        return;
    /* ahora p apunta a los patrones "*.txt;*.htm;..." */
    while (*p && ext_count < 8) {
        const char *q = p;
        while (*q && *q != ';')
            q++;
        /* copiar "*.ext" -> "ext" */
        if (q - p >= 3 && p[0] == '*' && p[1] == '.') {
            int k = 0;
            while (p + 2 + k < q && k < 7) {
                ext_list[ext_count][k] = p[2 + k];
                k++;
            }
            ext_list[ext_count][k] = 0;
            if (ext_list[ext_count][0] == '*' &&
                ext_list[ext_count][1] == 0) {
                ext_count = 0;  /* "*.*" -> todo */
                return;
            }
            ext_count++;
        }
        p = (*q) ? q + 1 : q;
    }
}

static char low8(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}

static int file_matches(const cdlg_file_t *f)
{
    int i;
    if (ext_count == 0)
        return 1;
    for (i = 0; i < ext_count; i++) {
        int k = 0, n = 0;
        while (ext_list[i][n])
            n++;
        /* termina con '.' + ext? */
        while (f->name[k])
            k++;
        if (k > n + 1 && f->name[k - n - 1] == '.') {
            int m = 0, ok = 1;
            while (m < n)
                if (low8(f->name[k - n + m]) != low8(ext_list[i][m])) {
                    ok = 0;
                    break;
                } else
                    m++;
            if (ok)
                return 1;
        }
    }
    return 0;
}

static void load_list(void)
{
    char name[16];
    uint32_t sz;
    uint32_t idx = 0;
    file_count = 0;
    while (file_count < MAX_FILES && sys_dlist(idx, name, &sz) == 0) {
        int k = 0;
        while (name[k] && k < NAME_MAX - 1) {
            files[file_count].name[k] = name[k];
            k++;
        }
        files[file_count].name[k] = 0;
        files[file_count].size = sz;
        if (file_matches(&files[file_count]))
            file_count++;
        idx++;
    }
}

/* --- boton simple (hover/pressed) --- */

typedef struct {
    int x, y, w, h;
    const char *label;
    int hovered, pressed;
} cdlg_btn_t;

static int pt_in(int px, int py, int x, int y, int w, int h)
{
    return px >= x && px < x + w && py >= y && py < y + h;
}

static void draw_btn(const cdlg_btn_t *b)
{
    uint32_t bg = COLOR_BTN, fg = COLOR_BTN_TX;
    if (b->pressed) {
        bg = COLOR_TEXT;
        fg = COLOR_BTN;
    } else if (b->hovered) {
        bg = 0x00AAAAAAu;
    }
    fillrect(b->x, b->y, b->w, b->h, bg);
    drawtext(b->x + (b->w - (int)cdlg_len(b->label) * 8) / 2,
             b->y + (b->h - 16) / 2, b->label, fg);
}

static int btn_feed(cdlg_btn_t *b, const uint32_t *ev)
{
    int in = pt_in((int)ev[1], (int)ev[2], b->x, b->y, b->w, b->h);

    if (ev[0] == EV_BUTTON_DOWN) {
        if (!in)
            return 0;
        b->pressed = 1;
        draw_btn(b);
        return 0;
    }
    if (ev[0] == EV_BUTTON_UP) {
        if (b->pressed) {
            b->pressed = 0;
            b->hovered = in;
            draw_btn(b);
            return in ? 1 : 0;
        }
        if (b->hovered != in) {
            b->hovered = in;
            draw_btn(b);
        }
        return 0;
    }
    if (ev[0] == EV_MOVE && !b->pressed && b->hovered != in) {
        b->hovered = in;
        draw_btn(b);
    }
    return 0;
}

/* --- dialog --- */

static int dl_x, dl_y;          /* posicion de la ventana */
static int sel;                 /* fila seleccionada */
static int scroll;              /* primer archivo visible */
static char fname[NAME_MAX];    /* campo de nombre */
static int fname_len;

static void draw_list(void)
{
    int i;
    for (i = 0; i < LIST_ROWS; i++) {
        int idx = scroll + i;
        int y = dl_y + LIST_Y0 + i * ROW_H;
        if (idx >= file_count)
            break;
        if (idx == sel) {
            fillrect(dl_x + 10, y - 1, DIALOG_W - 20, 17, COLOR_SEL_BG);
            drawtext(dl_x + 14, y, files[idx].name, COLOR_SEL_TX);
        } else {
            drawtext(dl_x + 14, y, files[idx].name, COLOR_TEXT);
        }
    }
}

static void draw_dialog(const char *title)
{
    fillrect(dl_x - 3, dl_y - 3, DIALOG_W + 6, DIALOG_H + 6, COLOR_FRAME);
    fillrect(dl_x, dl_y, DIALOG_W, DIALOG_H, COLOR_BG);
    fillrect(dl_x, dl_y, DIALOG_W, 20, COLOR_TITLE);
    drawtext(dl_x + 6, dl_y + 2, title ? title : "Abrir archivo",
             COLOR_TEXT);

    /* lista */
    fillrect(dl_x + 8, dl_y + LIST_Y0 - 3, DIALOG_W - 16, LIST_H + 4,
             0x00101030u);
    draw_list();

    /* campo de nombre */
    drawtext(dl_x + 12, dl_y + DIALOG_H - 96, "Archivo:",
             COLOR_TEXT);
    fillrect(dl_x + 12 + 9 * 8, dl_y + DIALOG_H - 96,
             DIALOG_W - 24 - 9 * 8, 17, COLOR_SEL_BG);
    drawtext_n(dl_x + 14 + 9 * 8, dl_y + DIALOG_H - 96, fname, fname_len,
               COLOR_SEL_TX);
    {
        int cx = dl_x + 14 + 9 * 8 + fname_len * 8;
        fillrect(cx, dl_y + DIALOG_H - 96, 2, 16, 0x00FF0000u);
    }
}

static void clamp_view(void)
{
    if (sel < scroll)
        scroll = sel;
    if (sel >= scroll + LIST_ROWS)
        scroll = sel - LIST_ROWS + 1;
}

/* Selecciona la primera fila cuyo nombre empieza por el prefijo. */
static void find_prefix(void)
{
    int i;
    if (fname_len == 0)
        return;
    for (i = 0; i < file_count; i++) {
        int k = 0;
        while (k < fname_len && low8(files[i].name[k]) == low8(fname[k]))
            k++;
        if (k == fname_len) {
            sel = i;
            clamp_view();
            return;
        }
    }
}

/* Fase 20-B: dialogo de confirmacion de sobrescritura. Devuelve 1 (Si)
 * o 0 (No). 'name' = archivo que ya existe. Se dibuja una caja modal
 * sobre la pantalla y se espera a que el usuario elija. */
static int confirm_overwrite(const char *name)
{
    uint32_t info[4];
    uint32_t ev[5];
    const int cw = 360, ch = 140;
    int cx, cy;
    cdlg_btn_t btn_yes, btn_no;
    int done = 0, result = 0;
    char msg[96];

    if (sys_gfxinfo(info) != 0)
        return 1;               /* sin framebuffer: asumir Si */

    cx = ((int)info[1] - cw) / 2;
    cy = ((int)info[2] - ch) / 2;

    fillrect(cx - 3, cy - 3, cw + 6, ch + 6, COLOR_FRAME);
    fillrect(cx, cy, cw, ch, COLOR_BG);
    fillrect(cx, cy, cw, 20, COLOR_TITLE);
    drawtext(cx + 6, cy + 2, "Confirmar sobrescritura", COLOR_TEXT);

    /* mensaje: el archivo ya existe */
    {
        uint32_t k = 0, m = 0;
        static const char pre[] = "'";
        static const char post[] = "' ya existe. Quieres sobrescribirlo?";
        while (pre[m]) msg[k++] = pre[m++];
        m = 0;
        while (name[m] && k < sizeof(msg) - 40) msg[k++] = name[m++];
        m = 0;
        while (post[m] && k < sizeof(msg) - 1) msg[k++] = post[m++];
        msg[k] = 0;
    }
    drawtext(cx + 16, cy + 36, msg, COLOR_TEXT);

    btn_yes.x = cx + 60;  btn_yes.y = cy + ch - 48;
    btn_yes.w = 80;       btn_yes.h = 26;
    btn_yes.label = "Si";
    btn_yes.hovered = 0;  btn_yes.pressed = 0;
    btn_no = btn_yes;
    btn_no.x = cx + 220;
    btn_no.label = "No";

    draw_btn(&btn_yes);
    draw_btn(&btn_no);

    while (!done) {
        if (sys_event(ev) != 0)
            continue;
        switch (ev[0]) {
        case EV_KEY:
            if (ev[4] == 'y' || ev[4] == 'Y') { done = 1; result = 1; }
            else if (ev[4] == 'n' || ev[4] == 'N') { done = 1; result = 0; }
            else if (ev[4] == '\n') { done = 1; result = 1; }
            else if (ev[4] == 27) { done = 1; result = 0; }
            break;
        case EV_BUTTON_DOWN:
        case EV_BUTTON_UP:
        case EV_MOVE:
            if (btn_feed(&btn_yes, ev)) { done = 1; result = 1; break; }
            if (btn_feed(&btn_no, ev)) { done = 1; result = 0; break; }
            break;
        default:
            break;
        }
    }
    return result;
}

static void dlg_current_name(char *dst);

/* Fase 20-B: devuelve 1 si el nombre actual de Guardar ya existe (hay
 * que confirmar sobrescritura). */
static int dlg_wants_confirm(void)
{
    char nm[NAME_MAX];
    if (fname_len == 0 && sel < file_count)
        return 0;               /* nombre del archivo seleccionado (a salvar nuevo no pide confirmacion si se tipeo) */
    dlg_current_name(nm);
    return sys_fsize(nm) >= 0;
}

/* Copia a 'dst' el nombre actual del dialogo (fname tipeado o el
 * archivo seleccionado). */
static void dlg_current_name(char *dst)
{
    uint32_t k = 0;
    if (fname_len > 0) {
        while (k < NAME_MAX - 1 && fname[k]) {
            dst[k] = fname[k];
            k++;
        }
    } else if (sel < file_count && files[sel].name[0]) {
        while (k < NAME_MAX - 1 && files[sel].name[k]) {
            dst[k] = files[sel].name[k];
            k++;
        }
    }
    dst[k] = 0;
}

static uint32_t dlg_run(const void *ofn, int is_save)
{
    uint32_t info[4];
    uint32_t ev[5];
    uint32_t flags = 0;
    const char *initial = 0, *title = 0;
    char *out = 0;
    uint32_t out_n = 0;
    cdlg_btn_t btn_open, btn_cancel;
    int done = 0, result = 0;
    const char *ok_label = is_save ? "Guardar" : "Abrir";

    if (ofn == 0)
        return 0;
    out = *(char **)((const char *)ofn + OFN_LPSTRFILE);
    out_n = *(uint32_t *)((const char *)ofn + OFN_NMAXFILE);
    initial = *(const char **)((const char *)ofn + OFN_LPSTRINITIAL);
    title = *(const char **)((const char *)ofn + OFN_LPSTRTITLE);
    flags = *(uint32_t *)((const char *)ofn + OFN_FLAGS);
    if (out == 0 || out_n == 0)
        return 0;

    parse_filter(*(const char **)((const char *)ofn + OFN_LPSTRFILTER),
                 *(uint32_t *)((const char *)ofn + OFN_NFILTERINDEX));
    load_list();
    if (file_count == 0 && !is_save) {
        out[0] = 0;
        return 0;
    }

    /* seleccion inicial: el nombre de lpstrFile si existe, sino la
     * primera entrada; dir inicial ignorado (MEFS sin subdirectorios) */
    fname[0] = 0;
    fname_len = 0;
    sel = 0;
    scroll = 0;
    if (initial) {
        uint32_t i = 0;
        while (initial[i] && i < out_n - 1) {
            out[i] = initial[i];
            i++;
        }
        out[i] = 0;
        for (i = 0; i < (uint32_t)file_count; i++)
            if (files[i].name[0]) {
                sel = (int)i;
                break;
            }
    } else {
        out[0] = 0;
    }

    if (sys_gfxinfo(info) != 0)
        return 0;
    lfb = (uint32_t *)info[0];
    scr_w = info[1];
    scr_h = info[2];

    dl_x = ((int)scr_w - DIALOG_W) / 2;
    dl_y = ((int)scr_h - DIALOG_H) / 2;

    if (is_save)
        dlg_write("[cdlg] GetSaveFileNameA dialog\n", 30);
    else
        dlg_write("[cdlg] GetOpenFileNameA dialog\n", 31);

    btn_open.x = dl_x + DIALOG_W / 2 - 165;
    btn_open.y = dl_y + DIALOG_H - 56;
    btn_open.w = 70;
    btn_open.h = 24;
    btn_open.label = (char *)ok_label;
    btn_open.hovered = 0;
    btn_open.pressed = 0;
    btn_cancel = btn_open;
    btn_cancel.x = dl_x + DIALOG_W / 2 + 95;
    btn_cancel.label = "Cancelar";

    draw_dialog(title);
    draw_btn(&btn_open);
    draw_btn(&btn_cancel);

    while (!done) {
        if (sys_event(ev) != 0)
            continue;
        switch (ev[0]) {
        case EV_KEY: {
            uint32_t key = ev[4];
            if (key == 0x102) {                 /* UP */
                if (sel > 0) {
                    sel--;
                    clamp_view();
                    draw_dialog(title);
                    draw_btn(&btn_open);
                    draw_btn(&btn_cancel);
                }
            } else if (key == 0x103) {          /* DOWN */
                if (sel < file_count - 1) {
                    sel++;
                    clamp_view();
                    draw_dialog(title);
                    draw_btn(&btn_open);
                    draw_btn(&btn_cancel);
                }
            } else if (key == 0x106) {          /* PGUP */
                sel -= 10;
                if (sel < 0)
                    sel = 0;
                clamp_view();
                draw_dialog(title);
                draw_btn(&btn_open);
                draw_btn(&btn_cancel);
            } else if (key == 0x107) {          /* PGDN */
                sel += 10;
                if (sel >= file_count)
                    sel = file_count - 1;
                clamp_view();
                draw_dialog(title);
                draw_btn(&btn_open);
                draw_btn(&btn_cancel);
            } else if (key == 0x104) {          /* HOME */
                sel = 0;
                scroll = 0;
                draw_dialog(title);
                draw_btn(&btn_open);
                draw_btn(&btn_cancel);
            } else if (key == 0x105) {          /* END */
                sel = file_count - 1;
                clamp_view();
                draw_dialog(title);
                draw_btn(&btn_open);
                draw_btn(&btn_cancel);
            } else if (key == '\n') {           /* Enter */
                if (is_save && dlg_wants_confirm()) {
                    /* el archivo ya existe: confirmar antes de cerrar */
                    char nm[NAME_MAX];
                    dlg_current_name(nm);
                    if (!confirm_overwrite(nm))
                        break;              /* No: seguir en el dialogo */
                }
                done = 1;
                result = 1;
            } else if (key == 27) {             /* Esc */
                done = 1;
                result = 0;
            } else if (key == '\b') {           /* backspace */
                if (fname_len > 0) {
                    fname_len--;
                    fname[fname_len] = 0;
                    draw_dialog(title);
                    draw_btn(&btn_open);
                    draw_btn(&btn_cancel);
                }
            } else if (key >= 32 && key <= 126) {
                if (fname_len < NAME_MAX - 1) {
                    fname[fname_len++] = (char)key;
                    fname[fname_len] = 0;
                    find_prefix();
                    draw_dialog(title);
                    draw_btn(&btn_open);
                    draw_btn(&btn_cancel);
                }
            }
            break;
        }
        case EV_BUTTON_DOWN:
        case EV_BUTTON_UP:
        case EV_MOVE:
            if (btn_feed(&btn_open, ev)) {
                if (is_save && dlg_wants_confirm()) {
                    char nm[NAME_MAX];
                    dlg_current_name(nm);
                    if (!confirm_overwrite(nm))
                        break;              /* No: seguir en el dialogo */
                }
                done = 1;
                result = 1;
                break;
            }
            if (btn_feed(&btn_cancel, ev)) {
                done = 1;
                result = 0;
                break;
            }
            if (ev[0] == EV_BUTTON_DOWN) {
                int row = ((int)ev[2] - (dl_y + LIST_Y0)) / ROW_H;
                if (row >= 0 && row < LIST_ROWS) {
                    int idx = scroll + row;
                    if (idx < file_count && idx != sel) {
                        sel = idx;
                        draw_list();
                    }
                }
            }
            break;
        default:
            break;
        }
    }

    if (result) {
        const char *src;
        uint32_t k = 0;
        if (fname_len > 0) {
            if (is_save) {
                /* Guardar: usa el nombre tipeado tal cual (puede ser
                 * nuevo). Si es exacto a uno existente, se sobrescribe. */
                src = fname;
            } else {
                /* Abrir: completar con el archivo que matchea */
                int i, m = 0;
                for (i = 0; i < file_count; i++) {
                    int j = 0;
                    while (j < fname_len &&
                           low8(files[i].name[j]) == low8(fname[j]))
                        j++;
                    if (j == fname_len) {
                        m = 1;
                        break;
                    }
                }
                src = m ? files[sel].name : fname;
            }
        } else {
            src = (file_count > 0) ? files[sel].name : fname;
        }
        while (src[k] && k < out_n - 1) {
            out[k] = src[k];
            k++;
        }
        out[k] = 0;
    } else {
        out[0] = 0;
    }
    (void)flags;
    return (uint32_t)result;
}

uint32_t GetOpenFileNameA(const void *ofn)
{
    return dlg_run(ofn, 0);
}

uint32_t GetSaveFileNameA(const void *ofn)
{
    return dlg_run(ofn, 1);
}

/* FindTextA/ReplaceTextA devuelven HWND del dialogo modelo (0 = no
 * abierto). */
uint32_t FindTextA(const void *fr)
{
    (void)fr;
    return 0;
}

uint32_t ReplaceTextA(const void *fr)
{
    (void)fr;
    return 0;
}

uint32_t ChooseFontA(const void *cf)
{
    (void)cf;
    return 0;
}

uint32_t ChooseColorA(const void *cc)
{
    (void)cc;
    return 0;
}

uint32_t PrintDlgA(const void *pd)
{
    (void)pd;
    return 0;
}

uint32_t PageSetupDlgA(const void *psd)
{
    (void)psd;
    return 0;
}

uint32_t CommDlgExtendedError(void)
{
    return 0;
}

typedef struct {
    char     name[32];
    uint32_t fn;
} win32_export_t;

win32_export_t __exports[] __attribute__((section(".exports"))) = {
    { "GetOpenFileNameA",     (uint32_t)&GetOpenFileNameA },
    { "GetSaveFileNameA",     (uint32_t)&GetSaveFileNameA },
    { "FindTextA",            (uint32_t)&FindTextA },
    { "ReplaceTextA",         (uint32_t)&ReplaceTextA },
    { "ChooseFontA",          (uint32_t)&ChooseFontA },
    { "ChooseColorA",         (uint32_t)&ChooseColorA },
    { "PrintDlgA",            (uint32_t)&PrintDlgA },
    { "PageSetupDlgA",        (uint32_t)&PageSetupDlgA },
    { "CommDlgExtendedError", (uint32_t)&CommDlgExtendedError },
    { "", 0 },
};
