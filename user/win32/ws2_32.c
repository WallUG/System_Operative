/* MyOS - user/win32/ws2_32.c
 * ws2_32.dll (Fase 25-W2A paso 6): Winsock 1.1 sobre el stack del
 * kernel (SYS_NET_* 47-53). Cliente TCP + UDP: WSAStartup, socket,
 * connect, send, recv, closesocket, shutdown, gethostbyname (IP
 * literal o DNS por UDP a 10.0.2.3), inet_addr/ntohs..., select
 * (no-op), getaddrinfo/freeaddrinfo. Una sola conexion TCP a la vez
 * (el kernel la tiene); socket() devuelve 0x51 mientras haya una
 * conexion abierta. */

#include <stdint.h>

#define SYS_MALLOC  10
#define SYS_FREE    11
#define SYS_NET_ARP 47
#define SYS_NET_TCP_CONNECT 48
#define SYS_NET_TCP_SEND 49
#define SYS_NET_TCP_RECV 50
#define SYS_NET_TCP_CLOSE 51
#define SYS_NET_UDP_SEND 52
#define SYS_NET_UDP_RECV 53

#define DNS_SERVER_IP 10, 0, 2, 3

/* --- syscalls --- */

static long sys6(uint32_t n, uint32_t a, uint32_t b, uint32_t c,
                 uint32_t d, uint32_t e)
{
    long r;
    __asm__ volatile("int $0x80"
                     : "=a"(r)
                     : "a"(n), "b"(a), "c"(b), "d"(c), "S"(d), "D"(e)
                     : "memory");
    return r;
}

static void *w32_malloc(uint32_t size)
{
    return (void *)sys6(SYS_MALLOC, size, 0, 0, 0, 0);
}

static void w32_free(void *p)
{
    sys6(SYS_FREE, (uint32_t)p, 0, 0, 0, 0);
}

/* --- util --- */

