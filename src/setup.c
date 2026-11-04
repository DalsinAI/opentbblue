/*
 * OpenTBBlue: host folders and the first-run fetch (see setup.h).
 *
 * The download is OpenTBBlue's own (fetch.c, libcurl built in), so nothing
 * else needs installing for it. The archive is a plain .tar, so unpacking
 * needs no decompressor; the reader below is our own.
 */
#include "setup.h"
#include "fatimg.h"
#include "fetch.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#if defined(__AMIGA__) || defined(__amigaos__)
#include <proto/dos.h>
#endif

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
otb_progress otb_prog;

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

/* OPENTBBLUE_HOME. On AmigaOS libnix's getenv() sees only Set (local)
 * variables, so dos.library's GetVar() is asked: it finds a SetEnv one too. */
static const char *home_var(void)
{
#if defined(__AMIGA__) || defined(__amigaos__)
    static char v[512];
    return GetVar((STRPTR)"OPENTBBLUE_HOME", (STRPTR)v, sizeof v, 0) > 0 ? v : NULL;
#else
    return getenv("OPENTBBLUE_HOME");
#endif
}

#if defined(__AMIGA__) || defined(__amigaos__)
/* "PROGDIR:x" as a full path (Work:OpenTBBlue/x): PROGDIR: means this
 * program's drawer only to this program, and the settings and card paths
 * are kept as full ones. */
static void amiga_full(char *out, size_t size, const char *path)
{
    char buf[1024];
    BPTR dir = GetProgramDir();
    if (strncmp(path, "PROGDIR:", 8) || !dir || !NameFromLock(dir, (STRPTR)buf, sizeof buf) ||
        !AddPart((STRPTR)buf, (CONST_STRPTR)(path + 8), sizeof buf)) {
        snprintf(out, size, "%s", path);
        return;
    }
    snprintf(out, size, "%s", buf);
}
#endif

