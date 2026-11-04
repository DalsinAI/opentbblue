/*
 * OpenTBBlue's own files on the host, and the first-run fetch of the
 * official Next software distribution (System/Next, gitlab.com/thesmog358/tbblue).
 *
 * Everything lives in one folder (otb_home):
 *
 *   opentbblue.ini  settings, written with the defaults on the first run.
 *   drives/SD0/     the Next's first SD card, as a plain host folder: the
 *                   distribution's files plus whatever the Next saves. The
 *                   emulator shows it to the Next as a FAT32 card.
 *   drives/<name>/  more cards: an .img's files written into a folder.
 *   media/          the media repository: the official downloads (the
 *                   System/Next archive) and any .img given to OpenTBBlue,
 *                   kept as they came, each with a .json note of its source.
 *   pi/             the Next's Raspberry Pi (NextPi) files.
 *   drives/SD0/.opentbblue  which distribution commit is on the card.
 *
 * Nothing from the distribution is in OpenTBBlue's repository or releases:
 * it is downloaded from its own project when asked (or on first run).
 */
#ifndef OTB_SETUP_H
#define OTB_SETUP_H

#include <stddef.h>

#define OTB_MEDIA    "media"
#define OTB_DRIVES   "drives"
#define OTB_SETTINGS "opentbblue.ini"

typedef struct otb_settings {
    char sd0[1024], sd1[1024], pi[1024];
    int scale, fullscreen, fetch;
    int card_mb;              /* size of a folder card in MB, 0 to fit its files */
    int bar_pinned;           /* the control bar: 1 pinned, 0 auto-hide */
    int check;                /* look for a newer System/Next at start */
} otb_settings;

/* opentbblue.ini, written with the defaults when missing. */
void otb_settings_defaults(otb_settings *s);
int otb_settings_load(otb_settings *s);
/* Set key=value in opentbblue.ini (the line replaced, or added). */
int otb_settings_put(const char *key, const char *value);

/* A settings path: full paths as they are, others inside otb_home. */
void otb_resolve(char *out, size_t size, const char *path);

/* Move a card from an older layout (sd/) to drives/SD0. */
void otb_migrate(void);

/* An image for a drive: kept in media/, its files written into
 * drives/<name>/ (once: later calls find them there), and that folder's
 * path put in drive. 0 on success. */
int otb_import_image(const char *image, char *drive, size_t size);

/* How a fetch is going, for a window to show (firstrun.c). */
typedef struct otb_progress {
    volatile int phase;         /* 0 nothing, 1 looking up, 2 downloading, 3 writing files */
    volatile long files;        /* files written so far */
} otb_progress;
extern otb_progress otb_prog;

/* otb_fetch with a small window showing its progress (firstrun.c); without
 * a display it is otb_fetch. */
int otb_fetch_shown(const char *sd, int update);

/* The folder above, made if missing; NULL if it cannot be. */
const char *otb_home(void);

/* path = otb_home()/name. */
void otb_path(char *out, size_t size, const char *name);

/* AmigaOS: GlowIcons (PROGDIR:Icons/) for the drawers the program keeps,
 * where they have none yet: its home, drives/ and each card's drawer,
 * media/. Elsewhere nothing. */
void otb_drawer_icons(void);

/* sd holds an unpacked distribution (TBBLUE.FW and machines/next). */
int otb_card_ready(const char *sd);

/* A System/Next release: a commit on the distribution's master. */
typedef struct otb_release {
    char id[41], date[32];
} otb_release;

/* The newest release (asks GitLab's API; waits for the answer). */
int otb_latest(otb_release *r);
/* The API's address, and its answer read into r, for a lookup that does
 * not wait (fetch.c's otb_dl). */
const char *otb_release_api(void);
int otb_release_parse(const char *json, otb_release *r);
/* The release a card was written from (its .opentbblue). */
int otb_card_release(const char *sd, otb_release *r);
/* The archive's address. */
void otb_release_url(const otb_release *r, char *out, size_t size);
/* The archive's place in media/: 1 when it is there whole, 0 when not
 * (download it to tar.part), -1 when media/ cannot be made. */
int otb_release_tar(const otb_release *r, char *tar, size_t size);
/* A finished download (part): named as the archive, with its note. */
int otb_release_downloaded(const otb_release *r, const char *part);
/* Write the release's files into a card folder (made if missing; files of
 * the card's own are left alone), with its config.ini and .opentbblue. */
int otb_release_install(const otb_release *r, const char *sd);

/* Download the latest distribution (kept in media/; not downloaded again
 * when it is already there) and write its files into sd. With update set,
 * an existing card is refreshed in place: the distribution's files are
 * written over, and everything else on the card is left alone.
 * 0 on success. */
int otb_fetch(const char *sd, int update);

/* Unpack a .tar of the distribution (as GitLab serves it, one top folder)
 * into dir. 0 on success; files written are counted in *files. */
int otb_untar(const char *tar, const char *dir, long *files);

/* Write machines/next/config.ini with a video mode chosen, so the first
 * boot goes straight to NextZXOS instead of the monitor test card. Leaves an
 * existing config.ini alone. */
int otb_default_config(const char *sd);

/* mkdir -p */
int otb_mkdirs(const char *path);

#endif
