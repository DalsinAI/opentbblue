/*
 * OpenTBBlue's window and sound (see video.h).
 */
#include "video.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static SDL_Window *win;
static SDL_Renderer *ren;
static SDL_Texture *tex;
static SDL_AudioDeviceID dev;
static uint16_t lut[512];          /* the Next's RGB333 to RGB565 */
static uint16_t *last;             /* the source picture last drawn */
static uint16_t *conv;             /* converted lines, the texture's copy */
static int tw, th, skipped, force = 1;
static uint32_t audio_frame_bytes;

unsigned long otb_frames_drawn, otb_frames_skipped, otb_lines_converted;

int otb_video_open(zx *m, const char *title, int scale, int fullscreen)
{
    SDL_AudioSpec want, have;
    int i;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) < 0 &&
        SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
        fprintf(stderr, "opentbblue: SDL: %s\n", SDL_GetError());
        return -1;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    win = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                           ZX_SCREEN_W * scale, ZX_SCREEN_H * scale,
                           SDL_WINDOW_RESIZABLE | (fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0));
    if (!win) {
        fprintf(stderr, "opentbblue: SDL: %s\n", SDL_GetError());
        return -1;
    }
    ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    if (!ren)
        ren = SDL_CreateRenderer(win, -1, 0);
    if (!ren) {
        fprintf(stderr, "opentbblue: SDL: %s\n", SDL_GetError());
        return -1;
    }
    /* The Next's picture is 640 wide (half-width pixels) and shows as 320x256. */
    SDL_RenderSetLogicalSize(ren, ZX_SCREEN_W, ZX_SCREEN_H);
    tw = m->screen_w;
    th = m->screen_h;
    tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, tw, th);
    last = calloc((size_t)tw * th, sizeof *last);
    conv = calloc((size_t)tw * th, sizeof *conv);
    if (!tex || !last || !conv) {
        fprintf(stderr, "opentbblue: cannot make the picture (%s)\n", SDL_GetError());
        return -1;
    }
    for (i = 0; i < 512; i++) {
        unsigned r = (unsigned)(i >> 6) & 7, g = (unsigned)(i >> 3) & 7, b = (unsigned)i & 7;
        lut[i] = (uint16_t)((r * 31 / 7) << 11 | (g * 63 / 7) << 5 | (b * 31 / 7));
    }

    SDL_zero(want);
    want.freq = ZX_AUDIO_RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    audio_frame_bytes = ZX_AUDIO_RATE / 50 * 4;
    if (dev)
        SDL_PauseAudioDevice(dev, 0);
    return 0;
}

void otb_video_title(const char *title)
{
    SDL_SetWindowTitle(win, title);
}

void otb_video_fullscreen(void)
{
    uint32_t f = SDL_GetWindowFlags(win);
    SDL_SetWindowFullscreen(win, (f & SDL_WINDOW_FULLSCREEN_DESKTOP) ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
    force = 1;
}

void otb_video_redraw(void)
{
    force = 1;
}

void otb_video_turbo(int on)
{
    if (!on && dev)
        SDL_ClearQueuedAudio(dev);
}

/* Convert the lines that changed; returns how many. y0/y1 bound them. */
static int convert(const zx *m, int *y0, int *y1)
{
    int y, x, n = 0;
    *y0 = th;
    *y1 = -1;
    for (y = 0; y < th; y++) {
        const uint16_t *src = m->pixels + (size_t)y * m->pitch;
        uint16_t *old = last + (size_t)y * tw;
        uint16_t *dst;
        if (!force && !memcmp(src, old, (size_t)tw * sizeof *src))
            continue;
        memcpy(old, src, (size_t)tw * sizeof *src);
        dst = conv + (size_t)y * tw;
        for (x = 0; x < tw; x++)
            dst[x] = lut[src[x] & 511];
        if (y < *y0) *y0 = y;
        *y1 = y;
        n++;
    }
    force = 0;
    return n;
}

void otb_video_frame(zx *m, int turbo)
{
    int late = 0, y0, y1;

    if (dev && !turbo) {
        Uint32 q;
        SDL_QueueAudio(dev, m->audio, (Uint32)m->audio_samples * 4);
        q = SDL_GetQueuedAudioSize(dev);
        /* Less than a frame of sound left: we are behind real time. */
        late = q < audio_frame_bytes;
        /* Keep about three frames queued: that paces the emulation to the
         * Next's own 50 Hz. */
        while (SDL_GetQueuedAudioSize(dev) > audio_frame_bytes * 3)
            SDL_Delay(1);
    } else if (!dev && !turbo) {
        static Uint32 next_tick;
        Uint32 now = SDL_GetTicks();
        if (!next_tick)
            next_tick = now;
        next_tick += 20;
        if ((int32_t)(next_tick - now) > 0)
            SDL_Delay(next_tick - now);
        else
            late = (int32_t)(now - next_tick) > 20;
    }

    if ((turbo && (m->frames % 10)) || (late && skipped < OTB_MAX_SKIP)) {
        skipped++;
        otb_frames_skipped++;
        return;
    }
    skipped = 0;
    otb_lines_converted += (unsigned long)convert(m, &y0, &y1);
    if (y1 < y0)
        return;     /* nothing changed: the window still shows this picture */
    {
        SDL_Rect r;
        r.x = 0;
        r.y = y0;
        r.w = tw;
        r.h = y1 - y0 + 1;
        SDL_UpdateTexture(tex, &r, conv + (size_t)y0 * tw, tw * 2);
    }
    SDL_RenderClear(ren);
    SDL_RenderCopy(ren, tex, NULL, NULL);
    SDL_RenderPresent(ren);
    otb_frames_drawn++;
}

void otb_video_close(void)
{
    if (dev)
        SDL_CloseAudioDevice(dev);
    free(last);
    free(conv);
    SDL_Quit();
}
