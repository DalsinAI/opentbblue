#ifndef ZX_NET_POSIX_H
#define ZX_NET_POSIX_H

#include "../devices/net.h"

zx_net *zx_net_posix_new(void);
void zx_net_posix_free(zx_net *net);

#if defined(__AMIGA__) || defined(__amigaos__)
/* bsdsocket.library, shared by the Next's wifi and the downloads. */
int otb_amiga_socket_open(void);
void otb_amiga_socket_close(void);
#endif

#endif
