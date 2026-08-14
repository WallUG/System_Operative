/* MyOS - user/winlib.h
 * Libreria compartida para las apps GUI nativas (ELF de la Fase 17:
 * escritorio y las apps que lanza). Wrappers de syscall (ABI int 0x80:
 * eax=numero, ebx/ecx/edx/esi=args, retorno en eax; los wrappers declaran
 * eax como clobber de salida, leccion de la Fase 11) y helpers de dibujo
 * sobre el LFB 32bpp (mismo estilo que user/win32/user32.c). */

#ifndef MYOS_WINLIB_H
#define MYOS_WINLIB_H

#include <stdint.h>
#include "win32/font8x16.h"

/* --- syscalls (kernel/syscall.h) --- */
#define SYS_EXIT   2
#define SYS_FORK   3
#define SYS_EXEC   4
#define SYS_WRITE  7
#define SYS_FSIZE  8
#define SYS_MALLOC 10
#define SYS_DREAD  12
#define SYS_DLIST  13
#define SYS_GFXINFO 15
#define SYS_EVENT  17
#define SYS_WINCREATE 18
#define SYS_WINCLOSE 19
#define SYS_WINMOVE 20
#define SYS_WINUPDATE 21
#define SYS_WININFO 22
#define SYS_DLISTDIR 33
#define SYS_DPARENT 34
#define SYS_DLOOKUP 35
#define SYS_MOUSE_INJECT 36
#define SYS_MKDIR 37
#define SYS_FCREATE_IN 38
#define SYS_FWRITE 27
#define SYS_FLUSH 29

/* --- eventos (kernel/drivers/mouse.h) --- */
#define EV_MOVE         1
#define EV_BUTTON_DOWN  2
#define EV_BUTTON_UP    3
#define EV_KEY          4
#define EV_WINCLOSE     5

/* --- flags de SYS_WINCREATE --- */
#define WM_FLAG_FIXED   0x1
#define WM_FLAG_NOFRAME 0x2
#define WM_FLAG_BG      0x4

static inline int sys_write(const char *s, uint32_t n)
{
    int r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(SYS_WRITE), "b"(s), "c"(n) : "memory");
    return r;
}

static inline void sys_exit(uint32_t code)
{
    __asm__ volatile("int $0x80" : : "a"(SYS_EXIT), "b"(code) : "memory");
    for (;;)
        ;
}

static inline int sys_fork(void)
{
    int r;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(SYS_FORK));
    return r;
}

static inline int sys_exec(const char *name)
{
    int r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(SYS_EXEC), "b"(name) : "memory");
    return r;
}

static inline void *sys_malloc(uint32_t size)
{
    void *p;
    __asm__ volatile("int $0x80" : "=a"(p) : "a"(SYS_MALLOC), "b"(size));
    return p;
}

static inline int sys_event(uint32_t *ev)
{
    int r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(SYS_EVENT), "b"(ev) : "memory");
    return r;
}

static inline int sys_dlist(uint32_t idx, char *name, uint32_t *size)
{
    int r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(SYS_DLIST), "b"(idx), "c"(name), "d"(size)
                     : "memory");
    return r;
}

/* Fase 21: directorio idx-esimo de 'parent' (MEFS_ROOT = raiz).
 * out = {name[16], size, flags}; flags bit0 = IS_DIR. -1 = fin. */
static inline int sys_dlistdir(uint32_t parent, uint32_t idx,
                               char *name, uint32_t *size, uint32_t *flags)
{
    int r;
    uint32_t out[6];
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(SYS_DLISTDIR), "b"(parent), "c"(idx), "d"(out)
                     : "memory");
    if (r != 0)
        return r;
    for (int i = 0; i < 16; i++)
        name[i] = (char)((uint8_t *)out)[i];
    if (size) *size = out[4];
    if (flags) *flags = out[5];
    return 0;
}

static inline uint32_t sys_dparent(uint32_t idx)
{
    uint32_t r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(SYS_DPARENT), "b"(idx) : "memory");
    return r;
}

static inline int sys_dlookup(uint32_t parent, const char *name)
{
    int r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(SYS_DLOOKUP), "b"(parent), "c"(name)
                     : "memory");
    return r;
}

/* Fase 23-A3: inyeccion sintetica de eventos de raton (tests). */
static inline int sys_mouse_inject(int type, int x, int y,
                                   int buttons, int key)
{
    int r;
    uint32_t ev[5];
    ev[0] = (uint32_t)type;
    ev[1] = (uint32_t)x;
    ev[2] = (uint32_t)y;
    ev[3] = (uint32_t)buttons;
    ev[4] = (uint32_t)key;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(SYS_MOUSE_INJECT), "b"(ev) : "memory");
    return r;
}

