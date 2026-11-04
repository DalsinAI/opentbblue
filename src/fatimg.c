/*
 * FAT image reader (see fatimg.h). Our own code, written from the FAT
 * layout (BPB, FAT12/16/32 chains, 8.3 and long-name directory entries).
 */
#define _FILE_OFFSET_BITS 64
#if !defined(_WIN32) && !defined(__AMIGA__) && !defined(__amigaos__)
#define _POSIX_C_SOURCE 200809L
#endif
#include "fatimg.h"
#include "setup.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#define SECTOR 512
#define MAX_DEPTH 32

typedef struct img {
    FILE *fp;
    uint64_t base;              /* first sector of the volume */
    uint32_t spc, rsv, nfats, fatsz, root_ent, root_clus;
    uint32_t first_data, nclus;
    int bits;                   /* 12, 16 or 32 */
    uint32_t fat_cached;        /* FAT sector in fat_buf, or ~0 */
    uint8_t fat_buf[2 * SECTOR];
    uint8_t *clus;              /* one cluster */
    long files;
} img;

static uint32_t g16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t g32(const uint8_t *p) { return g16(p) | g16(p + 2) << 16; }

static int seek_to(FILE *fp, uint64_t off)
{
#if defined(_WIN32)
    return _fseeki64(fp, (long long)off, SEEK_SET);
#elif defined(__AMIGA__) || defined(__amigaos__)
    if (off > 0x7fffffffu)
        return -1;
    return fseek(fp, (long)off, SEEK_SET);
#else
    return fseeko(fp, (off_t)off, SEEK_SET);
#endif
}

static int rd(img *im, uint64_t sector, void *buf, uint32_t count)
{
    if (seek_to(im->fp, (im->base + sector) * SECTOR))
        return -1;
    return fread(buf, SECTOR, count, im->fp) == count ? 0 : -1;
}

/* A boot sector that looks like a FAT volume's. */
static int is_bpb(const uint8_t *b)
{
    uint32_t spc = b[13];
    return (b[0] == 0xEB || b[0] == 0xE9) && g16(b + 11) == SECTOR && spc && !(spc & (spc - 1)) &&
           g16(b + 14) && b[16] >= 1 && b[16] <= 2;
}

static int open_volume(img *im, const char *path)
{
    uint8_t b[SECTOR];
    uint32_t tot, root_secs, data;
    memset(im, 0, sizeof *im);
    im->fat_cached = ~0u;
    im->fp = fopen(path, "rb");
    if (!im->fp)
        return -1;
    if (rd(im, 0, b, 1))
        return -1;
    if (!is_bpb(b)) {
        int i;
        if (b[510] != 0x55 || b[511] != 0xAA)
            return -1;
        for (i = 0; i < 4; i++) {
            const uint8_t *e = b + 446 + 16 * i;
            uint8_t t = e[4];
            if (t == 0x01 || t == 0x04 || t == 0x06 || t == 0x0b || t == 0x0c || t == 0x0e) {
                im->base = g32(e + 8);
                break;
            }
        }
        if (i == 4 || rd(im, 0, b, 1) || !is_bpb(b))
            return -1;
    }
    im->spc = b[13];
    im->rsv = g16(b + 14);
    im->nfats = b[16];
    im->root_ent = g16(b + 17);
    tot = g16(b + 19) ? g16(b + 19) : g32(b + 32);
    im->fatsz = g16(b + 22) ? g16(b + 22) : g32(b + 36);
    root_secs = (im->root_ent * 32 + SECTOR - 1) / SECTOR;
    im->first_data = im->rsv + im->nfats * im->fatsz + root_secs;
    if (!im->fatsz || im->first_data >= tot)
        return -1;
    data = tot - im->first_data;
    im->nclus = data / im->spc;
    /* A FAT32 boot sector has no 16-bit FAT size; small FAT32 volumes
     * (fewer than 65525 clusters) exist, so that decides, not the count. */
    im->bits = !g16(b + 22) ? 32 : im->nclus < 4085 ? 12 : 16;
    im->root_clus = im->bits == 32 ? g32(b + 44) : 0;
    im->clus = malloc((size_t)im->spc * SECTOR);
    return im->clus ? 0 : -1;
}

