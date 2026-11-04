/*
 * frames: run the Next from a card folder with typed input at set frames,
 * and print a hash of the picture every 50 frames (and of the sound), so
 * two builds can be compared frame for frame.
 *
 *   frames SD FRAMES [FRAME:TEXT ...] [-o last.ppm]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zx/zx.h"
#include "zx/keys.h"
#include "next/next.h"

static zx m;
static zx_typer ty;

static uint32_t fnv(const void *p, size_t n, uint32_t h)
{
    const uint8_t *b = p;
    while (n--)
        h = (h ^ *b++) * 16777619u;
    return h;
}

int main(int argc, char **argv)
{
    int frames, f, a, nscript = 0, at[32];
    char *text[32];
    const char *out = NULL;
    uint32_t snd = 2166136261u;
    if (argc < 3) {
        fprintf(stderr, "usage: frames SD FRAMES [FRAME:TEXT ...] [-o out.ppm]\n");
        return 2;
    }
    frames = atoi(argv[2]);
    for (a = 3; a < argc; a++) {
        char *c, *d, *s;
        if (!strcmp(argv[a], "-o") && a + 1 < argc) { out = argv[++a]; continue; }
        c = strchr(argv[a], ':');
        if (!c || nscript == 32)
            return 2;
        *c = 0;
        at[nscript] = atoi(argv[a]);
        for (s = d = c + 1; *s; s++) {
            if (s[0] == '\\' && s[1] == 'n') { *d++ = '\n'; s++; }
            else *d++ = *s;
        }
        *d = 0;
        text[nscript++] = c + 1;
    }
    if (zx_create(&m, ZX_NEXT, NULL) || zx_attach_sd(&m, 0, argv[1]))
        return 1;
    for (f = 0; f < frames; f++) {
        int i;
        for (i = 0; i < nscript; i++)
            if (at[i] == f)
                zx_type(&ty, text[i]);
        zx_type_frame(&ty, &m);
        zx_run_frame(&m);
        snd = fnv(m.audio, (size_t)m.audio_samples * 4, snd);
        if ((f + 1) % 50 == 0) {
            uint32_t h = 2166136261u;
            int y;
            for (y = 0; y < m.screen_h; y++)
                h = fnv(m.pixels + (size_t)y * m.pitch, (size_t)m.screen_w * 2, h);
            printf("%d %08x %08x\n", f + 1, h, snd);
        }
    }
    if (out) {
        FILE *fp = fopen(out, "wb");
        int x, y;
        fprintf(fp, "P6\n%d %d\n255\n", m.screen_w, m.screen_h);
        for (y = 0; y < m.screen_h; y++)
            for (x = 0; x < m.screen_w; x++) {
                uint16_t c = m.pixels[y * m.pitch + x];
                fputc(((c >> 6) & 7) * 255 / 7, fp);
                fputc(((c >> 3) & 7) * 255 / 7, fp);
                fputc((c & 7) * 255 / 7, fp);
            }
        fclose(fp);
    }
    zx_free(&m);
    return 0;
}
