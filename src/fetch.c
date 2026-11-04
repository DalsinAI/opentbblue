/*
 * Downloads with libcurl (see fetch.h). On AmigaOS libcurl is DalsinAI/
 * gfopencurl's (HTTPS through gfopentls.library, its CA list in
 * S:GFOpenTLS), linked into the program; elsewhere it is the system's.
 */
#include "fetch.h"

#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#if defined(__AMIGA__) || defined(__amigaos__)
#include "frontend/net_posix.h"
#define OTB_AMIGA 1
#endif

#ifndef OTB_VERSION
#define OTB_VERSION "0.1.0"
#endif

#define TRIES 4

struct otb_dl {
    CURLM *multi;
    CURL *easy;
    FILE *fp;
    char url[1024], path[1400];
    long long from, got, total;
    int tries, resume, running, net;
    char err[CURL_ERROR_SIZE + 64];
};

static int inited;

static int global_init(void)
{
    if (!inited) {
        if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
            return -1;
        inited = 1;
    }
    return 0;
}

static size_t on_data(char *p, size_t size, size_t n, void *arg)
{
    otb_dl *d = arg;
    size_t len = size * n;
    if (fwrite(p, 1, len, d->fp) != len)
        return 0;   /* the disk is full: curl stops with a write error */
    d->got += (long long)len;
    return len;
}

static long long file_len(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0 ? (long long)st.st_size : 0;
}

/* (Re)start the transfer from what the file already holds (from 0 when
 * resume is off). */
