/*
 * The first run's window: while System/Next is fetched (about 100 MB) and
 * its files written to the card, a small window shows how far it has got,
 * with the step and the numbers in its title. Started from an icon there is
 * no terminal to watch, so without this the first run would look stuck.
 */
#include "setup.h"

#include <SDL.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define EXPECT_BYTES 102000000.0   /* the archive's size, near enough */
#define EXPECT_FILES 1339.0

typedef struct job {
    const char *sd;
    int update, rc;
    volatile int done;
} job;

static int run(void *arg)
{
    job *j = arg;
    j->rc = otb_fetch(j->sd, j->update);
    j->done = 1;
    return 0;
}

#if defined(__AMIGA__) || defined(__amigaos__)
#include <proto/dos.h>
#include <proto/exec.h>
#include <dos/dos.h>

/* The download's size so far. curl holds the file open for writing, which
 * on AmigaOS locks it, so stat() fails; its drawer can still be read, and
 * the entry there carries the size. */
static long long part_size(void)
{
    char dir[1300];
    const char *name, *p;
    long long size = 0;
    struct FileInfoBlock *fib;
    BPTR lock;
    if (!otb_prog.part[0])
        return 0;
    name = otb_prog.part;
    for (p = otb_prog.part; *p; p++)
        if (*p == '/' || *p == ':')
            name = p + 1;
    snprintf(dir, sizeof dir, "%.*s", (int)(name - otb_prog.part), otb_prog.part);
    if (!(lock = Lock((CONST_STRPTR)dir, ACCESS_READ)))
        return 0;
    if ((fib = AllocDosObject(DOS_FIB, NULL)) != NULL) {
        if (Examine(lock, fib))
            while (ExNext(lock, fib))
                if (!strcmp((const char *)fib->fib_FileName, name)) {
                    size = (long long)(unsigned long)fib->fib_Size;
                    break;
                }
        FreeDosObject(DOS_FIB, fib);
    }
    UnLock(lock);
    return size;
}
#else
static long long part_size(void)
{
    struct stat st;
    return otb_prog.part[0] && stat(otb_prog.part, &st) == 0 ? (long long)st.st_size : 0;
}
#endif

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

int otb_fetch_shown(const char *sd, int update)
{
    SDL_Window *win;
    SDL_Renderer *ren;
    SDL_Thread *th;
    job j;
    char title[160], last[160] = "";

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
    j.update = update;
    j.rc = -1;
    j.done = 0;
    th = SDL_CreateThread(run, "fetch", &j);
    if (!th) {
        SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(win);
        return otb_fetch(sd, update);
    }
    while (!j.done) {
        SDL_Event e;
        double f = 0;
        int x, bar;
        while (SDL_PollEvent(&e))
            ;   /* closing waits for the fetch: a half-written card is no use */
        switch (otb_prog.phase) {
        case 2: {
            long long b = part_size();
            f = 0.85 * (b / EXPECT_BYTES);
            snprintf(title, sizeof title, "OpenTBBlue: downloading System/Next, %lld MB of about 100",
                     b / 1000000);
            break;
        }
        case 3:
            f = 0.85 + 0.15 * (otb_prog.files / EXPECT_FILES);
            snprintf(title, sizeof title, "OpenTBBlue: writing %ld files to the SD card", otb_prog.files);
            break;
        default:
            snprintf(title, sizeof title, "OpenTBBlue: looking up the latest System/Next");
        }
        if (f > 0.99)
            f = 0.99;
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
        SDL_Delay(100);
    }
    SDL_WaitThread(th, NULL);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    return j.rc;
}