static void put16(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static uint32_t get16(const uint8_t *p)
{
    return ((uint32_t)p[0] << 8) | p[1];
}

/* --- DNS minimo (un A record) --- */

typedef struct {
    uint32_t addr;          /* 0 si no hay A */
} dns_result_t;

static int dns_name_to_wire(const char *name, uint8_t *out, int max)
{
    int o = 0;
    while (*name) {
        int l = 0;
        while (name[l] && name[l] != '.')
            l++;
        if (o + 1 + l + 1 > max || l == 0 || l > 63)
            return -1;
        out[o++] = (uint8_t)l;
        {
            int i;
            for (i = 0; i < l; i++)
                out[o++] = (uint8_t)name[i];
        }
        name += l;
        if (*name == '.')
            name++;
    }
    if (o + 1 > max)
        return -1;
    out[o++] = 0;
    return o;
}

/* Query DNS por UDP (10.0.2.3:53) y extrae la primera A. 0 = ok. */
static int dns_query(const char *name, dns_result_t *out)
{
    static const uint8_t dns_ip[4] = { DNS_SERVER_IP };
    uint8_t *q = (uint8_t *)w32_malloc(512);
    uint8_t *r = (uint8_t *)w32_malloc(512);
    uint16_t id = (uint16_t)((uint32_t)name[0] * 257 + 0x1234);
    int nlen, qlen, n, pos, ancount, i;
    const uint32_t QRY_PORT = 53000;

    if (q == 0 || r == 0) {
        if (q) w32_free(q);
        if (r) w32_free(r);
        return -1;
    }
    nlen = dns_name_to_wire(name, q + 12, 400);
    if (nlen < 0) {
        w32_free(q);
        w32_free(r);
        return -1;
    }
    q[0] = (uint8_t)(id >> 8);
    q[1] = (uint8_t)id;
    q[2] = 0x01;            /* RD */
    q[3] = 0;
    q[4] = 0;
    q[5] = 1;               /* QDCOUNT 1 */
    put16(q + 12 + nlen, 1);        /* QTYPE A */
    put16(q + 12 + nlen + 2, 1);    /* QCLASS IN */
    qlen = 12 + nlen + 4;
    if (sys6(SYS_NET_UDP_SEND, (uint32_t)dns_ip, QRY_PORT, 53,
             (uint32_t)q, (uint32_t)qlen) != 0) {
        w32_free(q);
        w32_free(r);
        return -1;
    }
    w32_free(q);
    n = (int)sys6(SYS_NET_UDP_RECV, QRY_PORT, (uint32_t)r, 512, 3000, 0);
    if (n < 12) {
        w32_free(r);
        return -1;
    }
    if (((uint32_t)((r[0] << 8) | r[1])) != (uint32_t)id) {
        w32_free(r);
        return -1;
    }
    /* saltar la pregunta: nombre + 4 B */
    pos = 12;
    while (pos < n && r[pos] != 0) {
        if (r[pos] & 0xC0) {        /* compresion en la pregunta: raro */
            pos += 2;
            break;
        }
        pos += 1 + r[pos];
    }
    pos += 1 + 4;                   /* 0 terminal + type/class */
    ancount = (r[6] << 8) | r[7];
    out->addr = 0;
    for (i = 0; i < ancount && pos + 10 <= n; i++) {
        uint32_t rdlen;
        if (r[pos] & 0xC0) {
            pos += 2;               /* puntero */
        } else {
            while (pos < n && r[pos] != 0) {
                if (r[pos] & 0xC0) {
                    pos += 2;
                    break;
                }
                pos += 1 + r[pos];
            }
            pos++;
        }
        if (pos + 10 > n)
            break;
        pos += 2;                   /* type */
        pos += 2;                   /* class */
        pos += 4;                   /* ttl */
        rdlen = get16(r + pos);
        pos += 2;
        if (pos + (int)rdlen > n)
            break;
        if (rdlen == 4 && get16(r + pos - 8 - 2) == 1) {
            /* type A: rdata = ipv4 */
            out->addr = ((uint32_t)r[pos] << 24) | ((uint32_t)r[pos + 1] << 16)
                        | ((uint32_t)r[pos + 2] << 8) | r[pos + 3];
            w32_free(r);
            return 0;
        }
        pos += (int)rdlen;
    }
    w32_free(r);
    return 0;
}

/* --- hostent --- */

/* Layout REAL de Windows: h_addrtype/h_length son SHORT (el app lee
 * h_addr_list en el offset 12; con ints quedaba en 16 y el caller
 * dereferenciaba basura). */
typedef struct {
    char     *h_name;
    char    **h_aliases;
    short     h_addrtype;
    short     h_length;
    char    **h_addr_list;
} my_hostent;

static char hostent_name[64];
static char *hostent_aliases[2];
static char *hostent_list[2];
static char hostent_addr[4];
static my_hostent hostent;

/* "x.x.x.x" o nombre DNS. Devuelve hostent o 0. */
my_hostent *gethostbyname(const char *name)
{
    uint32_t a[4];
    int i, nparts = 0;
    dns_result_t dr;

    if (name == 0)
        return 0;
    for (i = 0; name[i] && i < 63; i++)
        hostent_name[i] = name[i];
    hostent_name[i] = 0;

    /* intentar IP literal */
    nparts = 0;
    a[0] = a[1] = a[2] = a[3] = 0;
    {
        const char *p = name;
        while (*p && nparts < 4) {
            uint32_t v = 0;
            while (*p >= '0' && *p <= '9') {
                v = v * 10 + (uint32_t)(*p - '0');
                p++;
            }
            if (v > 255)
                break;
            a[nparts++] = v;
            if (*p == '.')
                p++;
            else
                break;
        }
        if (*p == 0 && nparts == 4) {
            hostent_addr[0] = (char)a[0];
            hostent_addr[1] = (char)a[1];
            hostent_addr[2] = (char)a[2];
            hostent_addr[3] = (char)a[3];
            hostent_aliases[0] = 0;
            hostent_list[0] = hostent_addr;
            hostent_list[1] = 0;
            hostent.h_name = hostent_name;
            hostent.h_aliases = hostent_aliases;
            hostent.h_addrtype = 2;         /* AF_INET */
            hostent.h_length = 4;
            hostent.h_addr_list = hostent_list;
            return &hostent;
        }
    }
    /* DNS */
    if (dns_query(name, &dr) != 0 || dr.addr == 0)
        return 0;
    hostent_addr[0] = (char)(dr.addr >> 24);
    hostent_addr[1] = (char)(dr.addr >> 16);
    hostent_addr[2] = (char)(dr.addr >> 8);
    hostent_addr[3] = (char)dr.addr;
    hostent_aliases[0] = 0;
    hostent_list[0] = hostent_addr;
    hostent_list[1] = 0;
    hostent.h_name = hostent_name;
    hostent.h_aliases = hostent_aliases;
    hostent.h_addrtype = 2;
    hostent.h_length = 4;
    hostent.h_addr_list = hostent_list;
    return &hostent;
}

/* --- exports --- */

static int ws_connected;        /* socket abierto */

uint32_t __attribute__((stdcall)) WSAStartup(uint16_t ver, void *data)
{
    (void)ver;
    if (data)
        *(uint16_t *)((uint8_t *)data + 0) = 0x0101;   /* wVersion */
    ws_connected = 0;
    return 0;
}

uint32_t __attribute__((stdcall)) WSACleanup(void)
{
    return 0;
}

uint32_t __attribute__((stdcall)) WSAGetLastError(void)
{
    return 10060;               /* WSAETIMEDOUT (recv timeout) */
}

void __attribute__((stdcall)) WSASetLastError(uint32_t e)
{
    (void)e;
}

uint32_t __attribute__((stdcall)) socket(int af, int type, int proto)
{
    (void)af; (void)type; (void)proto;
    if (ws_connected)
        return 0xFFFFFFFFu;
    ws_connected = 1;
    return 0x51u;
}

/* sockaddr_in: family(2), port(2, BE), addr(4, BE), zero(8) */
uint32_t __attribute__((stdcall)) connect(uint32_t s, const void *sa,
                                          int salen)
{
    const uint8_t *a = (const uint8_t *)sa;
    uint32_t ip;
    uint16_t port;
    (void)s;
    if (sa == 0 || salen < 8)
        return 0xFFFFFFFFu;
    (void)ip;
    port = (uint16_t)(((uint16_t)a[2] << 8) | a[3]);
    if (sys6(SYS_NET_TCP_CONNECT, (uint32_t)(a + 4), port, 0, 0, 0) != 0)
        return 0xFFFFFFFFu;
    return 0;
}

int __attribute__((stdcall)) send(uint32_t s, const void *buf, int len,
                                  int flags)
{
    (void)s; (void)flags;
    if (len <= 0)
        return 0;
    if (sys6(SYS_NET_TCP_SEND, (uint32_t)buf, (uint32_t)len, 0, 0, 0) != 0)
        return -1;
    return len;
}

int __attribute__((stdcall)) recv(uint32_t s, void *buf, int len, int flags)
{
    long n;
    (void)s; (void)flags;
    if (len <= 0)
        return 0;
    n = sys6(SYS_NET_TCP_RECV, (uint32_t)buf, (uint32_t)len, 10000, 0, 0);
    return (int)n;              /* >0 datos, 0 FIN, -1 timeout */
}

uint32_t __attribute__((stdcall)) closesocket(uint32_t s)
{
    (void)s;
    sys6(SYS_NET_TCP_CLOSE, 0, 0, 0, 0, 0);
    ws_connected = 0;
    return 0;
}

uint32_t __attribute__((stdcall)) shutdown(uint32_t s, int how)
{
    (void)s; (void)how;
    return 0;
}

uint32_t __attribute__((stdcall)) htonl(uint32_t v)
{
    return ((v & 0xFF) << 24) | ((v & 0xFF00) << 8)
           | ((v >> 8) & 0xFF00) | (v >> 24);
}

uint16_t __attribute__((stdcall)) htons(uint16_t v)
{
    return (uint16_t)((v << 8) | (v >> 8));
}

uint32_t __attribute__((stdcall)) ntohl(uint32_t v)
{
    return htonl(v);
}

uint16_t __attribute__((stdcall)) ntohs(uint16_t v)
{
    return htons(v);
}

uint32_t __attribute__((stdcall)) inet_addr(const char *cp)
{
    uint32_t a = 0, v = 0, parts = 0;
    const char *p = cp ? cp : "";
    while (*p && parts < 4) {
        if (*p >= '0' && *p <= '9') {
            v = v * 10 + (uint32_t)(*p - '0');
        } else if (*p == '.') {
            if (v > 255)
                return 0xFFFFFFFFu;
            a = (a << 8) | v;
            v = 0;
            parts++;
        } else {
            return 0xFFFFFFFFu;
        }
        p++;
    }
    if (parts != 3 || v > 255)
        return 0xFFFFFFFFu;
    a = (a << 8) | v;
    /* red en orden de red: la app la guarda RAW en sockaddr_in y el
     * connect lee los 4 bytes tal cual */
    return ((a & 0xFF) << 24) | ((a & 0xFF00) << 8)
           | ((a >> 8) & 0xFF00) | (a >> 24);
}

/* select: devuelve el socket si hay datos pendientes (no-op simple:
 * siempre "legible" — el recv bloquea con timeout de todas formas). */
uint32_t __attribute__((stdcall)) select(int nfds, void *rd, void *wr,
                                         void *ex, const void *tv)
{
    (void)nfds; (void)wr; (void)ex; (void)tv;
    if (rd) {
        uint8_t *fd = (uint8_t *)rd;
        uint32_t i;
        for (i = 0; i < (uint32_t)((nfds + 7) / 8); i++)
            fd[i] = 0xFF;
    }
    return 1;
}

uint32_t __attribute__((stdcall)) getsockopt(uint32_t s, int lvl, int opt,
                                             void *val, int *vlen)
{
    (void)s; (void)lvl; (void)opt;
    if (val && vlen && *vlen >= 4)
        *(uint32_t *)val = 0;
    return 0;
}

uint32_t __attribute__((stdcall)) setsockopt(uint32_t s, int lvl, int opt,
                                             const void *val, int vlen)
{
    (void)s; (void)lvl; (void)opt; (void)val; (void)vlen;
    return 0;
}

uint32_t __attribute__((stdcall)) getsockname(uint32_t s, void *sa, int *len)
{
    (void)s; (void)sa; (void)len;
    return 0xFFFFFFFFu;
}

uint32_t __attribute__((stdcall)) bind(uint32_t s, const void *sa, int len)
{
    (void)s; (void)sa; (void)len;
    return 0xFFFFFFFFu;         /* sin servidor */
}

uint32_t __attribute__((stdcall)) listen(uint32_t s, int backlog)
{
    (void)s; (void)backlog;
    return 0xFFFFFFFFu;
}

uint32_t __attribute__((stdcall)) accept(uint32_t s, void *sa, int *len)
{
    (void)s; (void)sa; (void)len;
    return 0xFFFFFFFFu;
}

typedef struct {
    uint32_t ai_flags;
    int32_t  ai_family;
    int32_t  ai_socktype;
    int32_t  ai_protocol;
    uint32_t ai_addrlen;
    char    *ai_canonname;
    void    *ai_addr;
    void    *ai_next;
} my_addrinfo;

static my_addrinfo addrinfo_node;
static char addrinfo_sa[16];

uint32_t __attribute__((stdcall)) getaddrinfo(const char *name,
                                              const char *service,
                                              const void *hints,
                                              void **res)
{
    my_hostent *he;
    const uint8_t *ip;
    (void)hints;
    if (res == 0)
        return 0xFFFFFFFFu;
    he = gethostbyname(name ? name : "");
    if (he == 0)
        return 0xFFFFFFFFu;
    ip = (const uint8_t *)he->h_addr_list[0];
    addrinfo_sa[0] = 0;
    addrinfo_sa[1] = 2;                 /* AF_INET */
    addrinfo_sa[2] = 0;
    addrinfo_sa[3] = 0;
    if (service) {
        uint32_t p = 0;
        while (*service >= '0' && *service <= '9')
            p = p * 10 + (uint32_t)(*service++ - '0');
        addrinfo_sa[2] = (char)(p >> 8);
        addrinfo_sa[3] = (char)p;
    }
    addrinfo_sa[4] = (char)ip[0];
    addrinfo_sa[5] = (char)ip[1];
    addrinfo_sa[6] = (char)ip[2];
    addrinfo_sa[7] = (char)ip[3];
    addrinfo_node.ai_flags = 0;
    addrinfo_node.ai_family = 2;
    addrinfo_node.ai_socktype = 1;      /* SOCK_STREAM */
    addrinfo_node.ai_protocol = 6;
    addrinfo_node.ai_addrlen = 16;
    addrinfo_node.ai_canonname = 0;
    addrinfo_node.ai_addr = addrinfo_sa;
    addrinfo_node.ai_next = 0;
    *res = &addrinfo_node;
    return 0;
}

void __attribute__((stdcall)) freeaddrinfo(void *res)
{
    (void)res;
}

uint32_t __attribute__((stdcall)) gethostname(char *name, int len)
{
    (void)name; (void)len;
    return 0xFFFFFFFFu;
}

uint32_t __attribute__((stdcall)) ioctlsocket(uint32_t s, long cmd, void *arg)
{
    (void)s; (void)cmd; (void)arg;
    return 0;
}

typedef struct {
    char     name[32];
    uint32_t fn;
} win32_export_t;

win32_export_t __exports[] __attribute__((section(".exports"))) = {
    { "WSAStartup",      (uint32_t)&WSAStartup },
    { "WSACleanup",      (uint32_t)&WSACleanup },
    { "WSAGetLastError", (uint32_t)&WSAGetLastError },
    { "WSASetLastError", (uint32_t)&WSASetLastError },
    { "socket",          (uint32_t)&socket },
    { "connect",         (uint32_t)&connect },
    { "send",            (uint32_t)&send },
    { "recv",            (uint32_t)&recv },
    { "closesocket",     (uint32_t)&closesocket },
    { "shutdown",        (uint32_t)&shutdown },
    { "gethostbyname",   (uint32_t)&gethostbyname },
    { "htonl",           (uint32_t)&htonl },
    { "htons",           (uint32_t)&htons },
    { "ntohl",           (uint32_t)&ntohl },
    { "ntohs",           (uint32_t)&ntohs },
    { "inet_addr",       (uint32_t)&inet_addr },
    { "select",          (uint32_t)&select },
    { "getsockopt",      (uint32_t)&getsockopt },
    { "setsockopt",      (uint32_t)&setsockopt },
    { "getsockname",     (uint32_t)&getsockname },
    { "bind",            (uint32_t)&bind },
    { "listen",          (uint32_t)&listen },
    { "accept",          (uint32_t)&accept },
    { "getaddrinfo",     (uint32_t)&getaddrinfo },
    { "freeaddrinfo",    (uint32_t)&freeaddrinfo },
    { "gethostname",     (uint32_t)&gethostname },
    { "ioctlsocket",     (uint32_t)&ioctlsocket },
    { "", 0 },
};