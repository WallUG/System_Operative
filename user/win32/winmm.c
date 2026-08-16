/* MyOS - user/win32/winmm.c
 * winmm.dll (Fase 25-W2A paso 5): multimedia sobre el AC'97 del
 * kernel (SYS_AUDIO_PLAY 46, reproduccion sincrona por DMA).
 *
 * - PlaySoundA: SND_FILENAME -> parsea el RIFF y reproduce.
 * - waveOutOpen/Write/Close/Reset/GetDevCaps: una sola salida,
 *   waveOutWrite BLOQUEA hasta terminar (sin IRQ de audio en ring 3
 *   ni hilo de fondo; suficiente para players de WAV y sndrec32).
 * - mciSendStringA: minimo (open/play/close alias).
 * - timeGetTime: ms reales (SYS_TICKS 41).
 * Conversiones de formato: 8->16 bit (unsigned), mono->stereo
 * (duplicado); la tasa se pasa al codec tal cual (8000-48000). */

#include <stdint.h>

#define KERNEL_BASE 0xB0000000u

#define SYS_WRITE  7
#define SYS_FSIZE  8
#define SYS_DREAD  12
#define SYS_TICKS  41
#define SYS_AUDIO_PLAY 46

/* --- util de consola (debug) --- */

static uint32_t strlen_u(const char *s)
{
    uint32_t n = 0;
    while (s[n]) n++;
    return n;
}

static void trace(const char *s)
{
    uint32_t r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(SYS_WRITE), "b"(s), "c"(strlen_u(s))
                     : "memory");
    (void)r;
}

static void trace_num(uint32_t v)
{
    char tmp[12];
    int i, p = 0;
    if (v == 0) {
        trace("0");
        return;
    }
    while (v && p < 11) {
        tmp[p++] = (char)('0' + v % 10);
        v /= 10;
    }
    for (i = p - 1; i >= 0; i--) {
        char c = tmp[i];
        uint32_t r;
        __asm__ volatile("int $0x80" : "=a"(r)
                         : "a"(SYS_WRITE), "b"(&c), "c"(1) : "memory");
        (void)r;
    }
}

static uint32_t sys_ticks(void)
{
    uint32_t r;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(SYS_TICKS));
    return r;
}

static int sys_fsize(const char *name)
{
    int r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(SYS_FSIZE), "b"(name) : "memory");
    return r;
}

static int sys_dread(const char *name, void *buf, uint32_t off, uint32_t max)
{
    int r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(SYS_DREAD), "b"(name), "c"(buf), "d"(off),
                       "S"(max)
                     : "memory");
    return r;
}

static int sys_audio_play(const uint8_t *buf, uint32_t bytes, uint32_t rate)
{
    int r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(SYS_AUDIO_PLAY), "b"(buf), "c"(bytes), "d"(rate)
                     : "memory");
    return r;
}

/* --- heap de la DLL (SYS_MALLOC es del proceso; lo usa igual) --- */

static void *win_malloc(uint32_t size)
{
    uint32_t r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(10), "b"(size) : "memory");
    return (void *)r;
}

static void win_free(void *p)
{
    uint32_t r;
    __asm__ volatile("int $0x80" : "=a"(r)
                     : "a"(11), "b"(p) : "memory");
    (void)r;
}

/* --- WAV (RIFF): extrae fmt + data --- */

typedef struct {
    uint32_t rate;
    uint32_t channels;
    uint32_t bits;
    const uint8_t *data;
    uint32_t datalen;
} wav_info_t;

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

