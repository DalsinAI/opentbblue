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

int otb_video_open(zx *m, const char *title, int scale, int fullscreen, int pinned);
void otb_video_title(const char *title);
void otb_video_fullscreen(void);
/* Window size one step (1x..6x) bigger (+1) or smaller (-1). */
void otb_video_scale(int step);
int otb_video_get_scale(void);
/* The control bar (bar.h): pinned under the picture, or auto-hidden. */
void otb_video_pin(int on);
int otb_video_pinned(void);
void otb_video_bar_state(int paused, int turbo, int muted);
/* The card button lit or not, and a note for the middle of the bar. */
void otb_video_bar_note(int lit, const char *note);
/* The panel over the picture (bar.h), or NULL to close it. */
struct otb_panel;
void otb_video_panel(const struct otb_panel *p);
int otb_video_panel_shown(void);
/* A line on the window while it starts (drawn now), or NULL to clear it. */
void otb_video_status(const char *line);
/* The pointer at logical x, y (inside: it is in the window). */
void otb_video_pointer(int x, int y, int inside);
/* A click at logical x, y: the bar button there, BAR_ACT + a panel
 * button's action, BAR_PANEL elsewhere on the panel, or BAR_NONE. */
int otb_video_click(int x, int y);
/* The window was resized by hand: work out the scale again. */
void otb_video_note_size(void);
/* While paused: keep the bar alive, about 50 times a second. */
void otb_video_idle(void);
void otb_video_turbo(int on);
/* The window was uncovered or resized: draw the whole picture next time. */
void otb_video_redraw(void);
/* After each emulated frame: queue its sound, wait for real time, draw it
 * (or skip it). */
void otb_video_frame(zx *m, int turbo);
void otb_video_close(void);

/* Counters for -b. */
extern unsigned long otb_frames_drawn, otb_frames_skipped, otb_lines_converted;
double otb_audio_rate(double seconds);
extern unsigned long otb_wait_ms, otb_slept_ms;
/* Milliseconds from the performance counter, worked out in 64 bits. */
unsigned long otb_ms(void);
double otb_counter_hz(void);
double otb_audio_dropped_s(void);

#endif
