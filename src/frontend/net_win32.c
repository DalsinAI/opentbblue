/*
 * zx_net over Winsock, for the Windows build (the same calls as
 * net_posix.c). Connections are non-blocking; name lookups block.
 */
#include "net_posix.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_CONN 16

typedef struct wnet {
    zx_net net;
    SOCKET fd[MAX_CONN];
    int state[MAX_CONN];
} wnet;

static int w_open(zx_net *net, int udp, const char *host, int port)
{
    wnet *p = (wnet *)net;
    struct addrinfo hints, *res;
    char ps[16];
    u_long nb = 1;
    int h;
    SOCKET fd;
    for (h = 0; h < MAX_CONN && p->fd[h] != INVALID_SOCKET; h++)
        ;
    if (h == MAX_CONN)
        return -1;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = udp ? SOCK_DGRAM : SOCK_STREAM;
    sprintf(ps, "%d", port);
    if (getaddrinfo(host, ps, &hints, &res))
        return -1;
    fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd == INVALID_SOCKET) {
        freeaddrinfo(res);
        return -1;
    }
    ioctlsocket(fd, FIONBIO, &nb);
    if (connect(fd, res->ai_addr, (int)res->ai_addrlen) && WSAGetLastError() != WSAEWOULDBLOCK) {
        freeaddrinfo(res);
        closesocket(fd);
        return -1;
    }
    freeaddrinfo(res);
    p->fd[h] = fd;
    p->state[h] = udp ? ZX_NET_OPEN : ZX_NET_CONNECTING;
    return h;
}

static int w_state(zx_net *net, int h)
{
    wnet *p = (wnet *)net;
    if (h < 0 || h >= MAX_CONN || p->fd[h] == INVALID_SOCKET)
        return ZX_NET_CLOSED;
    if (p->state[h] == ZX_NET_CONNECTING) {
        WSAPOLLFD pf;
        pf.fd = p->fd[h];
        pf.events = POLLOUT;
        pf.revents = 0;
        if (WSAPoll(&pf, 1, 0) > 0) {
            int err = 0, len = sizeof err;
            getsockopt(p->fd[h], SOL_SOCKET, SO_ERROR, (char *)&err, &len);
            p->state[h] = (err || (pf.revents & (POLLERR | POLLHUP))) ? ZX_NET_CLOSED : ZX_NET_OPEN;
        }
    }
    return p->state[h];
}

static int w_send(zx_net *net, int h, const uint8_t *buf, int n)
{
    wnet *p = (wnet *)net;
    int w;
    if (w_state(net, h) != ZX_NET_OPEN)
        return -1;
    w = send(p->fd[h], (const char *)buf, n, 0);
    if (w == SOCKET_ERROR)
        return WSAGetLastError() == WSAEWOULDBLOCK ? 0 : -1;
    return w;
}

static int w_recv(zx_net *net, int h, uint8_t *buf, int max)
{
    wnet *p = (wnet *)net;
    int r, st = w_state(net, h);
    if (st == ZX_NET_CONNECTING)
        return 0;
    if (st != ZX_NET_OPEN)
        return -1;
    r = recv(p->fd[h], (char *)buf, max, 0);
    if (r == SOCKET_ERROR)
        return WSAGetLastError() == WSAEWOULDBLOCK ? 0 : -1;
    if (r == 0) {
        p->state[h] = ZX_NET_CLOSED;
        return -1;
    }
    return r;
}

static void w_close(zx_net *net, int h)
{
    wnet *p = (wnet *)net;
    if (h < 0 || h >= MAX_CONN || p->fd[h] == INVALID_SOCKET)
        return;
    closesocket(p->fd[h]);
    p->fd[h] = INVALID_SOCKET;
}

static int w_resolve(zx_net *net, const char *host, char ip[16])
{
    struct addrinfo hints, *res;
    (void)net;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    if (getaddrinfo(host, NULL, &hints, &res))
        return -1;
    inet_ntop(AF_INET, &((struct sockaddr_in *)res->ai_addr)->sin_addr, ip, 16);
    freeaddrinfo(res);
    return 0;
}

zx_net *zx_net_posix_new(void)
{
    WSADATA wsa;
    wnet *p;
    int i;
    if (WSAStartup(MAKEWORD(2, 2), &wsa))
        return NULL;
    p = calloc(1, sizeof *p);
    if (!p)
        return NULL;
    for (i = 0; i < MAX_CONN; i++)
        p->fd[i] = INVALID_SOCKET;
    p->net.open = w_open;
    p->net.state = w_state;
    p->net.send = w_send;
    p->net.recv = w_recv;
    p->net.close = w_close;
    p->net.resolve = w_resolve;
    return &p->net;
}

void zx_net_posix_free(zx_net *net)
{
    wnet *p = (wnet *)net;
    int i;
    if (!p)
        return;
    for (i = 0; i < MAX_CONN; i++)
        w_close(net, i);
    free(p);
    WSACleanup();
}