/* Fase 23-A4: escritura/persistencia en el FS. */
static inline int sys_fwrite(const char *name, const void *buf,
                             uint32_t len)
{
    int r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(SYS_FWRITE), "b"(name), "c"(buf), "d"(len)
                     : "memory");
    return r;
}

static inline int sys_flush(void)
{
    int r;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(SYS_FLUSH));
    return r;
}

static inline int sys_mkdir(uint32_t parent, const char *name)
{
    int r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(SYS_MKDIR), "b"(name), "c"(parent) : "memory");
    return r;
}

static inline int sys_fcreate_in(uint32_t parent, const char *name)
{
    int r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(SYS_FCREATE_IN), "b"(parent), "c"(name)
                     : "memory");
    return r;
}

/* MEFS_ROOT = indice de la raiz (mismas syscalls del kernel). */
#define MEFS_ROOT 0xFFFFFFFFu

static inline int sys_dread(const char *name, char *buf,
                            uint32_t off, uint32_t max)
{
    int r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(SYS_DREAD), "b"(name), "c"(buf), "d"(off),
                       "S"(max) : "memory");
    return r;
}

static inline int sys_gfxinfo(uint32_t *info)
{
    int r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(SYS_GFXINFO), "b"(info) : "memory");
    return r;
}

static inline int sys_wincreate(uint32_t *a)
{
    int r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(SYS_WINCREATE), "b"(a) : "memory");
    return r;
}

static inline int sys_winclose(int id)
{
    int r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(SYS_WINCLOSE), "b"(id) : "memory");
    return r;
}

static inline int sys_winupdate(int id)
{
    int r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(SYS_WINUPDATE), "b"(id), "c"(0)
                     : "memory");
    return r;
}

static inline int sys_wininfo(int id, uint32_t *info)
{
    int r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(SYS_WININFO), "b"(id), "c"(info) : "memory");
    return r;
}

/* --- utilidades de cadena/dec --- */

static inline uint32_t wl_strlen(const char *s)
{
    uint32_t n = 0;
    while (s[n])
        n++;
    return n;
}

/* Escribe la representacion decimal de v en out (sin terminador).
 * Devuelve la longitud. */
static inline uint32_t wl_dec(char *out, uint32_t v)
{
    char tmp[12];
    int k = 0;
    uint32_t n = 0;
    do {
        tmp[k++] = (char)('0' + v % 10);
        v /= 10;
    } while (v > 0);
    while (k > 0)
        out[n++] = tmp[--k];
    return n;
}

/* --- dibujo sobre el LFB (32 bpp 0x00RRGGBB, pitch = ancho en px) ---
 * El LFB (VBE 32bpp) espera el byte bajo = R (BGRx8888 en memoria);
 * los colores logicos 0x00RRGGBB se swapean al escribir. */

static inline uint32_t wl_px_disp(uint32_t c)
{
    return ((c & 0x0000FFFFu) << 8) |
           ((c >> 16) & 0x000000FFu) |
           (c & 0xFF000000u);
}

static inline void wl_putpixel(uint32_t *fb, int pw, int ph,
                               int x, int y, uint32_t c)
{
    if (x < 0 || y < 0 || x >= pw || y >= ph)
        return;
    fb[y * pw + x] = wl_px_disp(c);
}

static inline void wl_fillrect(uint32_t *fb, int pw, int ph,
                               int x, int y, int w, int h, uint32_t c)
{
    int i, j;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            wl_putpixel(fb, pw, ph, x + i, y + j, c);
}

static inline void wl_drawchar(uint32_t *fb, int pw, int ph,
                               int x, int y, char c, uint32_t fg)
{
    const unsigned char *g;
    int i, j;

    if (c < 32 || c > 126)
        c = '?';
    g = font8x16_basic[(unsigned char)c - 32];
    for (j = 0; j < 16; j++)
        for (i = 0; i < 8; i++)
            if (g[j] & (0x80u >> i))
                wl_putpixel(fb, pw, ph, x + i, y + j, fg);
}

static inline void wl_drawtext(uint32_t *fb, int pw, int ph,
                               int x, int y, const char *s, uint32_t fg)
{
    while (*s) {
        wl_drawchar(fb, pw, ph, x, y, *s, fg);
        x += 8;
        s++;
    }
}

static inline int wl_point_in(int px, int py, int x, int y, int w, int h)
{
    return px >= x && px < x + w && py >= y && py < y + h;
}

#endif