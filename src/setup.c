/*
 * OpenTBBlue: host folders and the first-run fetch (see setup.h).
 *
 * The download is done by the host's own curl, which every platform
 * OpenTBBlue runs on has: Linux, macOS, Windows 10 and later (curl.exe), and
 * AmigaOS with DalsinAI/gfopencurl. The archive is a plain .tar, so
 * unpacking needs no decompressor; the reader below is our own.
 */
#include "setup.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef _WIN32
#include <direct.h>
#define otb_mkdir1(p) _mkdir(p)
#else
#include <unistd.h>
#define otb_mkdir1(p) mkdir((p), 0777)
#endif

#define TBBLUE_API  "https://gitlab.com/api/v4/projects/thesmog358%2Ftbblue/repository/commits?ref_name=master&per_page=1"
#define TBBLUE_TAR  "https://gitlab.com/thesmog358/tbblue/-/archive/%s/tbblue-%s.tar"
#define MARK_FILE   ".opentbblue"

/* Where the files go when nothing says otherwise. On AmigaOS that is beside
 * the program, on the disk the user chose to put it on. */
#if defined(__AMIGA__) || defined(__amigaos__)
#define OTB_DEFAULT_HOME "PROGDIR:OpenTBBlue-data"
#else
#define OTB_DEFAULT_HOME "opentbblue-data"
#endif

static char home[1024];