/* Parse RIFF/WAVE: devuelve 0 si OK. */
static int wav_parse(const uint8_t *w, uint32_t len, wav_info_t *out)
{
    uint32_t pos;
    if (len < 12 || w[0] != 'R' || w[1] != 'I' || w[2] != 'F' ||
        w[3] != 'F' || w[8] != 'W' || w[9] != 'A' || w[10] != 'V' ||
        w[11] != 'E')
        return -1;
    pos = 12;
    out->rate = 48000;
    out->channels = 2;
    out->bits = 16;
    out->data = 0;
    out->datalen = 0;
    while (pos + 8 <= len) {
        uint32_t sz = rd32(w + pos + 4);
        if (w[pos] == 'f' && w[pos + 1] == 'm' && w[pos + 2] == 't') {
            if (pos + 8 + 16 <= len) {
                out->channels = rd16(w + pos + 10);
                out->rate = rd32(w + pos + 12);
                out->bits = rd16(w + pos + 22);
            }
        } else if (w[pos] == 'd' && w[pos + 1] == 'a' && w[pos + 2] == 't') {
            uint32_t ds = sz;
            if (pos + 8 + ds > len)
                ds = len - (pos + 8);
            out->data = w + pos + 8;
            out->datalen = ds;
            return 0;
        }
        pos += 8 + sz + (sz & 1);
    }
    return (out->data != 0) ? 0 : -1;
}

/* Convierte PCM a 16-bit stereo y lo reproduce (bloquea). */
static int wav_play(const wav_info_t *wi)
{
    uint32_t rate = wi->rate, ch = wi->channels, bits = wi->bits;
    uint32_t in, outn, k;
    uint8_t *conv;
    int r;

    if (wi->datalen == 0)
        return 0;
    if (rate < 8000 || rate > 48000)
        rate = 48000;
    if (ch != 1 && ch != 2)
        ch = 2;
    if (bits != 8 && bits != 16)
        bits = 16;

    /* 16-bit stereo directo: sin conversion. */
    if (bits == 16 && ch == 2) {
        r = sys_audio_play(wi->data, wi->datalen, rate);
        return (r == 0) ? 0 : -1;
    }

    /* conversion: bytes por frame de salida = 4 (16-bit stereo) */
    outn = wi->datalen / ((bits / 8) * ch) * 4;
    conv = (uint8_t *)win_malloc(outn);
    if (conv == 0)
        return -1;
    in = 0;
    k = 0;
    while (in + (bits / 8) * ch <= wi->datalen && k + 4 <= outn) {
        int16_t l, rch;
        if (bits == 8) {
            l = (int16_t)(((int)wi->data[in] - 128) << 8);
            if (ch == 2)
                rch = (int16_t)(((int)wi->data[in + 1] - 128) << 8);
            else
                rch = l;
            in += ch;
        } else {
            l = (int16_t)rd16(wi->data + in);
            if (ch == 2)
                rch = (int16_t)rd16(wi->data + in + 2);
            else
                rch = l;
            in += ch * 2;
        }
        conv[k++] = (uint8_t)l;
        conv[k++] = (uint8_t)(l >> 8);
        conv[k++] = (uint8_t)rch;
        conv[k++] = (uint8_t)(rch >> 8);
    }
    r = sys_audio_play(conv, k, rate);
    win_free(conv);
    return (r == 0) ? 0 : -1;
}

/* Lee un archivo completo (MEFS) y lo reproduce. 0 = OK. */
static int play_file(const char *name)
{
    int sz;
    uint8_t *w;
    wav_info_t wi;
    int r;

    if (name == 0)
        return -1;
    sz = sys_fsize(name);
    if (sz < 0 || sz == 0)
        return -1;
    w = (uint8_t *)win_malloc((uint32_t)sz);
    if (w == 0)
        return -1;
    if (sys_dread(name, w, 0, (uint32_t)sz) != sz) {
        win_free(w);
        return -1;
    }
    r = wav_parse(w, (uint32_t)sz, &wi);
    if (r == 0) {
        trace("[winmm] playing '");
        trace(name);
        trace("' rate=");
        trace_num(wi.rate);
        trace(" ch=");
        trace_num(wi.channels);
        trace(" bits=");
        trace_num(wi.bits);
        trace(" len=");
        trace_num(wi.datalen);
        trace("\n");
        r = wav_play(&wi);
    }
    win_free(w);
    return r;
}

/* --- exports --- */

/* PlaySoundA: solo SND_FILENAME (0x20000); NULL = no-op. */
uint32_t __attribute__((stdcall)) PlaySoundA(const char *name, void *hmod,
                        uint32_t flags)
{
    (void)hmod;
    if ((flags & 0x20000u) == 0 || name == 0) {
        trace("[winmm] PlaySoundA sin SND_FILENAME (no-op)\n");
        return 1;
    }
    trace("[winmm] PlaySoundA '");
    trace(name);
    trace("'\n");
    return (uint32_t)(play_file(name) == 0);
}

