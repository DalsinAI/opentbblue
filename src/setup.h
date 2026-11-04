/*
 * OpenTBBlue's own files on the host, and the first-run fetch of the
 * official Next software distribution (System/Next, gitlab.com/thesmog358/tbblue).
 *
 * Everything lives in one folder (otb_home):
 *
 *   sd/       the Next's SD card, as a plain host folder: the distribution
 *             unpacked, plus whatever the Next saves. The emulator shows it to
 *             the Next as a FAT32 card, the way AmiCradle gives each machine
 *             host folders for its drives.
 *   pi/       the Next's Raspberry Pi (NextPi) files.
 *   sd/.opentbblue  which distribution commit is unpacked (hidden from the Next).
 *
 * Nothing from the distribution is in OpenTBBlue's repository or releases:
 * it is downloaded from its own project when asked (or on first run).
 */
#ifndef OTB_SETUP_H
#define OTB_SETUP_H

#include <stddef.h>

/* The folder above, made if missing; NULL if it cannot be. */
const char *otb_home(void);

/* path = otb_home()/name. */
void otb_path(char *out, size_t size, const char *name);

/* sd holds an unpacked distribution (TBBLUE.FW and machines/next). */
int otb_card_ready(const char *sd);

/* Download the latest distribution and unpack it into sd. With update set,
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