static uint32_t fat_next(img *im, uint32_t c)
{
    uint32_t off, sec, v;
    if (c < 2 || c >= im->nclus + 2)
        return 0x0fffffff;
    off = im->bits == 32 ? c * 4 : im->bits == 16 ? c * 2 : c + c / 2;
    sec = off / SECTOR;
    if (sec != im->fat_cached) {
        /* two sectors: a FAT12 entry can straddle them */
        if (rd(im, im->rsv + sec, im->fat_buf, sec + 1 < im->fatsz ? 2 : 1))
            return 0x0fffffff;
        im->fat_cached = sec;
    }
    off %= SECTOR;
    if (im->bits == 32)
        v = g32(im->fat_buf + off) & 0x0fffffff;
    else if (im->bits == 16)
        v = g16(im->fat_buf + off);
    else
        v = (c & 1) ? g16(im->fat_buf + off) >> 4 : g16(im->fat_buf + off) & 0xfff;
    if ((im->bits == 12 && v >= 0xff7) || (im->bits == 16 && v >= 0xfff7) || v >= 0x0ffffff7)
        return 0x0fffffff;
    return v;
}

static int read_cluster(img *im, uint32_t c)
{
    return rd(im, im->first_data + (uint64_t)(c - 2) * im->spc, im->clus, im->spc);
}

/* A directory's raw entries, read whole (root of FAT12/16 or a chain). */
static uint8_t *read_dir(img *im, uint32_t first, uint32_t *bytes)
{
    uint8_t *buf = NULL;
    uint32_t n = 0, csize = im->spc * SECTOR, guard = 0;
    if (!first && im->bits != 32) {
        uint32_t secs = (im->root_ent * 32 + SECTOR - 1) / SECTOR;
        buf = malloc((size_t)secs * SECTOR);
        if (!buf || rd(im, im->rsv + im->nfats * im->fatsz, buf, secs)) {
            free(buf);
            return NULL;
        }
        *bytes = secs * SECTOR;
        return buf;
    }
    if (!first)
        first = im->root_clus;
    while (first >= 2 && first < im->nclus + 2 && guard++ < 65536) {
        uint8_t *nb = realloc(buf, n + csize);
        if (!nb || read_cluster(im, first)) {
            free(nb ? nb : buf);
            return NULL;
        }
        buf = nb;
        memcpy(buf + n, im->clus, csize);
        n += csize;
        first = fat_next(im, first);
    }
    *bytes = n;
    return buf;
}

static int safe_name(const char *s)
{
    if (!s[0] || !strcmp(s, ".") || !strcmp(s, ".."))
        return 0;
    for (; *s; s++)
        if (*s == '/' || *s == '\\' || *s == ':' || (unsigned char)*s < 32)
            return 0;
    return 1;
}

/* UCS-2 from a long-name entry, as UTF-8. */
static void put_utf8(char *out, size_t *o, size_t size, uint32_t u)
{
    if (u < 0x80 && *o + 1 < size)
        out[(*o)++] = (char)u;
    else if (u < 0x800 && *o + 2 < size) {
        out[(*o)++] = (char)(0xc0 | u >> 6);
        out[(*o)++] = (char)(0x80 | (u & 0x3f));
    } else if (u >= 0x800 && *o + 3 < size) {
        out[(*o)++] = (char)(0xe0 | u >> 12);
        out[(*o)++] = (char)(0x80 | ((u >> 6) & 0x3f));
        out[(*o)++] = (char)(0x80 | (u & 0x3f));
    }
}

static uint8_t sfn_sum(const uint8_t *e)
{
    uint8_t s = 0;
    int i;
    for (i = 0; i < 11; i++)
        s = (uint8_t)(((s & 1) << 7) + (s >> 1) + e[i]);
    return s;
}