static int begin(otb_dl *d)
{
    if (d->easy) {
        curl_multi_remove_handle(d->multi, d->easy);
        curl_easy_cleanup(d->easy);
        d->easy = NULL;
    }
    if (d->fp)
        fclose(d->fp);
    d->from = d->resume ? file_len(d->path) : 0;
    d->fp = fopen(d->path, d->from ? "ab" : "wb");
    if (!d->fp) {
        snprintf(d->err, sizeof d->err, "cannot write %s", d->path);
        return -1;
    }
    d->got = 0;
    d->err[0] = 0;
    d->easy = curl_easy_init();
    if (!d->easy) {
        snprintf(d->err, sizeof d->err, "no memory for the download");
        return -1;
    }
    curl_easy_setopt(d->easy, CURLOPT_URL, d->url);
    curl_easy_setopt(d->easy, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(d->easy, CURLOPT_MAXREDIRS, 8L);
    curl_easy_setopt(d->easy, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(d->easy, CURLOPT_USERAGENT, "OpenTBBlue/" OTB_VERSION);
    curl_easy_setopt(d->easy, CURLOPT_WRITEFUNCTION, on_data);
    curl_easy_setopt(d->easy, CURLOPT_WRITEDATA, d);
    curl_easy_setopt(d->easy, CURLOPT_ERRORBUFFER, d->err);
    curl_easy_setopt(d->easy, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(d->easy, CURLOPT_CONNECTTIMEOUT, 30L);
    /* a connection that has stalled (under 100 bytes a second for a
     * minute) is dropped and tried again from where it got to */
    curl_easy_setopt(d->easy, CURLOPT_LOW_SPEED_LIMIT, 100L);
    curl_easy_setopt(d->easy, CURLOPT_LOW_SPEED_TIME, 60L);
#ifdef _WIN32
    /* Windows' own certificate store, whichever TLS libcurl has */
    curl_easy_setopt(d->easy, CURLOPT_SSL_OPTIONS, (long)CURLSSLOPT_NATIVE_CA);
#endif
    if (d->from)
        curl_easy_setopt(d->easy, CURLOPT_RESUME_FROM_LARGE, (curl_off_t)d->from);
    if (curl_multi_add_handle(d->multi, d->easy) != CURLM_OK) {
        snprintf(d->err, sizeof d->err, "cannot start the download");
        return -1;
    }
    d->running = 1;
    return 0;
}

otb_dl *otb_dl_start(const char *url, const char *path, int resume)
{
    otb_dl *d;
    if (global_init())
        return NULL;
    d = calloc(1, sizeof *d);
    if (!d)
        return NULL;
#ifdef OTB_AMIGA
    if (otb_amiga_socket_open()) {
        free(d);
        return NULL;    /* no TCP/IP stack running */
    }
    d->net = 1;
#endif
    snprintf(d->url, sizeof d->url, "%s", url);
    snprintf(d->path, sizeof d->path, "%s", path);
    d->resume = resume;
    d->multi = curl_multi_init();
    if (!d->multi || begin(d)) {
        otb_dl_free(d);
        return NULL;
    }
    return d;
}

int otb_dl_step(otb_dl *d)
{
    int still = 0, left;
    CURLMsg *msg;
    if (!d)
        return -1;
    if (!d->running)
        return d->err[0] ? -1 : 1;
    if (curl_multi_perform(d->multi, &still) != CURLM_OK) {
        snprintf(d->err, sizeof d->err, "the download stopped");
        d->running = 0;
        return -1;
    }
    if (d->total <= 0) {
        curl_off_t cl = -1;
        if (curl_easy_getinfo(d->easy, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &cl) == CURLE_OK && cl > 0)
            d->total = d->from + (long long)cl;
    }
    while ((msg = curl_multi_info_read(d->multi, &left)) != NULL) {
        CURLcode rc;
        long code = 0;
        if (msg->msg != CURLMSG_DONE)
            continue;
        rc = msg->data.result;
        curl_easy_getinfo(d->easy, CURLINFO_RESPONSE_CODE, &code);
        if (rc == CURLE_OK) {
            d->running = 0;
            d->err[0] = 0;
            if (fclose(d->fp)) {
                d->fp = NULL;
                snprintf(d->err, sizeof d->err, "cannot write %s", d->path);
                return -1;
            }
            d->fp = NULL;
            if (d->total <= 0)
                d->total = d->from + d->got;
            return 1;
        }
        if (!d->err[0])
            snprintf(d->err, sizeof d->err, "%s", curl_easy_strerror(rc));
        /* Tried again, from where it got to; a server that cannot carry
         * on a part (or says the part is wrong) sends it all again. A
         * full disk, or a page that is not there, is not tried again. */
        if (++d->tries < TRIES && rc != CURLE_WRITE_ERROR &&
            !(code >= 400 && code < 500 && code != 408 && code != 416 && code != 429)) {
            d->resume = !(d->from && (rc == CURLE_RANGE_ERROR || code == 416));
            d->total = 0;
            if (begin(d) == 0)
                return 0;
        }
        d->running = 0;
        return -1;
    }
    return 0;
}

void otb_dl_progress(const otb_dl *d, long long *now, long long *total)
{
    if (now)
        *now = d ? d->from + d->got : 0;
    if (total)
        *total = d ? d->total : 0;
}

const char *otb_dl_error(const otb_dl *d)
{
    return d ? d->err : "";
}

void otb_dl_free(otb_dl *d)
{
    if (!d)
        return;
    if (d->easy) {
        if (d->multi)
            curl_multi_remove_handle(d->multi, d->easy);
        curl_easy_cleanup(d->easy);
    }
    if (d->multi)
        curl_multi_cleanup(d->multi);
    if (d->fp)
        fclose(d->fp);
#ifdef OTB_AMIGA
    if (d->net)
        otb_amiga_socket_close();
#endif
    free(d);
}

int otb_dl_wait(const char *url, const char *path, int resume)
{
    otb_dl *d = otb_dl_start(url, path, resume);
    int rc;
    if (!d)
        return -1;
    while ((rc = otb_dl_step(d)) == 0) {
        int n;
        curl_multi_poll(d->multi, NULL, 0, 200, &n);
    }
    if (rc < 0)
        fprintf(stderr, "opentbblue: %s: %s\n", url, otb_dl_error(d));
    otb_dl_free(d);
    return rc > 0 ? 0 : -1;
}

typedef struct text_buf {
    char *buf;
    size_t size, len;
} text_buf;

static size_t on_text(char *p, size_t size, size_t n, void *arg)
{
    text_buf *t = arg;
    size_t len = size * n, room = t->size - 1 - t->len;
    if (len > room)
        len = room;     /* the rest is not needed */
    memcpy(t->buf + t->len, p, len);
    t->len += len;
    t->buf[t->len] = 0;
    return size * n;
}

int otb_http_text(const char *url, char *buf, size_t size)
{
    CURL *c;
    CURLcode rc;
    text_buf t;
    char err[CURL_ERROR_SIZE] = "";
    if (!size || global_init())
        return -1;
    buf[0] = 0;
#ifdef OTB_AMIGA
    if (otb_amiga_socket_open())
        return -1;
#endif
    c = curl_easy_init();
    if (!c) {
#ifdef OTB_AMIGA
        otb_amiga_socket_close();
#endif
        return -1;
    }
    t.buf = buf;
    t.size = size;
    t.len = 0;
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(c, CURLOPT_USERAGENT, "OpenTBBlue/" OTB_VERSION);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, on_text);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &t);
    curl_easy_setopt(c, CURLOPT_ERRORBUFFER, err);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 30L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 90L);
#ifdef _WIN32
    curl_easy_setopt(c, CURLOPT_SSL_OPTIONS, (long)CURLSSLOPT_NATIVE_CA);
#endif
    rc = curl_easy_perform(c);
    curl_easy_cleanup(c);
#ifdef OTB_AMIGA
    otb_amiga_socket_close();
#endif
    if (rc != CURLE_OK) {
        fprintf(stderr, "opentbblue: %s: %s\n", url, err[0] ? err : curl_easy_strerror(rc));
        return -1;
    }
    return 0;
}
