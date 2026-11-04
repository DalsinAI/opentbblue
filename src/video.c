/*
 * OpenTBBlue's window and sound (see video.h).
 */
#include "video.h"
#include "bar.h"

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

/* the control bar */
static SDL_Texture *bar_tex;
static uint16_t bar_pix[BAR_W * BAR_H];
static otb_bar_state bst;
static int bar_dirty = 1;          /* bar_pix needs drawing */
static int bar_shown;              /* rows showing while auto-hidden: 0..BAR_H */
static int bar_near;               /* the pointer is at the bottom edge or on the bar */
static Uint32 bar_left;            /* when it left */
static void present(void);

/* the panel over the picture (the card button's) */
#define PANEL_MAX_H (PANEL_ROWS * PANEL_ROW_H + 3)
static SDL_Texture *panel_tex;
static uint16_t panel_pix[PANEL_W * PANEL_MAX_H];
static otb_panel panel;
static int panel_on, panel_dirty, panel_hover;
static char bar_note[64];
static int16_t *silence;
static size_t silence_bytes;

/* The bar has its own rows under the picture: pinned and in a window. */
static int bar_below(void)
{
    return bst.pinned && !bst.fullscreen;
}

/* Frames still to draw whole after the window changed. A full-screen switch
 * opens a new screen and window (on AmigaOS, SDL2.module), and a frame drawn
 * before that has settled is lost; a still picture would then never be
 * drawn again. */
static int settle;

static void set_logical(void)
{
    SDL_RenderSetLogicalSize(ren, ZX_SCREEN_W, ZX_SCREEN_H + (bar_below() ? BAR_H : 0));
    force = 1;
    bar_dirty = 1;
    panel_dirty = 1;
    settle = 50;
}

#if defined(__AMIGA__) || defined(__amigaos__)
static int amiga_start_big;
void otb_video_fullscreen(void);
#endif

static void size_window(void)
{
    if (!bst.fullscreen)
        SDL_SetWindowSize(win, ZX_SCREEN_W * bst.scale, (ZX_SCREEN_H + (bar_below() ? BAR_H : 0)) * bst.scale);
}

unsigned long otb_frames_drawn, otb_frames_skipped, otb_lines_converted;

int otb_video_open(zx *m, const char *title, int scale, int fullscreen, int pinned)
{
    SDL_AudioSpec want, have;
    int i;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) < 0 &&
        SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
        fprintf(stderr, "opentbblue: SDL: %s\n", SDL_GetError());
        return -1;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    bst.scale = scale;
    bst.pinned = pinned;
    bst.fullscreen = fullscreen;
#if defined(__AMIGA__) || defined(__amigaos__)
    /* full screen is a big window there (otb_video_fullscreen) */
    amiga_start_big = fullscreen;
    fullscreen = 0;
    bst.fullscreen = 0;
#endif
    win = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                           ZX_SCREEN_W * scale, (ZX_SCREEN_H + (bar_below() ? BAR_H : 0)) * scale,
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
    set_logical();
    tw = m->screen_w;
    th = m->screen_h;
    tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, tw, th);
    bar_tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, BAR_W, BAR_H);
    panel_tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, PANEL_W, PANEL_MAX_H);
    last = calloc((size_t)tw * th, sizeof *last);
    conv = calloc((size_t)tw * th, sizeof *conv);
    if (!tex || !bar_tex || !panel_tex || !last || !conv) {
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
#if defined(__AMIGA__) || defined(__amigaos__)
    if (amiga_start_big)
        otb_video_fullscreen();
#endif
    return 0;
}

void otb_video_title(const char *title)
{
    SDL_SetWindowTitle(win, title);
}

#if defined(__AMIGA__) || defined(__amigaos__)
/* AmigaOS: full screen is the window at the biggest size the Workbench
 * screen holds, in the middle of it. SDL2.module's own full-screen screen
 * showed the picture blown up past the screen (only its grey corner), so
 * the window stays on the Workbench screen; F11 or the button again puts it
 * back at the size it had. */
static int amiga_big, amiga_scale;

