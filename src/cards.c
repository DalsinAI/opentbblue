/*
 * The card panel (see cards.h).
 */
#include "cards.h"
#include "fetch.h"
#include "setup.h"

#include <SDL.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define MAX_CARDS 10

enum {
    ST_IDLE,        /* nothing asked yet */
    ST_CHECKING,    /* asking GitLab for the newest release */
    ST_LATEST,      /* SD0 has the newest */
    ST_NEWER,       /* a newer one is there to download */
    ST_DOWNLOADING,
    ST_READY,       /* downloaded: update SD0 or make a new card? */
    ST_WRITING,     /* writing it to a card (a thread) */
    ST_WRITTEN,     /* a new card is ready */
    ST_FAILED
};

enum {
    ACT_CHECK = 1,
    ACT_DOWNLOAD,
    ACT_STOP,
    ACT_UPDATE_SD0,
    ACT_NEW_CARD,
    ACT_EJECT_SD1,
    ACT_CLOSE,
    ACT_NEW_TO_SD0,
    ACT_NEW_TO_SD1,
    ACT_CARD_SD0 = 100,     /* + the card's number in the list */
    ACT_CARD_SD1 = 200
};

static otb_cards_host host;
static char slot_path[2][1200];
static int state, quiet;           /* quiet: the start's own check (no news, no noise) */
static otb_release newest, on_sd0;
static otb_dl *dl;
static char json_path[1200], tar[1200], part[1300], target[1200], target_name[80];
static char message[64];
static char note[64];
static int lit;

/* the write: its thread, and how it ended */
static SDL_Thread *writer;
static volatile int write_done, write_rc;
static int write_sd0;              /* writing over SD0's card (the Next waits) */

static char cards[MAX_CARDS][64];
static int ncards;

/* ---- the card folders in drives/ ---------------------------------------- */

static void card_path(char *out, size_t size, const char *name)
{
    char drives[1100];
    otb_path(drives, sizeof drives, OTB_DRIVES);
    snprintf(out, size, "%s/%s", drives, name);
}

static void rescan(void)
{
    char drives[1100];
    DIR *d;
    struct dirent *e;
    ncards = 0;
    otb_path(drives, sizeof drives, OTB_DRIVES);
    d = opendir(drives);
    if (!d)
        return;
    while ((e = readdir(d)) != NULL && ncards < MAX_CARDS) {
        char p[1200];
        struct stat st;
        if (e->d_name[0] == '.' || strlen(e->d_name) >= sizeof cards[0])
            continue;
        card_path(p, sizeof p, e->d_name);
        if (stat(p, &st) || !S_ISDIR(st.st_mode))
            continue;   /* .info icons and other files */
        snprintf(cards[ncards++], sizeof cards[0], "%s", e->d_name);
    }
    closedir(d);
    /* in name order */
    {
        int i, j;
        for (i = 1; i < ncards; i++)
            for (j = i; j > 0 && strcmp(cards[j - 1], cards[j]) > 0; j--) {
                char t[64];
                memcpy(t, cards[j], sizeof t);
                memcpy(cards[j], cards[j - 1], sizeof t);
                memcpy(cards[j - 1], t, sizeof t);
            }
    }
}

/* A card's name for the panel: the folder's own name. */
static const char *name_of(const char *path)
{
    const char *b = path, *q;
    for (q = path; *q; q++)
        if (*q == '/' || *q == '\\' || *q == ':')
            b = q + 1;
    return b;
}

/* The setting for a card: drives/<name> when it is one of ours. */
static void remember(int slot, const char *path)
{
    char drives[1100], v[1200];
    size_t n;
    otb_path(drives, sizeof drives, OTB_DRIVES);
    n = strlen(drives);
    if (path[0] && !strncmp(path, drives, n) && path[n] == '/')
        snprintf(v, sizeof v, "%s/%s", OTB_DRIVES, path + n + 1);
    else
        snprintf(v, sizeof v, "%s", path);
    otb_settings_put(slot ? "sd1" : "sd0", v);
}

static void put_in(int slot, const char *path)
{
    int other = !slot;
    if (!path[0])
        return;
    if (!strcmp(slot_path[slot], path))
        return;
    /* one card, one slot: both writing back to one folder would fight */
    if (!strcmp(slot_path[other], path)) {
        if (other == 0) {
            snprintf(message, sizeof message, "THAT CARD IS IN SD0");
            return;
        }
        host.eject(1);
        slot_path[1][0] = 0;
        remember(1, "");
    }
    if (slot == 0 && !otb_card_ready(path)) {
        snprintf(message, sizeof message, "%s HAS NO SYSTEM/NEXT ON IT", name_of(path));
        return;
    }
    if (host.insert(slot, path)) {
        snprintf(message, sizeof message, "CANNOT USE %s", name_of(path));
        return;
    }
    snprintf(slot_path[slot], sizeof slot_path[slot], "%s", path);
    remember(slot, path);
    message[0] = 0;
    if (slot == 0) {
        otb_card_release(path, &on_sd0);
        if (state == ST_LATEST || state == ST_NEWER)
            state = ST_IDLE;    /* worth asking again for this card */
    }
}

