/*
 * The curl command for AmigaOS 3.2 (68040): bsdsocket.library for the
 * command itself. libcurl built with GFOpenTLS leaves opening the network
 * to the program (gfopencurl's README), so this opens it before main()
 * and points bsdsocket's errno at the C library's.
 * Copyright (c) 2026 AmigaChrome Team. MIT licence.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <proto/exec.h>
#include <proto/bsdsocket.h>
#include <amitcp/socketbasetags.h>

struct Library *SocketBase;
/* A GFOpenTLS handshake needs 16 KB of stack deep inside curl. */
unsigned long __stack = 65536;
static const char ver[] __attribute__((used)) = "$VER: curl 8.22.0 (04.11.2026) GFOpenTLS";

static void net_close(void)
{
    if (SocketBase) {
        CloseLibrary(SocketBase);
        SocketBase = NULL;
    }
}

__attribute__((constructor)) static void net_open(void)
{
    SocketBase = OpenLibrary((CONST_STRPTR)"bsdsocket.library", 4);
    if (!SocketBase) {
        fprintf(stderr, "curl: no bsdsocket.library (is the network up?)\n");
        exit(20);
    }
    SocketBaseTags(SBTM_SETVAL(SBTC_ERRNOPTR(sizeof(errno))), (ULONG)&errno,
                   SBTM_SETVAL(SBTC_LOGTAGPTR), (ULONG)"curl", TAG_DONE);
    atexit(net_close);
}
