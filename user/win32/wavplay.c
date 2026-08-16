/* MyOS - user/win32/wavplay.c
 * Fase 25-W2A paso 5: prueba de winmm.dll sobre el AC'97.
 * 1) Genera tone.wav (seno 440 Hz, 16-bit mono 44100, 0.5 s) con
 *    APIs Win32 (CreateFileA/WriteFile).
 * 2) PlaySoundA(tone.wav, SND_FILENAME).
 * 3) waveOutOpen (44100 mono 16-bit) + waveOutWrite de un buffer de
 *    seno directo.
 * 4) mciSendStringA("play tone.wav").
 * El wav del host (-audiodev wav) debe tener muestras no-cero. */

#include <windows.h>
#include <string.h>

static void logstr(const char *s)
{
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD n = 0;
    int len = 0;
    while (s[len]) len++;
    WriteFile(h, s, len, &n, 0);
}

static void lognum(unsigned int v)
{
    char b[12];
    int p = 0, u = (int)v < 0 ? -(int)v : (int)v;
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if ((int)v < 0) { char c = '-'; WriteFile(h, &c, 1, &(DWORD){0}, 0); }
    do { b[p++] = (char)('0' + u % 10); u /= 10; } while (u);
    while (p > 0) WriteFile(h, &b[--p], 1, &(DWORD){0}, 0);
}

#define RATE 44100
#define NSAMP (RATE / 2)            /* 0.5 s */

static double sin2(unsigned int i);

static int gen_wav(const char *name)
{
    HANDLE h;
    DWORD wr = 0;
    static char data[44 + NSAMP * 2];
    unsigned int i, p = 0;
    unsigned int sz = NSAMP * 2;

    memset(data, 0, 44);
    data[0] = 'R'; data[1] = 'I'; data[2] = 'F'; data[3] = 'F';
    data[4] = (char)((44 + sz) & 0xFF);
    data[5] = (char)(((44 + sz) >> 8) & 0xFF);
    data[6] = (char)(((44 + sz) >> 16) & 0xFF);
    data[7] = (char)(((44 + sz) >> 24) & 0xFF);
    data[8] = 'W'; data[9] = 'A'; data[10] = 'V'; data[11] = 'E';
    data[12] = 'f'; data[13] = 'm'; data[14] = 't'; data[15] = ' ';
    data[16] = 16;                  /* fmt size */
    data[20] = 1;                   /* PCM */
    data[22] = 1;                   /* mono */
    data[24] = (char)(RATE & 0xFF);
    data[25] = (char)((RATE >> 8) & 0xFF);
    data[26] = (char)((RATE >> 16) & 0xFF);
    data[27] = (char)((RATE >> 24) & 0xFF);
    data[28] = (char)((RATE * 2) & 0xFF);
    data[29] = (char)(((RATE * 2) >> 8) & 0xFF);
    data[30] = (char)(((RATE * 2) >> 16) & 0xFF);
    data[31] = (char)(((RATE * 2) >> 24) & 0xFF);
    data[32] = 2;                   /* block align */
    data[34] = 16;                  /* bits */
    data[36] = 'd'; data[37] = 'a'; data[38] = 't'; data[39] = 'a';
    data[40] = (char)(sz & 0xFF);
    data[41] = (char)((sz >> 8) & 0xFF);
    data[42] = (char)((sz >> 16) & 0xFF);
    data[43] = (char)((sz >> 24) & 0xFF);
    p = 44;
    for (i = 0; i < NSAMP; i++) {
        short s = (short)(8000 * sin2(i));
        data[p++] = (char)s;
        data[p++] = (char)(s >> 8);
    }

    h = CreateFileA(name, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
    if (h == INVALID_HANDLE_VALUE) {
        logstr("wavplay: FAIL CreateFileA tone\n");
        return -1;
    }
    if (!WriteFile(h, data, 44 + sz, &wr, 0) || wr != 44 + sz) {
        logstr("wavplay: FAIL WriteFile tone\n");
        CloseHandle(h);
        return -1;
    }
    CloseHandle(h);
    logstr("wavplay: tone.wav generado (");
    lognum(44 + sz);
    logstr(" B)\n");
    return 0;
}

static double sin2(unsigned int i)
{
    double x = (double)i * 6.283185307179586 * 440.0 / RATE;
    int k;
    double s = 0, term = x;
    /* serie de Taylor de sin(x) */
    for (k = 1; k <= 9; k += 2) {
        if (k == 1)
            s = x;
        else {
            term = -term * x * x / (k * (k - 1));
            s += term;
        }
    }
    return s;
}

int main(void)
{
    WAVEFORMATEX fmt;
    HWAVEOUT hwo;
    MMRESULT r;
    static short buf[NSAMP];
    unsigned int i;

    logstr("wavplay: start\n");

    if (gen_wav("tone.wav") != 0)
        return 1;

    /* 2) PlaySoundA */
    logstr("wavplay: PlaySoundA...\n");
    if (!PlaySoundA("tone.wav", NULL, SND_FILENAME)) {
        logstr("wavplay: FAIL PlaySoundA\n");
        return 1;
    }
    logstr("wavplay: PlaySoundA ok\n");

    /* 3) waveOut */
    memset(&fmt, 0, sizeof(fmt));
    fmt.wFormatTag = 1;
    fmt.nChannels = 1;
    fmt.nSamplesPerSec = RATE;
    fmt.nAvgBytesPerSec = RATE * 2;
    fmt.nBlockAlign = 2;
    fmt.wBitsPerSample = 16;
    r = waveOutOpen(&hwo, WAVE_MAPPER, &fmt, 0, 0, 0);
    if (r != 0) {
        logstr("wavplay: FAIL waveOutOpen\n");
        return 1;
    }
    for (i = 0; i < NSAMP; i++)
        buf[i] = (short)(8000 * sin2(i));
    {
        WAVEHDR hdr;
        memset(&hdr, 0, sizeof(hdr));
        hdr.lpData = (LPSTR)buf;
        hdr.dwBufferLength = sizeof(buf);
        logstr("wavplay: waveOutWrite...\n");
        r = waveOutWrite(hwo, &hdr, sizeof(hdr));
    }
    if (r != 0) {
        logstr("wavplay: FAIL waveOutWrite\n");
        return 1;
    }
    logstr("wavplay: waveOutWrite ok\n");
    waveOutClose(hwo);

    /* 4) MCI */
    logstr("wavplay: mci play...\n");
    r = mciSendStringA("play tone.wav", 0, 0, 0);
    if (r != 0) {
        logstr("wavplay: FAIL mciSendStringA\n");
        return 1;
    }
    logstr("wavplay: mci ok\n");

    logstr("wavplay: t=");
    lognum(timeGetTime());
    logstr("\n");
    logstr("wavplay:PASS\n");
    return 0;
}