const char *otb_home(void)
{
    const char *e;
    if (home[0])
        return home;
    if ((e = home_var()) && *e)
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
#if defined(__AMIGA__) || defined(__amigaos__)
    {
        char full[1024];
        amiga_full(full, sizeof full, home);
        snprintf(home, sizeof home, "%s", full);
    }
#endif
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
            otb_prog.files = count;
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

void otb_release_url(const otb_release *r, char *out, size_t size)
{
    snprintf(out, size, TBBLUE_TAR, r->id, r->id);
}

/* The commit id and date out of the API's answer. */
int otb_release_parse(const char *json, otb_release *r)
{
    const char *p = strstr(json, "\"id\":\"");
    memset(r, 0, sizeof *r);
    if (!p || strspn(p + 6, "0123456789abcdef") < 40)
        return -1;
    memcpy(r->id, p + 6, 40);
    r->id[40] = 0;
    if ((p = strstr(json, "\"committed_date\":\"")) != NULL)
        sscanf(p + 18, "%31[^\"]", r->date);
    return 0;
}

const char *otb_release_api(void)
{
    return TBBLUE_API;
}

/* The newest commit on the distribution's master: its id and date. */
int otb_latest(otb_release *r)
{
    static char json[8192];
    if (otb_http_text(TBBLUE_API, json, sizeof json))
        return -1;
    return otb_release_parse(json, r);
}

int otb_card_release(const char *sd, otb_release *r)
{
    char p[1100], line[200];
    FILE *fp;
    memset(r, 0, sizeof *r);
    join(p, sizeof p, sd, MARK_FILE);
    fp = fopen(p, "r");
    if (!fp)
        return -1;
    while (fgets(line, sizeof line, fp)) {
        if (!strncmp(line, "commit=", 7))
            sscanf(line + 7, "%40s", r->id);
        else if (!strncmp(line, "date=", 5))
            sscanf(line + 5, "%31s", r->date);
    }
    fclose(fp);
    return r->id[0] ? 0 : -1;
}

/* A note beside each file in the media repository: where it came from. */
static void write_sidecar(const char *file, const char *source, const char *commit, const char *date,
                          long long size)
{
    char p[1200];
    FILE *fp;
    snprintf(p, sizeof p, "%s.json", file);
    fp = fopen(p, "wb");
    if (!fp)
        return;
    fprintf(fp, "{\n  \"source\": \"%s\",\n", source);
    if (commit && *commit)
        fprintf(fp, "  \"commit\": \"%s\",\n", commit);
    if (date && *date)
        fprintf(fp, "  \"date\": \"%s\",\n", date);
    fprintf(fp, "  \"size\": %lld,\n  \"note\": \"As downloaded; OpenTBBlue never changes this file.\"\n}\n", size);
    fclose(fp);
}

static long long file_size(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0 && S_ISREG(st.st_mode) ? (long long)st.st_size : -1;
}

static void write_mark(const char *sd, const char *id, const char *date, const char *from)
{
    char p[1100];
    FILE *fp;
    join(p, sizeof p, sd, MARK_FILE);
    fp = fopen(p, "w");
    if (fp) {
        fprintf(fp, "commit=%s\ndate=%s\nsource=https://gitlab.com/thesmog358/tbblue\nmedia=%s\n", id, date, from);
        fclose(fp);
    }
}

static void tar_name(const otb_release *r, char *name, size_t size)
{
    snprintf(name, size, "tbblue-%s.tar", r->id);
}

int otb_release_tar(const otb_release *r, char *tar, size_t size)
{
    char media[1100], name[80];
    otb_path(media, sizeof media, OTB_MEDIA);
    if (otb_mkdirs(media))
        return -1;
    tar_name(r, name, sizeof name);
    join(tar, size, media, name);
    return file_size(tar) >= 1000000 ? 1 : 0;
}

int otb_release_downloaded(const otb_release *r, const char *part)
{
    char tar[1200], url[300];
    size_t n = strlen(part);
    if (n < 5 || strcmp(part + n - 5, ".part"))
        return -1;
    snprintf(tar, sizeof tar, "%.*s", (int)(n - 5), part);
    remove(tar);
    if (rename(part, tar))
        return -1;
    otb_release_url(r, url, sizeof url);
    write_sidecar(tar, url, r->id, r->date, file_size(tar));
    return 0;
}

int otb_release_install(const otb_release *r, const char *sd)
{
    char tar[1200], name[80];
    long files = 0;
    if (otb_release_tar(r, tar, sizeof tar) != 1 || otb_mkdirs(sd))
        return -1;
    otb_prog.phase = 3;
    otb_prog.files = 0;
    printf("OpenTBBlue: writing System/Next %.10s into %s\n", r->id, sd);
    if (otb_untar(tar, sd, &files) || files < 100) {
        fprintf(stderr, "opentbblue: unpacking failed (%ld files)\n", files);
        return -1;
    }
    otb_default_config(sd);
    tar_name(r, name, sizeof name);
    write_mark(sd, r->id, r->date, name);
    printf("OpenTBBlue: %ld files ready. The distribution's licence is in %s/LICENSE.md\n", files, sd);
    return 0;
}

int otb_fetch(const char *sd, int update)
{
    otb_release r, have;
    char url[300], tar[1200], part[1300];
    int got;

    if (otb_card_ready(sd) && !update) {
        printf("OpenTBBlue: the Next's SD card is already at %s\n", sd);
        return 0;
    }
    otb_prog.phase = 1;
    otb_prog.files = 0;
    printf("OpenTBBlue: looking up the latest System/Next distribution...\n");
    if (otb_latest(&r)) {
        fprintf(stderr, "opentbblue: cannot reach gitlab.com/thesmog358/tbblue (is the network up?)\n");
        return -1;
    }
    if (update && otb_card_release(sd, &have) == 0 && !strcmp(have.id, r.id)) {
        printf("OpenTBBlue: the SD card already has the latest distribution (%.10s, %s)\n", r.id, r.date);
        return 0;
    }
    /* The official archive is kept, unchanged, in the media repository;
     * one already there is not downloaded again, and a part of one is
     * carried on from where it stopped. */
    if ((got = otb_release_tar(&r, tar, sizeof tar)) < 0)
        return -1;
    if (!got) {
        otb_release_url(&r, url, sizeof url);
        snprintf(part, sizeof part, "%s.part", tar);
        otb_prog.phase = 2;
        printf("OpenTBBlue: downloading System/Next %.10s (%s), about 100 MB...\n", r.id, r.date);
        if (otb_dl_wait(url, part, 1) || otb_release_downloaded(&r, part)) {
            fprintf(stderr, "opentbblue: the download failed (what came is kept, and carried on next time)\n");
            return -1;
        }
    } else {
        printf("OpenTBBlue: using System/Next %.10s from the media repository\n", r.id);
    }
    return otb_release_install(&r, sd);
}

/* ---- images ------------------------------------------------------------- */

static const char *base_name(const char *p)
{
    const char *b = p, *q;
    for (q = p; *q; q++)
        if (*q == '/' || *q == '\\' || *q == ':')
            b = q + 1;
    return b;
}

static int copy_into(const char *from, const char *to)
{
    char part[1400];
    static char chunk[65536];
    FILE *in = fopen(from, "rb"), *out;
    size_t n;
    int rc = 0;
    if (!in)
        return -1;
    snprintf(part, sizeof part, "%s.part", to);
    out = fopen(part, "wb");
    if (!out) {
        fclose(in);
        return -1;
    }
    while ((n = fread(chunk, 1, sizeof chunk, in)) > 0)
        if (fwrite(chunk, 1, n, out) != n) {
            rc = -1;
            break;
        }
    fclose(in);
    if (fclose(out) || rc || rename(part, to)) {
        remove(part);
        return -1;
    }
    return 0;
}

int otb_import_image(const char *image, char *drive, size_t size)
{
    char media[1100], kept[1300], drives[1100], name[256], mark[1400], want[400], got[400] = "";
    const char *b = base_name(image);
    long long isize = file_size(image);
    long files = 0;
    size_t n;
    FILE *fp;

    if (isize <= 0 || !otb_img_is_fat(image)) {
        fprintf(stderr, "opentbblue: %s is not a FAT disk image\n", image);
        return -1;
    }
    /* drives/<image name without .img> */
    snprintf(name, sizeof name, "%s", b);
    n = strlen(name);
    if (n > 4 && (!strcmp(name + n - 4, ".img") || !strcmp(name + n - 4, ".IMG") ||
                  !strcmp(name + n - 4, ".hdf") || !strcmp(name + n - 4, ".bin")))
        name[n - 4] = 0;
    otb_path(drives, sizeof drives, OTB_DRIVES);
    join(drive, size, drives, name);

    /* Already written into that drive from this image: use it as it is,
     * with whatever the Next has saved there since. */
    snprintf(want, sizeof want, "image=%s size=%lld", b, isize);
    join(mark, sizeof mark, drive, ".opentbblue-image");
    if ((fp = fopen(mark, "r")) != NULL) {
        if (!fgets(got, sizeof got, fp))
            got[0] = 0;
        fclose(fp);
        got[strcspn(got, "\r\n")] = 0;
        if (!strcmp(got, want))
            return 0;
    }
    if (is_dir(drive)) {
        fprintf(stderr, "opentbblue: %s already holds a drive; move it away to write %s there again\n", drive, b);
        return -1;
    }

    /* The official image goes into the media repository unchanged. */
    otb_path(media, sizeof media, OTB_MEDIA);
    if (otb_mkdirs(media))
        return -1;
    join(kept, sizeof kept, media, b);
    if (file_size(kept) != isize) {
        printf("OpenTBBlue: keeping %s in the media repository\n", b);
        if (copy_into(image, kept)) {
            fprintf(stderr, "opentbblue: cannot copy %s into %s\n", image, media);
            return -1;
        }
        write_sidecar(kept, image, NULL, NULL, isize);
    }
    printf("OpenTBBlue: writing the files of %s into %s\n", b, drive);
    if (otb_img_extract(kept, drive, &files)) {
        fprintf(stderr, "opentbblue: reading %s failed after %ld files\n", b, files);
        return -1;
    }
    if ((fp = fopen(mark, "w")) != NULL) {
        fprintf(fp, "%s\n", want);
        fclose(fp);
    }
    printf("OpenTBBlue: %ld files ready in %s\n", files, drive);
    return 0;
}

/* ---- settings ----------------------------------------------------------- */

void otb_settings_defaults(otb_settings *s)
{
    memset(s, 0, sizeof *s);
    snprintf(s->sd0, sizeof s->sd0, "%s/SD0", OTB_DRIVES);
    snprintf(s->sd1, sizeof s->sd1, "%s/SD1", OTB_DRIVES);
    snprintf(s->pi, sizeof s->pi, "pi");
    s->card_mb = 32768;
    s->bar_pinned = 1;
    s->scale = 3;
    s->fetch = 1;
    s->check = 1;
}

static void trim(char *t)
{
    size_t n = strlen(t);
    while (n && (t[n - 1] == ' ' || t[n - 1] == '\t' || t[n - 1] == '\r' || t[n - 1] == '\n'))
        t[--n] = 0;
}

int otb_settings_load(otb_settings *s)
{
    char p[1100], line[1200];
    FILE *fp;
    otb_settings_defaults(s);
    otb_path(p, sizeof p, OTB_SETTINGS);
    fp = fopen(p, "r");
    if (!fp) {
        fp = fopen(p, "wb");
        if (!fp)
            return -1;
        fprintf(fp,
                "# OpenTBBlue settings. Options on the command line win over these.\n"
                "# Paths are inside this folder unless they are full paths.\n"
                "#\n"
                "# sd0, sd1: the Next's two SD card slots. A folder is used as the card;\n"
                "#   a .img is kept in media/ and its files written into drives/<name>/,\n"
                "#   which is then the card. sd0 is filled from the latest System/Next\n"
                "#   on the first run; sd1 starts empty. Leave sd1 blank for no second card.\n"
                "sd0=%s\n"
                "sd1=%s\n"
                "# card_mb: how big a folder card looks to the Next, in MB (64-32768),\n"
                "#   or 0 to fit its files with room to spare. Each 1 GB of card takes\n"
                "#   about 0.6 MB of memory (32 GB: about 20 MB per card).\n"
                "card_mb=%d\n"
                "# pi: the NextPi's files\n"
                "pi=%s\n"
                "# scale: window size (1-6); fullscreen: 1 to start full screen\n"
                "scale=%d\n"
                "fullscreen=0\n"
                "# fetch: 1 to fetch System/Next when the card is empty, 0 never\n"
                "fetch=1\n"
                "# bar: the control bar under the picture, pinned or auto (it comes up\n"
                "#   at the bottom edge); the pin button on the bar sets this\n"
                "bar=pinned\n"
                "# check: 1 to look for a newer System/Next at each start (the card\n"
                "#   button on the bar then lights up), 0 only when asked\n"
                "check=1\n",
                s->sd0, s->sd1, s->card_mb, s->pi, s->scale);
        return fclose(fp);
    }
    while (fgets(line, sizeof line, fp)) {
        char *eq = strchr(line, '=');
        if (line[0] == '#' || !eq)
            continue;
        *eq = 0;
        trim(line);
        trim(eq + 1);
        if (!strcmp(line, "sd0")) snprintf(s->sd0, sizeof s->sd0, "%s", eq + 1);
        else if (!strcmp(line, "sd1")) snprintf(s->sd1, sizeof s->sd1, "%s", eq + 1);
        else if (!strcmp(line, "pi")) snprintf(s->pi, sizeof s->pi, "%s", eq + 1);
        else if (!strcmp(line, "card_mb")) s->card_mb = atoi(eq + 1);
        else if (!strcmp(line, "bar")) s->bar_pinned = strcmp(eq + 1, "auto") != 0;
        else if (!strcmp(line, "scale")) s->scale = atoi(eq + 1);
        else if (!strcmp(line, "fullscreen")) s->fullscreen = atoi(eq + 1) != 0;
        else if (!strcmp(line, "fetch")) s->fetch = atoi(eq + 1) != 0;
        else if (!strcmp(line, "check")) s->check = atoi(eq + 1) != 0;
    }
    fclose(fp);
    return 0;
}

int otb_settings_put(const char *key, const char *value)
{
    char p[1100], q[1110], line[1200];
    size_t kl = strlen(key);
    int done = 0;
    FILE *in, *out;
    otb_path(p, sizeof p, OTB_SETTINGS);
    snprintf(q, sizeof q, "%s.new", p);
    in = fopen(p, "r");
    out = fopen(q, "wb");
    if (!out) {
        if (in) fclose(in);
        return -1;
    }
    while (in && fgets(line, sizeof line, in)) {
        if (!done && !strncmp(line, key, kl) && (line[kl] == '=' || line[kl] == ' ')) {
            fprintf(out, "%s=%s\n", key, value);
            done = 1;
        } else
            fputs(line, out);
    }
    if (!done)
        fprintf(out, "%s=%s\n", key, value);
    if (in) fclose(in);
    if (fclose(out))
        return -1;
    remove(p);
    return rename(q, p);
}

void otb_resolve(char *out, size_t size, const char *p)
{
    int full = p[0] == '/' || p[0] == '\\' || strchr(p, ':') != NULL;
    if (full)
        snprintf(out, size, "%s", p);
    else
        otb_path(out, size, p);
}

void otb_migrate(void)
{
    char old[1100], drives[1100], sd0[1200];
    otb_path(old, sizeof old, "sd");
    otb_path(drives, sizeof drives, OTB_DRIVES);
    join(sd0, sizeof sd0, drives, "SD0");
    if (is_dir(old) && !is_dir(sd0) && otb_mkdirs(drives) == 0 && rename(old, sd0) == 0)
        printf("OpenTBBlue: the SD card folder is now %s\n", sd0);
}

/* ---- Workbench icons ---------------------------------------------------- */

#ifdef __amigaos__
#include <dirent.h>

static int file_exists(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0;
}

/* dir.info from PROGDIR:Icons/icon, unless dir has an icon already. */
static void give_icon(const char *dir, const char *icon)
{
    char from[300], to[1200];
    size_t n = strlen(dir);
    if (!n || dir[n - 1] == ':' || dir[n - 1] == '/' || !is_dir(dir))
        return;
    snprintf(to, sizeof to, "%s.info", dir);
    snprintf(from, sizeof from, "PROGDIR:Icons/%s", icon);
    if (!file_exists(to) && file_exists(from))
        copy_into(from, to);
}

void otb_drawer_icons(void)
{
    const char *h = otb_home();
    char p[1100], drives[1100];
    DIR *d;
    struct dirent *de;
    if (!h)
        return;
    give_icon(h, strcmp(base_name(h), "OpenTBBlue-data") ? "drawer.info" : "OpenTBBlue-data.info");
    /* Only the drawers the program keeps: never a drawer inside a card,
     * whose files are the Next's. */
    otb_path(drives, sizeof drives, OTB_DRIVES);
    if ((d = opendir(h)) != NULL) {
        while ((de = readdir(d)) != NULL) {
            if (de->d_name[0] == '.')
                continue;
            join(p, sizeof p, h, de->d_name);
            give_icon(p, !strcmp(de->d_name, OTB_DRIVES) ? "drives.info" :
                         !strcmp(de->d_name, OTB_MEDIA) ? "media.info" : "drawer.info");
        }
        closedir(d);
    }
    if ((d = opendir(drives)) != NULL) {
        while ((de = readdir(d)) != NULL) {
            if (de->d_name[0] == '.')
                continue;
            join(p, sizeof p, drives, de->d_name);
            give_icon(p, "card.info");
        }
        closedir(d);
    }
}
#else
void otb_drawer_icons(void)
{
}
#endif
