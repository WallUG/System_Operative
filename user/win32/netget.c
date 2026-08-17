/* MyOS - user/win32/netget.c
 * Fase 25-W2A paso 6: cliente HTTP via ws2_32.dll sobre el stack
 * UDP/TCP del kernel.
 * 1) UDP/DNS: gethostbyname("10.0.2.2") (literal) y una query DNS
 *    real por UDP a 10.0.2.3:53 (gethostbyname("example.com")).
 * 2) TCP: socket/connect a 10.0.2.2:8080, GET /netget.txt HTTP/1.0,
 *    recv hasta FIN, separar el cuerpo y verificar checksum.
 * Imprime "netget:PASS len=<n> cksum=<hex8>" — el test del host
 * compara el checksum con el archivo servido. */

#include <winsock2.h>
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

static void loghex(unsigned int v)
{
    static const char hx[] = "0123456789abcdef";
    char b[8];
    int i;
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    for (i = 7; i >= 0; i--) {
        b[i] = hx[v & 15];
        v >>= 4;
    }
    WriteFile(h, b, 8, &(DWORD){0}, 0);
}

static unsigned int body_cksum(const char *body, int len)
{
    unsigned int c = 0;
    int i;
    for (i = 0; i < len; i++)
        c = c * 33 + (unsigned char)body[i];
    return c;
}

int main(void)
{
    WSADATA wd;
    SOCKET s;
    struct sockaddr_in sa;
    struct hostent *he;
    char buf[4096];
    int n, total = 0;
    const char *req = "GET /netget.txt HTTP/1.0\r\n\r\n";
    const char *body;
    int bodylen;
    unsigned int cksum;
    const char *p;
    int status_ok = 0;

    logstr("netget: start\n");

    if (WSAStartup(0x0101, &wd) != 0) {
        logstr("netget: FAIL WSAStartup\n");
        return 1;
    }
    logstr("netget: WSAStartup ok\n");

    /* 1) IP literal */
    he = gethostbyname("10.0.2.2");
    if (he == 0 || he->h_addrtype != AF_INET) {
        logstr("netget: FAIL gethostbyname literal\n");
        return 1;
    }
    logstr("netget: gethostbyname literal ok\n");

    /* 1b) DNS real por UDP */
    he = gethostbyname("example.com");
    if (he == 0 || he->h_addrtype != AF_INET) {
        logstr("netget: FAIL DNS\n");
        return 1;
    }
    logstr("netget: DNS ok -> ");
    {
        unsigned char *a = (unsigned char *)he->h_addr_list[0];
        lognum(a[0]); logstr("."); lognum(a[1]); logstr(".");
        lognum(a[2]); logstr("."); lognum(a[3]); logstr("\n");
    }

    /* 2) TCP + HTTP */
    s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == INVALID_SOCKET) {
        logstr("netget: FAIL socket\n");
        return 1;
    }
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(8080);
    sa.sin_addr.s_addr = inet_addr("10.0.2.2");
    if (connect(s, (struct sockaddr *)&sa, sizeof(sa)) != 0) {
        logstr("netget: FAIL connect\n");
        return 1;
    }
    logstr("netget: connect ok\n");

    if (send(s, req, (int)strlen(req), 0) != (int)strlen(req)) {
        logstr("netget: FAIL send\n");
        return 1;
    }
    logstr("netget: GET enviado\n");

    /* recv hasta FIN: juntar la respuesta (cabe en 64 KB). Si un
     * recv agota su timeout (-1) no es un fallo inmediato: el peer
     * puede estar retransmitiendo (el stack es sincrono); se
     * reintenta un par de veces antes de rendirse. */
    {
        char big[65536];
        int bl = 0;
        int misses = 0;
        while (misses < 12) {
            n = recv(s, big + bl, sizeof(big) - bl, 0);
            if (n > 0) {
                bl += n;
                misses = 0;
            } else if (n == 0) {
                break;              /* FIN */
            } else {
                misses++;           /* timeout: reintentar */
            }
        }
        if (n < 0) {
            logstr("netget: FAIL recv timeout\n");
            return 1;
        }
        total = bl;
        /* separar header HTTP del cuerpo */
        p = big;
        while (p < big + bl && *p != '\r' && *p != '\n') {
            if (memcmp(p, "HTTP/1.", 7) == 0 && p + 12 <= big + bl)
                status_ok = 1;
            while (p < big + bl && *p != '\r' && *p != '\n') p++;
            if (p < big + bl && *p == '\r') p++;
            if (p < big + bl && *p == '\n') p++;
        }
        if (p < big + bl && *p == '\r') p++;
        if (p < big + bl && *p == '\n') p++;
        body = p;
        bodylen = (int)(big + bl - p);
        cksum = body_cksum(body, bodylen);
        logstr("netget: HTTP status ok, body ");
        lognum((unsigned int)bodylen);
        logstr(" B\n");
        if (!status_ok || bodylen < 8192) {
            logstr("netget: FAIL respuesta HTTP incompleta\n");
            return 1;
        }
    }

    closesocket(s);
    WSACleanup();

    logstr("netget:PASS len=");
    lognum((unsigned int)total);
    logstr(" cksum=");
    loghex(cksum);
    logstr("\n");
    return 0;
}