static int is_dir(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

static int is_file(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0 && S_ISREG(st.st_mode);
}

int otb_mkdirs(const char *path)
{
    char buf[1024];
    size_t i, n = strlen(path);
    if (n == 0 || n >= sizeof buf)
        return -1;
    memcpy(buf, path, n + 1);
    for (i = 1; i <= n; i++) {
        if (buf[i] == '/' || buf[i] == '\\' || buf[i] == 0) {
            char c = buf[i];
            /* "C:" or an Amiga volume "Work:" is not a folder to make. */
            if (buf[i - 1] == ':')
                continue;
            buf[i] = 0;
            if (!is_dir(buf) && otb_mkdir1(buf) && errno != EEXIST) {
                buf[i] = c;
                return -1;
            }
            buf[i] = c;
        }
    }
    return 0;
}

const char *otb_home(void)
{
    const char *e;
    if (home[0])
        return home;
    if ((e = getenv("OPENTBBLUE_HOME")) && *e)
        snprintf(home, sizeof home, "%s", e);
#if defined(_WIN32)
    else if ((e = getenv("APPDATA")) && *e)
        snprintf(home, sizeof home, "%s\\OpenTBBlue", e);
#elif defined(__APPLE__)
    else if ((e = getenv("HOME")) && *e)
        snprintf(home, sizeof home, "%s/Library/Application Support/OpenTBBlue", e);
#elif !defined(__AMIGA__) && !defined(__amigaos__)
    else if ((e = getenv("XDG_DATA_HOME")) && *e)
        snprintf(home, sizeof home, "%s/opentbblue", e);
    else if ((e = getenv("HOME")) && *e)
        snprintf(home, sizeof home, "%s/.local/share/opentbblue", e);
#endif
    else
        snprintf(home, sizeof home, "%s", OTB_DEFAULT_HOME);
    if (otb_mkdirs(home)) {
        fprintf(stderr, "opentbblue: cannot make %s\n", home);
        home[0] = 0;
        return NULL;
    }
    return home;
}

static void join(char *out, size_t size, const char *dir, const char *name)
{
    size_t n = strlen(dir);
    const char *sep = (n && (dir[n - 1] == '/' || dir[n - 1] == ':' || dir[n - 1] == '\\')) ? "" : "/";
    snprintf(out, size, "%s%s%s", dir, sep, name);
}

void otb_path(char *out, size_t size, const char *name)
{
    const char *h = otb_home();
    join(out, size, h ? h : ".", name);
}

int otb_card_ready(const char *sd)
{
    char p[1100];
    join(p, sizeof p, sd, "TBBLUE.FW");
    if (!is_file(p))
        return 0;
    join(p, sizeof p, sd, "machines/next");
    return is_dir(p);
}

int otb_default_config(const char *sd)
{
    char p[1100];
    FILE *fp;
    join(p, sizeof p, sd, "machines/next/config.ini");
    if (is_file(p))
        return 0;
    fp = fopen(p, "wb");   /* LF only, as the Next's own files */
    if (!fp)
        return -1;
    /* timing=0 is the first VGA mode at 50 Hz; the firmware fills in every
     * other setting with its defaults and rewrites the file itself when the
     * user changes anything in its editor. */
    fputs("timing=0\n", fp);
    return fclose(fp);
}

/* ---- tar ---------------------------------------------------------------- */

static unsigned long octal(const char *p, size_t n)
{
    unsigned long v = 0;
    size_t i = 0;
    while (i < n && (p[i] == ' ' || p[i] == 0))
        i++;
    for (; i < n && p[i] >= '0' && p[i] <= '7'; i++)
        v = v * 8 + (unsigned long)(p[i] - '0');
    return v;
}

static int header_ok(const unsigned char *h)
{
    unsigned long sum = 0, want = octal((const char *)h + 148, 8);
    int i;
    for (i = 0; i < 512; i++)
        sum += (i >= 148 && i < 156) ? ' ' : h[i];
    return sum == want;
}

/* A name from the archive, made safe: the top folder (tbblue-<commit>/)
 * dropped, no absolute paths, no "..". Returns 0 to skip the entry. */
static int clean_name(const char *in, char *out, size_t size)
{
    const char *p = strchr(in, '/');
    size_t n;
    if (in[0] == '/' || !p || !p[1])
        return 0;
    p++;
    if (*p == '/' || strstr(p, "..") == p || strstr(p, "/../") || strchr(p, ':') || strchr(p, '\\'))
        return 0;
    n = strlen(p);
    if (n >= 3 && !strcmp(p + n - 3, "/.."))
        return 0;
    snprintf(out, size, "%s", p);
    n = strlen(out);
    while (n && out[n - 1] == '/')
        out[--n] = 0;
    return n > 0;
}

/* The path= record of a pax extended header. */
static void pax_path(const char *rec, size_t len, char *out, size_t size)
{
    size_t i = 0;
    while (i < len) {
        size_t l = 0, j = i;
        while (j < len && rec[j] >= '0' && rec[j] <= '9')
            l = l * 10 + (size_t)(rec[j++] - '0');
        if (l == 0 || i + l > len)
            return;
        if (j < len && rec[j] == ' ' && !strncmp(rec + j + 1, "path=", 5)) {
            size_t s = j + 6, e = i + l - 1;   /* drop the newline */
            if (e > s && e - s < size) {
                memcpy(out, rec + s, e - s);
                out[e - s] = 0;
            }
        }
        i += l;
    }
}

int otb_untar(const char *tar, const char *dir, long *files)
{
    FILE *in = fopen(tar, "rb");
    unsigned char h[512];
    char longname[1024] = "", name[1024], rel[1024], path[2200];
    char *buf = NULL;
    size_t bufsize = 0;
    long count = 0;
    int zeros = 0, rc = 0;

    if (!in)
        return -1;
    if (otb_mkdirs(dir)) {
        fclose(in);
        return -1;
    }
    while (fread(h, 1, 512, in) == 512) {
        unsigned long size;
        char type;
        size_t pad;
        int i, empty = 1;
        for (i = 0; i < 512; i++)
            if (h[i]) { empty = 0; break; }
        if (empty) {
            if (++zeros == 2)
                break;
            continue;
        }
        zeros = 0;
        if (!header_ok(h)) {
            fprintf(stderr, "opentbblue: the archive is damaged (a bad header after %ld files)\n", count);
            rc = -1;
            break;
        }
        size = octal((const char *)h + 124, 12);
        type = (char)h[156];
        pad = (size_t)((512 - size % 512) % 512);

        if (type == 'x' || type == 'g' || type == 'L') {
            /* Extended headers: read the record, use its path for the next entry. */
            if (size > (1u << 20)) { rc = -1; break; }
            if (size + 1 > bufsize) {
                char *nb = realloc(buf, size + 1);
                if (!nb) { rc = -1; break; }
                buf = nb;
                bufsize = size + 1;
            }
            if (fread(buf, 1, size, in) != size || fseek(in, (long)pad, SEEK_CUR)) { rc = -1; break; }
            buf[size] = 0;
            if (type == 'L')
                snprintf(longname, sizeof longname, "%s", buf);
            else if (type == 'x')
                pax_path(buf, size, longname, sizeof longname);
            continue;
        }

        if (longname[0]) {
            snprintf(name, sizeof name, "%s", longname);
            longname[0] = 0;
        } else if (h[345]) {   /* ustar prefix */
            snprintf(name, sizeof name, "%.155s/%.100s", (const char *)h + 345, (const char *)h);
        } else {
            snprintf(name, sizeof name, "%.100s", (const char *)h);
        }

        if (!clean_name(name, rel, sizeof rel)) {
            if (fseek(in, (long)(size + pad), SEEK_CUR)) { rc = -1; break; }
            continue;
        }
        join(path, sizeof path, dir, rel);

        if (type == '5') {
            if (otb_mkdirs(path)) { rc = -1; break; }
        } else if (type == '0' || type == 0 || type == '7') {
            FILE *out;
            char *slash = strrchr(path, '/');
            unsigned long left = size;
            static char chunk[65536];
            if (slash) {
                *slash = 0;
                if (otb_mkdirs(path)) { rc = -1; break; }
                *slash = '/';
            }
            out = fopen(path, "wb");
            if (!out) {
                fprintf(stderr, "opentbblue: cannot write %s\n", path);
                rc = -1;
                break;
            }
            while (left) {
                size_t want = left > sizeof chunk ? sizeof chunk : (size_t)left;
                if (fread(chunk, 1, want, in) != want || fwrite(chunk, 1, want, out) != want) {
                    rc = -1;
                    break;
                }
                left -= want;
            }
            if (fclose(out) || rc) { rc = -1; break; }
            if (pad && fseek(in, (long)pad, SEEK_CUR)) { rc = -1; break; }
            count++;
        } else {
            /* Links and devices: the distribution has none worth keeping. */
            if (fseek(in, (long)(size + pad), SEEK_CUR)) { rc = -1; break; }
        }
    }
    free(buf);
    fclose(in);
    if (files)
        *files = count;
    return rc;
}

/* ---- fetch -------------------------------------------------------------- */

static int run_curl(const char *url, const char *out, int quiet)
{
    char cmd[3000];
    snprintf(cmd, sizeof cmd, "curl -fL --retry 3 %s -o \"%s\" \"%s\"",
             quiet ? "-sS" : "-#", out, url);
    return system(cmd) == 0 ? 0 : -1;
}

/* The newest commit on the distribution's master: its id and date. */
static int latest(char id[41], char date[32])
{
    char tmp[1100], *p;
    FILE *fp;
    static char json[8192];
    size_t n;
    otb_path(tmp, sizeof tmp, "latest.json");
    if (run_curl(TBBLUE_API, tmp, 1))
        return -1;
    fp = fopen(tmp, "rb");
    if (!fp)
        return -1;
    n = fread(json, 1, sizeof json - 1, fp);
    fclose(fp);
    remove(tmp);
    json[n] = 0;
    p = strstr(json, "\"id\":\"");
    if (!p || strspn(p + 6, "0123456789abcdef") < 40)
        return -1;
    memcpy(id, p + 6, 40);
    id[40] = 0;
    date[0] = 0;
    if ((p = strstr(json, "\"committed_date\":\"")) != NULL)
        sscanf(p + 18, "%31[^\"]", date);
    return 0;
}

static int read_mark(const char *sd, char id[41])
{
    char p[1100];
    FILE *fp;
    id[0] = 0;
    join(p, sizeof p, sd, MARK_FILE);
    fp = fopen(p, "r");
    if (!fp)
        return -1;
    if (fscanf(fp, "commit=%40s", id) != 1)
        id[0] = 0;
    fclose(fp);
    return id[0] ? 0 : -1;
}

int otb_fetch(const char *sd, int update)
{
    char id[41], date[32], have[41], url[300], tar[1100];
    long files = 0;
    FILE *fp;

    if (otb_card_ready(sd) && !update) {
        printf("OpenTBBlue: the Next's SD card is already at %s\n", sd);
        return 0;
    }
    printf("OpenTBBlue: looking up the latest System/Next distribution...\n");
    if (latest(id, date)) {
        fprintf(stderr, "opentbblue: cannot reach gitlab.com/thesmog358/tbblue (is curl installed and the network up?)\n");
        return -1;
    }
    if (update && read_mark(sd, have) == 0 && !strcmp(have, id)) {
        printf("OpenTBBlue: the SD card already has the latest distribution (%.10s, %s)\n", id, date);
        return 0;
    }
    snprintf(url, sizeof url, TBBLUE_TAR, id, id);
    otb_path(tar, sizeof tar, "tbblue-download.tar");
    printf("OpenTBBlue: downloading System/Next %.10s (%s), about 100 MB...\n", id, date);
    if (run_curl(url, tar, 0)) {
        fprintf(stderr, "opentbblue: the download failed\n");
        remove(tar);
        return -1;
    }
    printf("OpenTBBlue: unpacking into %s\n", sd);
    if (otb_untar(tar, sd, &files) || files < 100) {
        fprintf(stderr, "opentbblue: unpacking failed (%ld files)\n", files);
        remove(tar);
        return -1;
    }
    remove(tar);
    otb_default_config(sd);
    {
        char p[1100];
        join(p, sizeof p, sd, MARK_FILE);
        fp = fopen(p, "w");
        if (fp) {
            fprintf(fp, "commit=%s\ndate=%s\nsource=https://gitlab.com/thesmog358/tbblue\n", id, date);
            fclose(fp);
        }
    }
    printf("OpenTBBlue: %ld files ready. The distribution's licence is in %s/LICENSE.md\n", files, sd);
    return 0;
}