/* ---- the update --------------------------------------------------------- */

static void start_check(int q)
{
    otb_dl_free(dl);
    otb_path(json_path, sizeof json_path, "latest.json");
    dl = otb_dl_start(otb_release_api(), json_path, 0);
    quiet = q;
    message[0] = 0;
    state = dl ? ST_CHECKING : (q ? ST_IDLE : ST_FAILED);
    if (!dl && !q)
        snprintf(message, sizeof message, "NO NETWORK: IS IT UP?");
}

static void checked(int ok)
{
    static char json[8192];
    FILE *fp;
    size_t n = 0;
    int have;
    otb_dl_free(dl);
    dl = NULL;
    if (ok && (fp = fopen(json_path, "rb")) != NULL) {
        n = fread(json, 1, sizeof json - 1, fp);
        fclose(fp);
    }
    remove(json_path);
    json[n] = 0;
    if (!ok || otb_release_parse(json, &newest)) {
        state = quiet ? ST_IDLE : ST_FAILED;
        if (!quiet)
            snprintf(message, sizeof message, "NO ANSWER FROM GITLAB.COM: IS THE NETWORK UP?");
        return;
    }
    if (on_sd0.id[0] && !strcmp(on_sd0.id, newest.id)) {
        state = ST_LATEST;
        return;
    }
    have = otb_release_tar(&newest, tar, sizeof tar);
    state = have == 1 ? ST_READY : ST_NEWER;
}

static void start_download(void)
{
    char url[300];
    if (otb_release_tar(&newest, tar, sizeof tar) == 1) {
        state = ST_READY;
        return;
    }
    otb_release_url(&newest, url, sizeof url);
    snprintf(part, sizeof part, "%s.part", tar);
    otb_dl_free(dl);
    dl = otb_dl_start(url, part, 1);
    if (!dl) {
        state = ST_FAILED;
        snprintf(message, sizeof message, "NO NETWORK: IS IT UP?");
        return;
    }
    state = ST_DOWNLOADING;
}

static int write_thread(void *arg)
{
    (void)arg;
    write_rc = otb_release_install(&newest, target);
    write_done = 1;
    return 0;
}

static void start_write(int to_sd0)
{
    write_sd0 = to_sd0;
    if (to_sd0) {
        snprintf(target, sizeof target, "%s", slot_path[0]);
        snprintf(target_name, sizeof target_name, "%s", name_of(target));
        host.eject(0);  /* written back now, and left alone while it is written */
    } else {
        int i;
        /* SystemNext-2026-11-02, or -2, -3 when that is taken */
        for (i = 1; i < 100; i++) {
            struct stat st;
            if (i == 1)
                snprintf(target_name, sizeof target_name, "SystemNext-%.10s", newest.date[0] ? newest.date : newest.id);
            else
                snprintf(target_name, sizeof target_name, "SystemNext-%.10s-%d",
                         newest.date[0] ? newest.date : newest.id, i);
            card_path(target, sizeof target, target_name);
            if (stat(target, &st))
                break;
        }
    }
    write_done = 0;
    write_rc = -1;
    otb_prog.files = 0;
    writer = SDL_CreateThread(write_thread, "write card", NULL);
    if (!writer) {
        write_thread(NULL);
    }
    state = ST_WRITING;
}

static void written(void)
{
    if (writer) {
        SDL_WaitThread(writer, NULL);
        writer = NULL;
    }
    if (write_sd0) {
        host.insert(0, slot_path[0]);   /* the Next starts again on it */
        otb_card_release(slot_path[0], &on_sd0);
        if (write_rc) {
            state = ST_FAILED;
            snprintf(message, sizeof message, "WRITING SD0 FAILED (SEE THE MESSAGES)");
        } else {
            state = ST_LATEST;
            snprintf(message, sizeof message, "SD0 UPDATED");
        }
        return;
    }
    rescan();
    if (write_rc) {
        state = ST_FAILED;
        snprintf(message, sizeof message, "WRITING %s FAILED", target_name);
        return;
    }
    state = ST_WRITTEN;
    message[0] = 0;
}

