/*
 * The tar reader and the first-boot config: a small archive shaped like
 * GitLab's (one top folder, a pax header, a GNU long name, a ustar prefix),
 * plus entries that must never land outside the card.
 */
#include "../src/setup.h"
#include "../src/fatimg.h"
#include "../src/storage/blockdev.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static unsigned char tarbuf[64 * 1024];
static size_t tarlen;

static void header(const char *name, const char *prefix, char type, size_t size)
{
    unsigned char *h = tarbuf + tarlen;
    unsigned sum = 0;
    int i;
    memset(h, 0, 512);
    snprintf((char *)h, 100, "%s", name);
    snprintf((char *)h + 100, 8, "%07o", 0644);
    snprintf((char *)h + 124, 12, "%011lo", (unsigned long)size & 077777777777ul);
    snprintf((char *)h + 136, 12, "%011o", 0);
    h[156] = (unsigned char)type;
    memcpy(h + 257, "ustar", 6);
    memcpy(h + 263, "00", 2);
    if (prefix)
        snprintf((char *)h + 345, 155, "%s", prefix);
    memset(h + 148, ' ', 8);
    for (i = 0; i < 512; i++)
        sum += h[i];
    snprintf((char *)h + 148, 8, "%06o", sum);
    tarlen += 512;
}

static void body(const char *data, size_t n)
{
    memcpy(tarbuf + tarlen, data, n);
    tarlen += (n + 511) / 512 * 512;
}

static void entry(const char *name, const char *data)
{
    header(name, NULL, '0', strlen(data));
    body(data, strlen(data));
}

static int file_is(const char *path, const char *want)
{
    char got[256] = "";
    FILE *fp = fopen(path, "rb");
    size_t n;
    if (!fp)
        return 0;
    n = fread(got, 1, sizeof got - 1, fp);
    fclose(fp);
    got[n] = 0;
    return !strcmp(got, want);
}

static int exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

/* Make a disk image of a folder with the emulator's own FAT writer (vfat.c),
 * the way the Next would see it, for the image reader to read back. */
static int folder_to_image(const char *folder, const char *imgpath, int fat16)
{
    zx_blockdev *bd = fat16 ? zx_bd_open_folder16(folder, 64) : zx_bd_open_folder(folder, 64);
    uint8_t buf[ZX_BLOCK];
    uint64_t i;
    FILE *fp;
    if (!bd)
        return -1;
    fp = fopen(imgpath, "wb");
    if (!fp)
        return -1;
    for (i = 0; i < bd->blocks; i++) {
        bd->read(bd, i, buf);
        fwrite(buf, 1, ZX_BLOCK, fp);
    }
    fclose(fp);
    bd->close(bd);
    return 0;
}

static void write_text(const char *path, const char *text)
{
    FILE *fp = fopen(path, "wb");
    if (fp) {
        fputs(text, fp);
        fclose(fp);
    }
}

static void image_round_trip(const char *base, int fat16)
{
    char src[600], img[600], out[600], p[1200];
    static char big[200000];
    long files = -1;
    int i;
    FILE *fp;
    snprintf(src, sizeof src, "%s-img%d-src", base, fat16);
    snprintf(img, sizeof img, "%s-img%d.img", base, fat16);
    snprintf(out, sizeof out, "%s-img%d-out", base, fat16);
    snprintf(p, sizeof p, "%s/machines/next", src);
    otb_mkdirs(p);
    snprintf(p, sizeof p, "%s/TBBLUE.FW", src);
    write_text(p, "firmware");
    snprintf(p, sizeof p, "%s/machines/next/A long file name.txt", src);
    write_text(p, "long");
    snprintf(p, sizeof p, "%s/lower.bas", src);
    write_text(p, "lower");
    for (i = 0; i < (int)sizeof big; i++)
        big[i] = (char)('a' + i % 26);
    snprintf(p, sizeof p, "%s/machines/big.bin", src);
    fp = fopen(p, "wb");
    fwrite(big, 1, sizeof big, fp);
    fclose(fp);

    CHECK(folder_to_image(src, img, fat16) == 0);
    CHECK(otb_img_is_fat(img));
    CHECK(otb_img_extract(img, out, &files) == 0);
    CHECK(files == 4);
    snprintf(p, sizeof p, "%s/TBBLUE.FW", out);
    CHECK(file_is(p, "firmware"));
    snprintf(p, sizeof p, "%s/machines/next/A long file name.txt", out);
    CHECK(file_is(p, "long"));
    snprintf(p, sizeof p, "%s/lower.bas", out);
    CHECK(file_is(p, "lower"));
    snprintf(p, sizeof p, "%s/machines/big.bin", out);
    fp = fopen(p, "rb");
    CHECK(fp != NULL);
    if (fp) {
        static char back[200001];
        size_t n = fread(back, 1, sizeof back, fp);
        fclose(fp);
        CHECK(n == sizeof big && !memcmp(back, big, sizeof big));
    }
    CHECK(otb_card_ready(out));
    /* not an image */
    snprintf(p, sizeof p, "%s/lower.bas", src);
    CHECK(!otb_img_is_fat(p));
}