void otb_video_fullscreen(void)
{
    SDL_DisplayMode dm;
    int s, h = ZX_SCREEN_H + BAR_H;
    if (amiga_big) {
        amiga_big = 0;
        bst.scale = amiga_scale;
        size_window();
        SDL_SetWindowPosition(win, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
        bar_dirty = 1;
        force = 1;
        settle = 50;
        return;
    }
    /* the screen's size, by whichever call SDL2.module answers */
    {
        SDL_Rect r;
        int i = SDL_GetWindowDisplayIndex(win);
        if (i < 0)
            i = 0;
        dm.w = dm.h = 0;
        if (!SDL_GetDisplayUsableBounds(i, &r) && r.w > 0 && r.h > 0) {
            dm.w = r.w;
            dm.h = r.h;
        } else if (!SDL_GetDisplayBounds(i, &r) && r.w > 0 && r.h > 0) {
            dm.w = r.w;
            dm.h = r.h;
        } else if (SDL_GetCurrentDisplayMode(i, &dm) || dm.w <= 0 || dm.h <= 0) {
            if (SDL_GetDesktopDisplayMode(i, &dm) || dm.w <= 0 || dm.h <= 0)
                return;
        }
    }
    /* as big as fits, keeping the shape; room for the window's borders
     * and the screen's title bar */
    {
        double fw = (double)(dm.w - 16) / ZX_SCREEN_W, fh = (double)(dm.h - 40) / h;
        double f = fw < fh ? fw : fh;
        if (f < 1)
            f = 1;
        amiga_scale = bst.scale;
        amiga_big = 1;
        s = (int)f;
        bst.scale = s;
        SDL_SetWindowSize(win, (int)(ZX_SCREEN_W * f), (int)(h * f));
        SDL_SetWindowPosition(win, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
        bar_dirty = 1;
        force = 1;
        settle = 50;
        fprintf(stderr, "opentbblue: full screen: %dx%d screen, window %dx%d\n", dm.w, dm.h,
                (int)(ZX_SCREEN_W * f), (int)(h * f));
    }
}
#else
void otb_video_fullscreen(void)
{
    uint32_t f = SDL_GetWindowFlags(win);
    bst.fullscreen = !(f & SDL_WINDOW_FULLSCREEN_DESKTOP);
    SDL_SetWindowFullscreen(win, bst.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    set_logical();
    if (!bst.fullscreen)
        size_window();
}
#endif

void otb_video_scale(int step)
{
    int s = bst.scale + step;
    if (bst.fullscreen || s < 1 || s > 6)
        return;
    bst.scale = s;
    size_window();
    bar_dirty = 1;
}

int otb_video_get_scale(void)
{
    return bst.scale;
}

void otb_video_pin(int on)
{
    bst.pinned = on;
    bar_shown = 0;
    set_logical();
    size_window();
}

int otb_video_pinned(void)
{
    return bst.pinned;
}

void otb_video_bar_state(int paused, int turbo, int muted)
{
    if (bst.paused != paused || bst.turbo != turbo || bst.muted != muted) {
        bst.paused = paused;
        bst.turbo = turbo;
        bst.muted = muted;
        bar_dirty = 1;
    }
}

void otb_video_bar_note(int lit, const char *note)
{
    if (!note)
        note = "";
    if (bst.cards_lit != lit || strcmp(bar_note, note)) {
        bst.cards_lit = lit;
        snprintf(bar_note, sizeof bar_note, "%s", note);
        bst.note = bar_note[0] ? bar_note : NULL;
        bar_dirty = 1;
    }
}

/* The panel's place: centred, its foot on the bar (or where the hidden
 * bar comes up). */
static int panel_x(void)
{
    return (ZX_SCREEN_W - PANEL_W) / 2;
}

static int panel_y(void)
{
    return ZX_SCREEN_H - (bar_below() ? 0 : BAR_H) - otb_panel_h(&panel);
}

static int in_panel(int x, int y)
{
    return panel_on && x >= panel_x() && x < panel_x() + PANEL_W && y >= panel_y() &&
           y < panel_y() + otb_panel_h(&panel);
}

void otb_video_panel(const otb_panel *p)
{
    if (!p) {
        if (panel_on) {
            panel_on = 0;
            panel_hover = 0;
            force = 1;      /* uncover the picture under it */
        }
        return;
    }
    if (!panel_on || memcmp(&panel, p, sizeof panel)) {
        if (panel_on && otb_panel_h(p) < otb_panel_h(&panel))
            force = 1;
        panel = *p;
        if (panel.rows > PANEL_ROWS)
            panel.rows = PANEL_ROWS;
        panel_on = 1;
        panel_dirty = 1;
    }
}

void otb_video_status(const char *line)
{
    otb_panel p;
    if (!line) {
        otb_video_panel(NULL);
        return;
    }
    memset(&p, 0, sizeof p);
    p.rows = 1;
    snprintf(p.row[0].text, sizeof p.row[0].text, "%s", line);
    otb_video_panel(&p);
    SDL_PumpEvents();
    force = 1;
    present();
}

int otb_video_panel_shown(void)
{
    return panel_on;
}

/* Which bar row the logical y is on, or -1 when the bar isn't there. */
static int bar_row(int y)
{
    int top = bar_below() ? ZX_SCREEN_H : ZX_SCREEN_H - bar_shown;
    if (!bar_below() && !bar_shown)
        return -1;
    return y >= top && y < top + (bar_below() ? BAR_H : bar_shown) ? y - top : -1;
}

void otb_video_pointer(int x, int y, int inside)
{
    int on;
    if (!inside) {
        x = y = -1;
    }
    on = bar_row(y) >= 0;
    if (otb_bar_hover(on ? x : -1))
        bar_dirty = 1;
    {
        int h = in_panel(x, y) ? otb_panel_hit(&panel, x - panel_x(), y - panel_y()) : 0;
        if (h != panel_hover) {
            panel_hover = h;
            panel_dirty = 1;
        }
    }
    if (!bar_below()) {
        /* the bottom edge brings it up; it goes a moment after the pointer
         * leaves (it stays while the panel is open) */
        int near = panel_on || (inside && (on || y >= ZX_SCREEN_H - 3));
        if (near)
            bar_near = 1;
        else if (bar_near) {
            bar_near = 0;
            bar_left = SDL_GetTicks();
        }
    }
}

int otb_video_click(int x, int y)
{
    if (in_panel(x, y)) {
        int a = otb_panel_hit(&panel, x - panel_x(), y - panel_y());
        return a > 0 ? BAR_ACT + a : BAR_PANEL;
    }
    if (bar_row(y) < 0)
        return BAR_NONE;
    return otb_bar_hit(x);
}

void otb_video_note_size(void)
{
    int w, h;
    SDL_GetWindowSize(win, &w, &h);
    if (!bst.fullscreen && w >= ZX_SCREEN_W) {
        int s = (w + ZX_SCREEN_W / 2) / ZX_SCREEN_W;
        if (s != bst.scale) {
            bst.scale = s;
            bar_dirty = 1;
        }
    }
}

/* Slide the auto-hidden bar one step; 1 when it moved. */
static int bar_step(void)
{
    int want = 0;
    if (bar_below())
        return 0;
    if (bar_near || (bar_left && SDL_GetTicks() - bar_left < 1000))
        want = BAR_H;
    if (want > bar_shown) {
        bar_shown += 3;
        if (bar_shown > want) bar_shown = want;
        return 1;
    }
    if (want < bar_shown) {
        bar_shown -= 2;
        if (bar_shown < want) bar_shown = want;
        if (!bar_shown)
            force = 1;      /* uncover the picture under it */
        return 1;
    }
    return 0;
}

/* Draw the picture and the bar. */
static void present(void)
{
    SDL_Rect d, sr;
    if (bar_dirty) {
        otb_bar_draw(bar_pix, &bst);
        SDL_UpdateTexture(bar_tex, NULL, bar_pix, BAR_W * 2);
        bar_dirty = 0;
    }
    SDL_RenderClear(ren);
    d.x = 0; d.y = 0; d.w = ZX_SCREEN_W; d.h = ZX_SCREEN_H;
    SDL_RenderCopy(ren, tex, NULL, &d);
    if (bar_below()) {
        d.y = ZX_SCREEN_H; d.h = BAR_H;
        SDL_RenderCopy(ren, bar_tex, NULL, &d);
    } else if (bar_shown) {
        sr.x = 0; sr.y = 0; sr.w = BAR_W; sr.h = bar_shown;
        d.y = ZX_SCREEN_H - bar_shown; d.h = bar_shown;
        SDL_RenderCopy(ren, bar_tex, &sr, &d);
    }
    if (panel_on) {
        int h = otb_panel_h(&panel);
        if (panel_dirty) {
            otb_panel_draw(panel_pix, &panel, panel_hover);
            sr.x = 0; sr.y = 0; sr.w = PANEL_W; sr.h = h;
            SDL_UpdateTexture(panel_tex, &sr, panel_pix, PANEL_W * 2);
            panel_dirty = 0;
        }
        sr.x = 0; sr.y = 0; sr.w = PANEL_W; sr.h = h;
        d.x = panel_x(); d.y = panel_y(); d.w = PANEL_W; d.h = h;
        SDL_RenderCopy(ren, panel_tex, &sr, &d);
    }
    SDL_RenderPresent(ren);
}

/* While the window settles, the whole picture each frame. */
static void settling(void)
{
    if (settle > 0) {
        settle--;
        force = 1;
        bar_dirty = 1;
        panel_dirty = panel_on;
    }
}

void otb_video_idle(void)
{
    int moved;
    settling();
    moved = bar_step();
    SDL_Delay(20);
    if (moved || bar_dirty || panel_dirty || force) {
        force = 0;
        present();
    }
}

void otb_video_redraw(void)
{
    force = 1;
    if (settle < 10)
        settle = 10;
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
        if (bst.muted) {
            size_t n = (size_t)m->audio_samples * 4;
            if (n > silence_bytes) {
                free(silence);
                silence = calloc(1, n);
                silence_bytes = silence ? n : 0;
            }
            if (silence)
                SDL_QueueAudio(dev, silence, (Uint32)n);
        } else
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
    settling();
    {
        int moved = bar_step();
        otb_lines_converted += (unsigned long)convert(m, &y0, &y1);
        if (y1 < y0 && !moved && !bar_dirty && !panel_dirty)
            return;     /* nothing changed: the window still shows this picture */
    }
    if (y1 >= y0) {
        SDL_Rect r;
        r.x = 0;
        r.y = y0;
        r.w = tw;
        r.h = y1 - y0 + 1;
        SDL_UpdateTexture(tex, &r, conv + (size_t)y0 * tw, tw * 2);
    }
    present();
    otb_frames_drawn++;
}

void otb_video_close(void)
{
    if (dev)
        SDL_CloseAudioDevice(dev);
    free(last);
    free(conv);
    free(silence);
    SDL_Quit();
}