/* ---- the program's side ------------------------------------------------- */

void otb_cards_init(const otb_cards_host *h, const char *sd0, const char *sd1, int check)
{
    host = *h;
    snprintf(slot_path[0], sizeof slot_path[0], "%s", sd0 ? sd0 : "");
    snprintf(slot_path[1], sizeof slot_path[1], "%s", sd1 ? sd1 : "");
    otb_card_release(slot_path[0], &on_sd0);
    rescan();
    if (check)
        start_check(1);
}

void otb_cards_opened(void)
{
    rescan();
    if (quiet && state == ST_CHECKING)
        quiet = 0;      /* the panel is open: say if it fails */
}

void otb_cards_tick(void)
{
    int rc;
    if (state == ST_CHECKING) {
        if ((rc = otb_dl_step(dl)) != 0)
            checked(rc > 0);
    } else if (state == ST_DOWNLOADING) {
        if ((rc = otb_dl_step(dl)) > 0) {
            otb_dl_free(dl);
            dl = NULL;
            if (otb_release_downloaded(&newest, part)) {
                state = ST_FAILED;
                snprintf(message, sizeof message, "CANNOT KEEP THE DOWNLOAD IN MEDIA/");
            } else
                state = ST_READY;
        } else if (rc < 0) {
            otb_dl_free(dl);
            dl = NULL;
            state = ST_FAILED;
            snprintf(message, sizeof message, "THE DOWNLOAD STOPPED (WHAT CAME IS KEPT)");
        }
    } else if (state == ST_WRITING && write_done) {
        written();
    }
    /* the bar */
    lit = state == ST_NEWER || state == ST_READY || state == ST_DOWNLOADING || state == ST_WRITING ||
          state == ST_WRITTEN;
    note[0] = 0;
    if (state == ST_NEWER)
        snprintf(note, sizeof note, "NEWER SYSTEM/NEXT: SEE THE CARD BUTTON");
    else if (state == ST_DOWNLOADING) {
        long long now, total;
        otb_dl_progress(dl, &now, &total);
        if (total > 0)
            snprintf(note, sizeof note, "SYSTEM/NEXT: %lld OF %lld MB", now / 1000000, total / 1000000);
        else
            snprintf(note, sizeof note, "SYSTEM/NEXT: %lld MB", now / 1000000);
    } else if (state == ST_READY)
        snprintf(note, sizeof note, "SYSTEM/NEXT DOWNLOADED: SEE THE CARD BUTTON");
    else if (state == ST_WRITING)
        snprintf(note, sizeof note, "WRITING %ld FILES TO %s", otb_prog.files, target_name);
}

int otb_cards_busy(void)
{
    return state == ST_WRITING && write_sd0;
}

int otb_cards_lit(void)
{
    return lit;
}

const char *otb_cards_note(void)
{
    return note;
}

static otb_panel_row *row(otb_panel *p)
{
    otb_panel_row *r;
    if (p->rows >= PANEL_ROWS)
        return &p->row[PANEL_ROWS - 1];
    r = &p->row[p->rows++];
    memset(r, 0, sizeof *r);
    return r;
}

static void button(otb_panel_row *r, int k, const char *label, int act)
{
    snprintf(r->btn[k], sizeof r->btn[k], "%s", label);
    r->act[k] = act;
}