/* WAVEOUTCAPS: estructura minima de 52 bytes. */
typedef struct {
    uint16_t wMid;
    uint16_t wPid;
    uint32_t vDriverVersion;
    char     szPname[32];
    uint32_t dwFormats;
    uint16_t wChannels;
    uint16_t wReserved1;
    uint32_t dwSupport;
} my_WAVEOUTCAPS;

uint32_t __attribute__((stdcall)) waveOutGetDevCaps(uint32_t dev, void *caps,
                        uint32_t size)
{
    my_WAVEOUTCAPS *c = (my_WAVEOUTCAPS *)caps;
    (void)dev;
    if (c == 0 || size < 44)
        return 33;                      /* MMSYSERR_INVALPARAM */
    c->wMid = 0xFFFF;
    c->wPid = 0;
    c->vDriverVersion = 0x0100;
    {
        static const char nm[] = "MyOS AC97";
        uint32_t i;
        for (i = 0; i < 31 && nm[i]; i++)
            c->szPname[i] = nm[i];
        c->szPname[i] = 0;
    }
    c->dwFormats = 0x00010000u;         /* WAVE_FORMAT_PCM */
    c->wChannels = 2;
    c->wReserved1 = 0;
    c->dwSupport = 0;
    return 0;
}

static uint32_t wave_out_open;
static uint32_t wo_rate, wo_ch, wo_bits;    /* formato del waveOutOpen */

/* WAVEFORMATEX (18 B): wFormatTag, nChannels, nSamplesPerSec,
 * nAvgBytesPerSec, nBlockAlign, wBitsPerSample, cbSize. */
uint32_t __attribute__((stdcall)) waveOutOpen(void *phwo, uint32_t dev,
                        const void *fmt, void *cb, void *inst, uint32_t flags)
{
    const uint8_t *f = (const uint8_t *)fmt;
    (void)dev; (void)cb; (void)inst; (void)flags;
    if (phwo == 0 || fmt == 0)
        return 33;
    if (wave_out_open)
        return 17;                      /* MMSYSERR_ALLOCATED */
    wave_out_open = 1;
    wo_ch = rd16(f + 2);
    wo_rate = rd32(f + 4);
    wo_bits = rd16(f + 14);
    if (wo_ch != 1 && wo_ch != 2)
        wo_ch = 2;
    if (wo_bits != 8 && wo_bits != 16)
        wo_bits = 16;
    *(uint32_t *)phwo = (uint32_t)0xAC970001u;
    trace("[winmm] waveOutOpen ok rate=");
    trace_num(wo_rate);
    trace(" ch=");
    trace_num(wo_ch);
    trace(" bits=");
    trace_num(wo_bits);
    trace("\n");
    return 0;
}

uint32_t __attribute__((stdcall)) waveOutClose(uint32_t hwo)
{
    (void)hwo;
    wave_out_open = 0;
    trace("[winmm] waveOutClose\n");
    return 0;
}

uint32_t __attribute__((stdcall)) waveOutReset(uint32_t hwo)
{
    (void)hwo;
    return 0;
}

/* waveOutWrite: API real = WAVEHDR (lpData en +0, dwBufferLength en
 * +4); reproduce con el formato del waveOutOpen. BLOQUEA. */
uint32_t __attribute__((stdcall)) waveOutWrite(uint32_t hwo, const void *hdr,
                        uint32_t cbwh)
{
    const uint8_t *h = (const uint8_t *)hdr;
    wav_info_t wi;
    int r;
    (void)hwo;
    if (hdr == 0 || cbwh < 8)
        return 33;
    wi.rate = wo_rate ? wo_rate : 48000;
    wi.channels = wo_ch;
    wi.bits = wo_bits;
    wi.data = (const uint8_t *)*(uint32_t *)(void *)(h + 0);
    wi.datalen = *(uint32_t *)(void *)(h + 4);
    if (wi.data == 0 || wi.datalen == 0)
        return 33;
    r = wav_play(&wi);
    return (uint32_t)(r == 0 ? 0 : 33);
}

