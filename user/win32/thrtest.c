/* MyOS - user/win32/thrtest.c
 * Fase 24-P2.2: CreateThread con scheduler preemptivo. El hilo
 * incrementa un contador global (compartido con el proceso) N veces y
 * termina con ExitThread. El hilo principal hace busy-wait hasta que el
 * contador llega a N: esto solo funciona si el timer preempta al hilo
 * principal y programa el hilo (multitarea real). exit 0 si ok. */
#include <windows.h>
#include <stdint.h>

static volatile LONG g_counter = 0;
static volatile LONG g_ran = 0;

static void logmsg(const char *s)
{
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD n = 0;
    int len = 0;
    while (s[len]) len++;
    WriteFile(h, s, len, &n, 0);
}

static void lognum(int v)
{
    char b[12];
    int p = 0;
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (v < 0) { char c = '-'; WriteFile(h, &c, 1, &(DWORD){0}, 0); v = -v; }
    do { b[p++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (p > 0) WriteFile(h, &b[--p], 1, &(DWORD){0}, 0);
}

static void sys_exit(int code)
{
    __asm__ volatile("int $0x80" : : "a"(2), "b"(code) : "memory");
}

static DWORD WINAPI thread_fn(LPVOID p)
{
    int target = (int)(intptr_t)p;
    int i;
    g_ran = 1;
    for (i = 0; i < target; i++)
        g_counter++;
    logmsg("thr: hilo termina, counter=");
    lognum((int)g_counter);
    logmsg("\n");
    ExitThread(0);
    return 0;
}

int main(void)
{
    DWORD tid = 0;
    HANDLE h;
    int ok = 1;

    logmsg("thr: start\n");
    h = CreateThread(0, 0, thread_fn, (LPVOID)1000000, 0, &tid);
    logmsg("thr: CreateThread handle=");
    lognum((int)(intptr_t)h);
    logmsg(" tid=");
    lognum((int)tid);
    logmsg("\n");
    if (h == 0) { logmsg("thr: CreateThread fallo\n"); sys_exit(1); }

    /* busy-wait: solo retorna si el hilo (preemptido por el timer)
     * corre y llega al contador objetivo */
    while (g_counter < 1000000) { }

    logmsg("thr: main ve counter=");
    lognum((int)g_counter);
    logmsg(" ran=");
    lognum((int)g_ran);
    logmsg("\n");
    ok = ok && (g_counter == 1000000) && (g_ran == 1);

    logmsg("thr: fin ok=");
    lognum(ok);
    logmsg("\n");
    sys_exit(ok ? 0 : 1);
    return 0;
}