static int copy_file(img *im, uint32_t c, uint32_t size, const char *path)
{
    FILE *out = fopen(path, "wb");
    uint32_t csize = im->spc * SECTOR, left = size, guard = 0;
    if (!out)
        return -1;
    while (left && c >= 2 && c < im->nclus + 2 && guard++ <= size / csize + 1) {
        uint32_t n = left < csize ? left : csize;
        if (read_cluster(im, c) || fwrite(im->clus, 1, n, out) != n) {
            fclose(out);
            return -1;
        }
        left -= n;
        c = fat_next(im, c);
    }
    if (fclose(out) || left)
        return -1;
    im->files++;
    return 0;
}

static int walk(img *im, uint32_t first, const char *dir, int depth)
{
    uint32_t bytes = 0, i;
    uint8_t *d;
    uint16_t lfn[260];
    int lfn_ok = 0, lfn_seq = 0;
    uint8_t lfn_sum = 0;
    int rc = 0;

    if (depth > MAX_DEPTH || otb_mkdirs(dir))
        return -1;
    d = read_dir(im, first, &bytes);
    if (!d)
        return -1;
    memset(lfn, 0, sizeof lfn);
    for (i = 0; i + 32 <= bytes && !rc; i += 32) {
        uint8_t *e = d + i;
        uint8_t attr = e[11];
        char name[800], path[2200];
        size_t o = 0;
        if (e[0] == 0)
            break;
        if (e[0] == 0xE5) {
            lfn_ok = 0;
            continue;
        }
        if (attr == 0x0F) {             /* long-name piece, last one first */
            int seq = e[0] & 0x1f, k;
            static const int at[13] = { 1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30 };
            if (e[0] & 0x40) {
                memset(lfn, 0, sizeof lfn);
                lfn_ok = 1;
                lfn_sum = e[13];
            } else if (!lfn_ok || seq != lfn_seq - 1 || e[13] != lfn_sum) {
                lfn_ok = 0;
            }
            lfn_seq = seq;
            if (lfn_ok && seq >= 1 && seq <= 20)
                for (k = 0; k < 13; k++)
                    lfn[(seq - 1) * 13 + k] = (uint16_t)g16(e + at[k]);
            continue;
        }
        if (attr & 0x08) {              /* volume label */
            lfn_ok = 0;
            continue;
        }
        if (lfn_ok && lfn_seq == 1 && lfn_sum == sfn_sum(e)) {
            int k;
            for (k = 0; k < 260 && lfn[k] && lfn[k] != 0xffff; k++)
                put_utf8(name, &o, sizeof name, lfn[k]);
        } else {
            int k, end;
            for (end = 8; end > 0 && e[end - 1] == ' '; end--)
                ;
            for (k = 0; k < end; k++) {
                int ch = k == 0 && e[0] == 0x05 ? 0xE5 : e[k];
                if ((e[12] & 0x08) && ch >= 'A' && ch <= 'Z')
                    ch += 32;
                name[o++] = (char)ch;
            }
            for (end = 11; end > 8 && e[end - 1] == ' '; end--)
                ;
            if (end > 8)
                name[o++] = '.';
            for (k = 8; k < end; k++) {
                int ch = e[k];
                if ((e[12] & 0x10) && ch >= 'A' && ch <= 'Z')
                    ch += 32;
                name[o++] = (char)ch;
            }
        }
        name[o] = 0;
        lfn_ok = 0;
        if (!safe_name(name))
            continue;
        snprintf(path, sizeof path, "%s/%s", dir, name);
        {
            uint32_t c = g16(e + 26) | (im->bits == 32 ? g16(e + 20) << 16 : 0);
            if (attr & 0x10)
                rc = walk(im, c, path, depth + 1);
            else
                rc = copy_file(im, c, g32(e + 28), path);
            if (rc)
                fprintf(stderr, "opentbblue: cannot copy %s from the image\n", path);
        }
    }
    free(d);
    return rc;
}

int otb_img_extract(const char *path, const char *dir, long *files)
{
    img im;
    int rc = open_volume(&im, path);
    if (!rc)
        rc = walk(&im, 0, dir, 0);
    if (files)
        *files = im.files;
    if (im.fp)
        fclose(im.fp);
    free(im.clus);
    return rc;
}

int otb_img_is_fat(const char *path)
{
    img im;
    int ok = open_volume(&im, path) == 0;
    if (im.fp)
        fclose(im.fp);
    free(im.clus);
    return ok;
}