int main(void)
{
    char dir[256], p[1024], longname[512], pax[600];
    const char *tmp = getenv("TMPDIR");
    FILE *fp;
    long files = -1;

    if (!tmp || !*tmp) tmp = getenv("TEMP");
    if (!tmp || !*tmp) tmp = getenv("TMP");
#ifdef _WIN32
    if (!tmp || !*tmp) tmp = ".";
#endif
    snprintf(dir, sizeof dir, "%s/otb-setup-test-%ld", tmp ? tmp : "/tmp", (long)time(NULL) ^ (long)clock());

    /* GitLab's archive: a global pax header, then everything in tbblue-<id>/. */
    header("pax_global_header", NULL, 'g', 52);
    body("52 comment=5cd10da6d39210413c223c4f8becf56f093dca78\n", 52);
    header("tbblue-x/", NULL, '5', 0);
    entry("tbblue-x/TBBLUE.FW", "firmware");
    header("tbblue-x/machines/next/", NULL, '5', 0);
    entry("tbblue-x/machines/next/menu.def", "menu");
    /* A long name by pax ... */
    snprintf(longname, sizeof longname, "tbblue-x/docs/%s.txt",
             "a-very-long-name-that-does-not-fit-in-the-hundred-bytes-of-a-plain-tar-header-at-all-really");
    snprintf(pax, sizeof pax, "%d path=%s\n", (int)(strlen(longname) + 7 + 3), longname);
    header("PaxHeaders/x", NULL, 'x', strlen(pax));
    body(pax, strlen(pax));
    entry("ignored-short-name", "pax");
    /* ... by GNU's ././@LongLink ... */
    header("././@LongLink", NULL, 'L', strlen("tbblue-x/apps/gnu-long-name.bas") + 1);
    body("tbblue-x/apps/gnu-long-name.bas", strlen("tbblue-x/apps/gnu-long-name.bas") + 1);
    entry("ignored", "gnu");
    /* ... and by the ustar prefix. */
    header("deep.txt", NULL, '0', 6);
    memmove(tarbuf + tarlen - 512 + 345, "tbblue-x/games/sub", 19);
    {   /* re-sum the header we just changed */
        unsigned char *h = tarbuf + tarlen - 512;
        unsigned sum = 0;
        int k;
        memset(h + 148, ' ', 8);
        for (k = 0; k < 512; k++)
            sum += h[k];
        snprintf((char *)h + 148, 8, "%06o", sum);
    }
    body("prefix", 6);
    /* Escapes that must be refused. */
    entry("tbblue-x/../escaped.txt", "bad");
    entry("/etc/opentbblue-bad", "bad");
    entry("tbblue-x/a/../../escaped2.txt", "bad");
    /* A symlink is skipped. */
    header("tbblue-x/link", NULL, '2', 0);
    tarlen += 1024;   /* end of archive */

    snprintf(p, sizeof p, "%s.tar", dir);
    fp = fopen(p, "wb");
    CHECK(fp != NULL);
    if (!fp)
        return 1;
    fwrite(tarbuf, 1, tarlen, fp);
    fclose(fp);

    CHECK(otb_untar(p, dir, &files) == 0);
    CHECK(files == 5);
    if (files != 5) printf("  files = %ld\n", files);
    snprintf(p, sizeof p, "%s/TBBLUE.FW", dir);
    CHECK(file_is(p, "firmware"));
    snprintf(p, sizeof p, "%s/machines/next/menu.def", dir);
    CHECK(file_is(p, "menu"));
    snprintf(p, sizeof p, "%s/%s", dir, longname + 9);
    CHECK(file_is(p, "pax"));
    snprintf(p, sizeof p, "%s/apps/gnu-long-name.bas", dir);
    CHECK(file_is(p, "gnu"));
    snprintf(p, sizeof p, "%s/games/sub/deep.txt", dir);
    CHECK(file_is(p, "prefix"));
    snprintf(p, sizeof p, "%s/../escaped.txt", dir);
    CHECK(!exists(p));
    snprintf(p, sizeof p, "%s/link", dir);
    CHECK(!exists(p));
    CHECK(!exists("/etc/opentbblue-bad"));

    CHECK(otb_card_ready(dir));
    CHECK(otb_default_config(dir) == 0);
    snprintf(p, sizeof p, "%s/machines/next/config.ini", dir);
    CHECK(file_is(p, "timing=0\n"));
    /* A config the user has is never replaced. */
    fp = fopen(p, "wb");
    fputs("timing=3\n", fp);
    fclose(fp);
    CHECK(otb_default_config(dir) == 0);
    CHECK(file_is(p, "timing=3\n"));

    /* A damaged header stops the unpack. */
    tarbuf[512 * 3 + 10] ^= 0x55;
    snprintf(p, sizeof p, "%s-bad.tar", dir);
    fp = fopen(p, "wb");
    fwrite(tarbuf, 1, tarlen, fp);
    fclose(fp);
    snprintf(longname, sizeof longname, "%s-bad", dir);
    CHECK(otb_untar(p, longname, &files) != 0);

    image_round_trip(dir, 0);
    image_round_trip(dir, 1);

    printf(fails ? "setup_test: %d failed\n" : "setup_test: all passed\n", fails);
    return fails != 0;
}
