/*
 * OpenTBBlue's own files on the host, and the first-run fetch of the
 * official Next software distribution (System/Next, gitlab.com/thesmog358/tbblue).
 *
 * Everything lives in one folder (otb_home), laid out the way AmiCradle
 * keeps its machines:
 *
 *   opentbblue.ini  settings, written with the defaults on the first run.
 *   drives/SD0/     the Next's first SD card, as a plain host folder: the
 *                   distribution's files plus whatever the Next saves. The
 *                   emulator shows it to the Next as a FAT32 card, as
 *                   AmiCradle gives each machine host folders for its drives.
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
} otb_settings;

/* opentbblue.ini, written with the defaults when missing. */
void otb_settings_defaults(otb_settings *s);
int otb_settings_load(otb_settings *s);

/* A settings path: full paths as they are, others inside otb_home. */
void otb_resolve(char *out, size_t size, const char *path);

/* Move a card from an older layout (sd/) to drives/SD0. */
void otb_migrate(void);

/* An image for a drive: kept in media/, its files written into
 * drives/<name>/ (once: later calls find them there), and that folder's
 * path put in drive. 0 on success. */
int otb_import_image(const char *image, char *drive, size_t size);

/* The folder above, made if missing; NULL if it cannot be. */
const char *otb_home(void);

/* path = otb_home()/name. */
void otb_path(char *out, size_t size, const char *name);

/* sd holds an unpacked distribution (TBBLUE.FW and machines/next). */
int otb_card_ready(const char *sd);

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