uint32_t __attribute__((stdcall)) waveOutPrepareHeader(uint32_t hwo, void *h,
                        uint32_t sz)
{
    (void)hwo; (void)h; (void)sz;
    return 0;
}

uint32_t __attribute__((stdcall)) waveOutUnprepareHeader(uint32_t hwo, void *h,
                        uint32_t sz)
{
    (void)hwo; (void)h; (void)sz;
    return 0;
}

/* mciSendStringA: minimo "open <f> type waveaudio alias X",
 * "play X" (o "play <f>"), "close X". */
uint32_t __attribute__((stdcall)) mciSendStringA(const char *cmd, char *ret,
                        uint32_t retsz, void *cb)
{
    static char alias[32];
    static char file[64];
    const char *p;
    uint32_t i, r = 0;
    (void)cb;

    if (cmd == 0)
        return 512;                     /* MCIERR_UNRECOGNIZED_KEYWORD */
    if (ret != 0 && retsz > 0)
        ret[0] = 0;

    if (strlen_u(cmd) >= 4 && cmd[0] == 'o' && cmd[1] == 'p' &&
        cmd[2] == 'e' && cmd[3] == 'n') {
        /* open <file> ... alias <X> */
        p = cmd + 4;
        while (*p == ' ' || *p == '\t') p++;
        for (i = 0; i < 63 && *p && *p != ' ' && *p != '\t'; i++)
            file[i] = *p++;
        file[i] = 0;
        for (i = 0; i < 31; i++)
            alias[i] = 0;
        while (*p && !(*p == 'a' && p[1] == 'l' && p[2] == 'i')) p++;
        if (*p) {
            p += 5;
            while (*p == ' ' || *p == '\t') p++;
            for (i = 0; i < 31 && *p && *p != ' ' && *p != '\t'; i++)
                alias[i] = *p++;
            alias[i] = 0;
        }
        trace("[winmm] mci open '");
        trace(file);
        trace("'\n");
        return 0;
    }
    if (strlen_u(cmd) >= 4 && cmd[0] == 'p' && cmd[1] == 'l' &&
        cmd[2] == 'a' && cmd[3] == 'y') {
        p = cmd + 4;
        while (*p == ' ' || *p == '\t') p++;
        for (i = 0; i < 63 && *p && *p != ' ' && *p != '\t'; i++)
            file[i] = *p++;
        file[i] = 0;
        r = play_file(file);
        if (r != 0)
            return 512;
        return 0;
    }
    if (strlen_u(cmd) >= 5 && cmd[0] == 'c' && cmd[1] == 'l' &&
        cmd[2] == 'o' && cmd[3] == 's') {
        return 0;
    }
    trace("[winmm] mciSendStringA (sin parsear): ");
    trace(cmd);
    trace("\n");
    return 512;
}

uint32_t __attribute__((stdcall)) timeGetTime(void)
{
    return sys_ticks() * 10;
}

uint32_t __attribute__((stdcall)) timeBeginPeriod(uint32_t p) { (void)p; return 0; }
uint32_t __attribute__((stdcall)) timeEndPeriod(uint32_t p) { (void)p; return 0; }

typedef struct {
    char     name[32];
    uint32_t fn;
} win32_export_t;

win32_export_t __exports[] __attribute__((section(".exports"))) = {
    { "PlaySoundA",          (uint32_t)&PlaySoundA },
    { "waveOutOpen",         (uint32_t)&waveOutOpen },
    { "waveOutClose",        (uint32_t)&waveOutClose },
    { "waveOutWrite",        (uint32_t)&waveOutWrite },
    { "waveOutReset",        (uint32_t)&waveOutReset },
    { "waveOutGetDevCaps",   (uint32_t)&waveOutGetDevCaps },
    { "waveOutPrepareHeader", (uint32_t)&waveOutPrepareHeader },
    { "waveOutUnprepareHeader", (uint32_t)&waveOutUnprepareHeader },
    { "mciSendStringA",      (uint32_t)&mciSendStringA },
    { "timeGetTime",         (uint32_t)&timeGetTime },
    { "timeBeginPeriod",     (uint32_t)&timeBeginPeriod },
    { "timeEndPeriod",       (uint32_t)&timeEndPeriod },
    { "", 0 },
};