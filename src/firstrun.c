/*
 * The first run's window: while System/Next is fetched (about 100 MB) and
 * its files written to the card, a small window shows how far it has got,
 * with the step and the numbers in its title. Started from an icon there is
 * no terminal to watch, so without this the first run would look stuck.
 *
 * The download runs here, a step at a time between frames (fetch.c), and
 * counts its bytes as they come, so the bar moves on every system. Only
 * writing the files has a thread of its own.
 */
#include "setup.h"
#include "fetch.h"

#include <SDL.h>
#include <stdio.h>
#include <string.h>

#define EXPECT_BYTES 102000000.0   /* the archive's size, near enough */
#define EXPECT_FILES 1339.0

typedef struct job {
    const char *sd;
    otb_release r;
    int rc;
    volatile int done;
} job;

/* Writing the files: on its own thread, so the window keeps drawing. It
 * needs no network. */
static int install(void *arg)
{
    job *j = arg;
    j->rc = otb_release_install(&j->r, j->sd);
    j->done = 1;
    return 0;
}

static void fill(SDL_Renderer *r, int x, int y, int w, int h, Uint8 cr, Uint8 cg, Uint8 cb)
{
    SDL_Rect q;
    q.x = x;
    q.y = y;
    q.w = w;
    q.h = h;
    SDL_SetRenderDrawColor(r, cr, cg, cb, 255);
    SDL_RenderFillRect(r, &q);
}

/* One frame of the window: the title (when it changed) and the bar. */
static void draw(SDL_Window *win, SDL_Renderer *ren, const char *title, double f)
{
    static char last[160];
    int x, bar;
    if (f > 0.99)
        f = 0.99;
    if (f < 0)
        f = 0;
    if (strcmp(title, last)) {
        SDL_SetWindowTitle(win, title);
        snprintf(last, sizeof last, "%s", title);
    }
    /* a dark panel, the bar in the Next's four stripe colours */
    fill(ren, 0, 0, 480, 72, 24, 24, 32);
    fill(ren, 20, 26, 440, 20, 60, 60, 72);
    bar = (int)(436 * f);
    for (x = 0; x < bar; x++) {
        int band = x * 4 / 436;
        static const Uint8 c[4][3] = { { 220, 0, 0 }, { 230, 200, 0 }, { 0, 200, 0 }, { 0, 200, 220 } };
        fill(ren, 22 + x, 28, 1, 16, c[band][0], c[band][1], c[band][2]);
    }
    SDL_RenderPresent(ren);
}

/* Events while it works: 1 when the window was closed. */
static int closed(void)
{
    SDL_Event e;
    int q = 0;
    while (SDL_PollEvent(&e))
        if (e.type == SDL_QUIT || (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_CLOSE))
            q = 1;
    return q;
}

/* Run a download a step at a time, the window drawn as it goes: 0 done,
 * -1 failed or closed (what came is kept, and carried on next time). */
static int download(SDL_Window *win, SDL_Renderer *ren, otb_dl *d, int big)
{
    char title[160];
    int rc;
    while ((rc = otb_dl_step(d)) == 0) {
        long long now, total;
        double f = 0;
        if (closed())
            return -1;
        otb_dl_progress(d, &now, &total);
        if (big) {
            double all = total > 0 ? (double)total : EXPECT_BYTES;
            f = 0.85 * (now / all);
            if (total > 0)
                snprintf(title, sizeof title, "OpenTBBlue: downloading System/Next, %lld of %lld MB",
                         now / 1000000, total / 1000000);
            else
                snprintf(title, sizeof title, "OpenTBBlue: downloading System/Next, %lld MB of about 100",
                         now / 1000000);
        } else {
            snprintf(title, sizeof title, "OpenTBBlue: looking up the latest System/Next");
        }
        draw(win, ren, title, f);
        SDL_Delay(20);
    }
    if (rc < 0)
        fprintf(stderr, "opentbblue: %s\n", otb_dl_error(d));
    return rc > 0 ? 0 : -1;
}

/* The lookup, without waiting on it: the newest release into r. */
static int lookup(SDL_Window *win, SDL_Renderer *ren, otb_release *r)
{
    char tmp[1100];
    static char json[8192];
    otb_dl *d;
    FILE *fp;
    size_t n;
    int rc;
    otb_path(tmp, sizeof tmp, "latest.json");
    if (!(d = otb_dl_start(otb_release_api(), tmp, 0)))
        return -1;
    rc = download(win, ren, d, 0);
    otb_dl_free(d);
    if (rc)
        return -1;
    fp = fopen(tmp, "rb");
    if (!fp)
        return -1;
    n = fread(json, 1, sizeof json - 1, fp);
    fclose(fp);
    remove(tmp);
    json[n] = 0;
    return otb_release_parse(json, r);
}

int otb_fetch_shown(const char *sd, int update)
{
    SDL_Window *win;
    SDL_Renderer *ren;
    SDL_Thread *th;
    job j;
    otb_release have;
    char title[160], tar[1200], part[1300], url[300];
    int got, rc = -1;

    if (otb_card_ready(sd) && !update)
        return 0;
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) < 0)
        return otb_fetch(sd, update);
    win = SDL_CreateWindow("OpenTBBlue", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 480, 72, 0);
    ren = win ? SDL_CreateRenderer(win, -1, 0) : NULL;
    if (!ren) {
        if (win)
            SDL_DestroyWindow(win);
        return otb_fetch(sd, update);
    }
    j.sd = sd;
    j.rc = -1;
    j.done = 0;
    draw(win, ren, "OpenTBBlue: looking up the latest System/Next", 0);
    if (lookup(win, ren, &j.r)) {
        fprintf(stderr, "opentbblue: cannot reach gitlab.com/thesmog358/tbblue (is the network up?)\n");
        goto out;
    }
    if (update && otb_card_release(sd, &have) == 0 && !strcmp(have.id, j.r.id)) {
        printf("OpenTBBlue: the SD card already has the latest distribution (%.10s, %s)\n", j.r.id, j.r.date);
        rc = 0;
        goto out;
    }
    if ((got = otb_release_tar(&j.r, tar, sizeof tar)) < 0)
        goto out;
    if (!got) {
        otb_dl *d;
        otb_release_url(&j.r, url, sizeof url);
        snprintf(part, sizeof part, "%s.part", tar);
        printf("OpenTBBlue: downloading System/Next %.10s (%s), about 100 MB...\n", j.r.id, j.r.date);
        if (!(d = otb_dl_start(url, part, 1)))
            goto out;
        got = download(win, ren, d, 1);
        otb_dl_free(d);
        if (got || otb_release_downloaded(&j.r, part)) {
            fprintf(stderr, "opentbblue: the download did not finish (what came is kept, and carried on next time)\n");
            goto out;
        }
    }
    th = SDL_CreateThread(install, "install", &j);
    if (!th) {
        rc = otb_release_install(&j.r, sd);
        goto out;
    }
    while (!j.done) {
        closed();   /* closing waits for the files: a half-written card is no use */
        snprintf(title, sizeof title, "OpenTBBlue: writing %ld files to the SD card", otb_prog.files);
        draw(win, ren, title, 0.85 + 0.15 * (otb_prog.files / EXPECT_FILES));
        SDL_Delay(100);
    }
    SDL_WaitThread(th, NULL);
    rc = j.rc;
out:
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    return rc;
}
