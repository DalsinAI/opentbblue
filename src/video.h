/*
 * OpenTBBlue's window and sound (SDL2).
 *
 * Drawing is kept cheap, because on a 68040 every byte moved counts:
 * the picture goes to a 16-bit texture, only the lines that changed since
 * the last drawn frame are converted and uploaded, a frame with no change
 * is not uploaded at all, and when the emulation falls behind real time
 * frames are skipped (up to OTB_MAX_SKIP in a row) while the sound keeps
 * the pace.
 */
#ifndef OTB_VIDEO_H
#define OTB_VIDEO_H

#include "zx/zx.h"

#define OTB_MAX_SKIP 4

int otb_video_open(zx *m, const char *title, int scale, int fullscreen);
void otb_video_title(const char *title);
void otb_video_fullscreen(void);
void otb_video_turbo(int on);
/* The window was uncovered or resized: draw the whole picture next time. */
void otb_video_redraw(void);
/* After each emulated frame: queue its sound, wait for real time, draw it
 * (or skip it). */
void otb_video_frame(zx *m, int turbo);
void otb_video_close(void);

/* Counters for -b. */
extern unsigned long otb_frames_drawn, otb_frames_skipped, otb_lines_converted;

#endif
