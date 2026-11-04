/*
 * The control bar at the bottom of the window: power and reset, pause,
 * full speed, sound, window size, full screen, pin and quit.
 *
 * Pinned, it sits under the picture and the window is that much taller.
 * Not pinned, it is hidden and slides up over the bottom of the picture
 * when the pointer comes to the bottom edge, and away again a moment after
 * the pointer leaves. In full screen it always works that way.
 *
 * It is drawn by hand into its own small 16-bit picture (BAR_W x BAR_H),
 * so it needs nothing from SDL but a texture, and costs nothing while it
 * does not change.
 */
#ifndef OTB_BAR_H
#define OTB_BAR_H

#include <stdint.h>

#define BAR_W 320
#define BAR_H 12

enum {
    BAR_NONE,
    BAR_HARD_RESET,
    BAR_SOFT_RESET,
    BAR_PAUSE,
    BAR_TURBO,
    BAR_MUTE,
    BAR_SMALLER,
    BAR_BIGGER,
    BAR_FULLSCREEN,
    BAR_PIN,
    BAR_QUIT
};

/* What the bar shows as on or off, and the window scale for its label. */
typedef struct otb_bar_state {
    int paused, turbo, muted, pinned, fullscreen, scale;
} otb_bar_state;

/* Draw the bar for this state into pix (BAR_W x BAR_H, RGB565). */
void otb_bar_draw(uint16_t *pix, const otb_bar_state *st);

/* The pointer moved to x (bar coordinates; outside the bar: -1).
 * Returns 1 when the highlight changed, so the bar needs drawing. */
int otb_bar_hover(int x);

/* The button at x, or BAR_NONE. */
int otb_bar_hit(int x);

#endif