void otb_cards_panel(otb_panel *p)
{
    otb_panel_row *r;
    int i, s;
    memset(p, 0, sizeof *p);

    /* System/Next */
    r = row(p);
    snprintf(r->text, sizeof r->text, "SYSTEM/NEXT ON SD0: %.10s", on_sd0.date[0] ? on_sd0.date : "NOT KNOWN");
    button(r, 1, "CLOSE", ACT_CLOSE);
    r = row(p);
    switch (state) {
    case ST_CHECKING:
        snprintf(r->text, sizeof r->text, "LOOKING FOR A NEWER ONE...");
        break;
    case ST_LATEST:
        snprintf(r->text, sizeof r->text, "%s", message[0] ? message : "THAT IS THE NEWEST");
        button(r, 1, "CHECK AGAIN", ACT_CHECK);
        break;
    case ST_NEWER:
        snprintf(r->text, sizeof r->text, "NEWER: %.10s (ABOUT 100 MB)", newest.date);
        button(r, 1, "DOWNLOAD", ACT_DOWNLOAD);
        break;
    case ST_DOWNLOADING: {
        long long now, total;
        otb_dl_progress(dl, &now, &total);
        if (total > 0)
            snprintf(r->text, sizeof r->text, "DOWNLOADING %.10s: %lld OF %lld MB", newest.date, now / 1000000,
                     total / 1000000);
        else
            snprintf(r->text, sizeof r->text, "DOWNLOADING %.10s: %lld MB", newest.date, now / 1000000);
        button(r, 1, "STOP", ACT_STOP);
        break;
    }
    case ST_READY:
        snprintf(r->text, sizeof r->text, "%.10s DOWNLOADED. PUT IT ON:", newest.date);
        button(r, 0, "SD0 (UPDATE)", ACT_UPDATE_SD0);
        button(r, 1, "A NEW CARD", ACT_NEW_CARD);
        break;
    case ST_WRITING:
        snprintf(r->text, sizeof r->text, "WRITING %ld FILES TO %s", otb_prog.files, target_name);
        break;
    case ST_WRITTEN:
        snprintf(r->text, sizeof r->text, "NEW CARD %s IS READY", target_name);
        button(r, 0, "PUT IN SD0", ACT_NEW_TO_SD0);
        button(r, 1, "PUT IN SD1", ACT_NEW_TO_SD1);
        break;
    case ST_FAILED:
        snprintf(r->text, sizeof r->text, "%s", message[0] ? message : "THAT DID NOT WORK");
        button(r, 1, "TRY AGAIN", ACT_CHECK);
        break;
    default:
        snprintf(r->text, sizeof r->text, "IS THERE A NEWER ONE?");
        button(r, 1, "CHECK", ACT_CHECK);
    }

    /* the slots */
    for (s = 0; s < 2; s++) {
        r = row(p);
        snprintf(r->text, sizeof r->text, "SD%d: %s", s, slot_path[s][0] ? name_of(slot_path[s]) : "EMPTY");
        if (s == 1 && slot_path[1][0])
            button(r, 1, "TAKE OUT", ACT_EJECT_SD1);
    }
    if (message[0] && state != ST_FAILED && state != ST_LATEST) {
        r = row(p);
        snprintf(r->text, sizeof r->text, "%s", message);
        r->dim = 1;
    }

    /* the cards in drives/ */
    r = row(p);
    snprintf(r->text, sizeof r->text, ncards ? "CARDS:" : "NO CARDS IN DRIVES/");
    r->dim = 1;
    for (i = 0; i < ncards; i++) {
        char path[1200];
        int in0, in1;
        card_path(path, sizeof path, cards[i]);
        in0 = !strcmp(path, slot_path[0]);
        in1 = !strcmp(path, slot_path[1]);
        r = row(p);
        snprintf(r->text, sizeof r->text, "  %s%s", cards[i], in0 ? "  (IN SD0)" : in1 ? "  (IN SD1)" : "");
        if (in0 || in1) {
            r->dim = 1;
            continue;
        }
        button(r, 0, "PUT IN SD0", ACT_CARD_SD0 + i);
        button(r, 1, "PUT IN SD1", ACT_CARD_SD1 + i);
    }
}

int otb_cards_action(int act)
{
    char path[1200];
    if (act == ACT_CLOSE)
        return 1;
    if (state == ST_WRITING)
        return 0;     /* nothing else until the card is written */
    if (act >= ACT_CARD_SD1 && act < ACT_CARD_SD1 + ncards) {
        card_path(path, sizeof path, cards[act - ACT_CARD_SD1]);
        put_in(1, path);
        return 0;
    }
    if (act >= ACT_CARD_SD0 && act < ACT_CARD_SD0 + ncards) {
        card_path(path, sizeof path, cards[act - ACT_CARD_SD0]);
        put_in(0, path);
        return 0;
    }
    switch (act) {
    case ACT_CHECK: start_check(0); break;
    case ACT_DOWNLOAD: start_download(); break;
    case ACT_STOP:
        otb_dl_free(dl);
        dl = NULL;
        state = ST_NEWER;   /* what came is kept, and carried on next time */
        break;
    case ACT_UPDATE_SD0: start_write(1); break;
    case ACT_NEW_CARD: start_write(0); break;
    case ACT_EJECT_SD1:
        host.eject(1);
        slot_path[1][0] = 0;
        remember(1, "");
        break;
    case ACT_NEW_TO_SD0:
        put_in(0, target);
        if (!strcmp(slot_path[0], target))
            state = ST_LATEST;
        break;
    case ACT_NEW_TO_SD1:
        put_in(1, target);
        if (!strcmp(slot_path[1], target))
            state = on_sd0.id[0] && !strcmp(on_sd0.id, newest.id) ? ST_LATEST : ST_READY;
        break;
    default: break;
    }
    return 0;
}
