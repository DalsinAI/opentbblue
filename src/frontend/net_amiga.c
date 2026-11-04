/*
 * zx_net over bsdsocket.library, for the AmigaOS build (the same calls as
 * net_posix.c). Any TCP/IP stack works (Roadshow, AmiTCP, Miami, or
 * OpenSocket). With no stack running the Next's wifi simply
 * finds no network. Connections are non-blocking; name lookups block.
 */
#include "net_posix.h"

#include <exec/types.h>
#include <proto/exec.h>
#include <proto/socket.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netdb.h>
#include <amitcp/socketbasetags.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_CONN 16

struct Library *SocketBase;
static int socket_users;

/* bsdsocket.library for the Next's wifi and for OpenTBBlue's own downloads
 * (fetch.c), opened once for both: 0 when it is open. Its errno is the C
 * library's, which libcurl reads. */
int otb_amiga_socket_open(void)
{
    if (!SocketBase) {
        if (!(SocketBase = OpenLibrary((CONST_STRPTR)"bsdsocket.library", 4)))
            return -1;  /* no TCP/IP stack running */
        SocketBaseTags(SBTM_SETVAL(SBTC_ERRNOPTR(sizeof(errno))), (ULONG)&errno,
                       SBTM_SETVAL(SBTC_LOGTAGPTR), (ULONG)"OpenTBBlue", TAG_DONE);
    }
    socket_users++;
    return 0;
}

void otb_amiga_socket_close(void)
{
    if (socket_users > 0 && --socket_users == 0 && SocketBase) {
        CloseLibrary(SocketBase);
        SocketBase = NULL;
    }
}

typedef struct anet {
    zx_net net;
    LONG fd[MAX_CONN];
    int state[MAX_CONN];
} anet;

static int lookup(const char *host, struct in_addr *out)
{
    struct hostent *he;
    ULONG a = inet_addr((STRPTR)host);
    if (a != (ULONG)-1) {
        out->s_addr = a;
        return 0;
    }
    he = gethostbyname((STRPTR)host);
    if (!he || he->h_addrtype != AF_INET || !he->h_addr_list[0])
        return -1;
    memcpy(out, he->h_addr_list[0], sizeof *out);
    return 0;
}

static int a_open(zx_net *net, int udp, const char *host, int port)
{
    anet *p = (anet *)net;
    struct sockaddr_in sa;
    LONG fd, one = 1;
    int h;
    for (h = 0; h < MAX_CONN && p->fd[h] >= 0; h++)
        ;
    if (h == MAX_CONN)
        return -1;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons((unsigned short)port);
    if (lookup(host, &sa.sin_addr))
        return -1;
    fd = socket(AF_INET, udp ? SOCK_DGRAM : SOCK_STREAM, 0);
    if (fd < 0)
        return -1;
    IoctlSocket(fd, FIONBIO, (char *)&one);
    if (connect(fd, (struct sockaddr *)&sa, sizeof sa) < 0 && Errno() != EINPROGRESS) {
        CloseSocket(fd);
        return -1;
    }
    p->fd[h] = fd;
    p->state[h] = udp ? ZX_NET_OPEN : ZX_NET_CONNECTING;
    return h;
}

static int a_state(zx_net *net, int h)
{
    anet *p = (anet *)net;
    if (h < 0 || h >= MAX_CONN || p->fd[h] < 0)
        return ZX_NET_CLOSED;
    if (p->state[h] == ZX_NET_CONNECTING) {
        fd_set w;
        struct timeval tv;
        FD_ZERO(&w);
        FD_SET(p->fd[h], &w);
        tv.tv_sec = 0;
        tv.tv_usec = 0;
        if (WaitSelect(p->fd[h] + 1, NULL, &w, NULL, &tv, NULL) > 0) {
            LONG err = 0, len = sizeof err;
            getsockopt(p->fd[h], SOL_SOCKET, SO_ERROR, &err, &len);
            p->state[h] = err ? ZX_NET_CLOSED : ZX_NET_OPEN;
        }
    }
    return p->state[h];
}

static int a_send(zx_net *net, int h, const uint8_t *buf, int n)
{
    anet *p = (anet *)net;
    LONG w;
    if (a_state(net, h) != ZX_NET_OPEN)
        return -1;
    w = send(p->fd[h], (APTR)buf, n, 0);
    if (w < 0)
        return Errno() == EWOULDBLOCK ? 0 : -1;
    return (int)w;
}

static int a_recv(zx_net *net, int h, uint8_t *buf, int max)
{
    anet *p = (anet *)net;
    LONG r;
    int st = a_state(net, h);
    if (st == ZX_NET_CONNECTING)
        return 0;
    if (st != ZX_NET_OPEN)
        return -1;
    r = recv(p->fd[h], buf, max, 0);
    if (r < 0)
        return Errno() == EWOULDBLOCK ? 0 : -1;
    if (r == 0) {
        p->state[h] = ZX_NET_CLOSED;
        return -1;
    }
    return (int)r;
}

static void a_close(zx_net *net, int h)
{
    anet *p = (anet *)net;
    if (h < 0 || h >= MAX_CONN || p->fd[h] < 0)
        return;
    CloseSocket(p->fd[h]);
    p->fd[h] = -1;
}

static int a_resolve(zx_net *net, const char *host, char ip[16])
{
    struct in_addr a;
    (void)net;
    if (lookup(host, &a))
        return -1;
    snprintf(ip, 16, "%s", (const char *)Inet_NtoA(a.s_addr));
    return 0;
}

zx_net *zx_net_posix_new(void)
{
    anet *p;
    int i;
    if (otb_amiga_socket_open())
        return NULL;    /* no TCP/IP stack: the Next sees no network */
    p = calloc(1, sizeof *p);
    if (!p) {
        otb_amiga_socket_close();
        return NULL;
    }
    for (i = 0; i < MAX_CONN; i++)
        p->fd[i] = -1;
    p->net.open = a_open;
    p->net.state = a_state;
    p->net.send = a_send;
    p->net.recv = a_recv;
    p->net.close = a_close;
    p->net.resolve = a_resolve;
    return &p->net;
}

void zx_net_posix_free(zx_net *net)
{
    anet *p = (anet *)net;
    int i;
    if (p) {
        for (i = 0; i < MAX_CONN; i++)
            a_close(net, i);
        free(p);
        otb_amiga_socket_close();
    }
}
