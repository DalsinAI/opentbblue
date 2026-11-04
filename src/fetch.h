/*
 * Downloads, with libcurl built into the program: HTTPS on every system
 * OpenTBBlue runs on, the progress counted as it comes in, and a broken
 * download carried on from where it stopped (its .part file is kept).
 *
 * A download runs a step at a time (otb_dl_step), so the window it is shown
 * in, or the Next, keeps going meanwhile; otb_dl_wait runs one to the end.
 * Everything is done on the caller's own thread: on AmigaOS a network
 * connection belongs to the task that opened it.
 */
#ifndef OTB_FETCH_H
#define OTB_FETCH_H

#include <stddef.h>

typedef struct otb_dl otb_dl;

/* Start fetching url into path. resume: carry on from a part already in
 * path (a server that cannot does it again from the start). NULL if it
 * cannot start (no network library, no file). */
otb_dl *otb_dl_start(const char *url, const char *path, int resume);

/* Move it on without waiting: 0 still going, 1 done, -1 failed (each
 * failure is tried again, from where it got to, three times first). */
int otb_dl_step(otb_dl *d);

/* Bytes so far (with any part it carried on from), and the whole size
 * (0 while the server has not said). */
void otb_dl_progress(const otb_dl *d, long long *now, long long *total);

/* What went wrong, for a message ("" if nothing). */
const char *otb_dl_error(const otb_dl *d);

/* Stop (the file stays as far as it got) and free it. */
void otb_dl_free(otb_dl *d);

/* Start, run to the end and free: 0 when done. */
int otb_dl_wait(const char *url, const char *path, int resume);

/* A small reply (an API answer) into buf, 0-terminated: 0 when done. */
int otb_http_text(const char *url, char *buf, size_t size);

#endif
