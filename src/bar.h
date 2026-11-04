/*
 * The control bar at the bottom of the window: power and reset, pause,
 * full speed, sound, window size, the cards, full screen, pin and quit.
 *
 * The card button opens a panel over the picture (cards.c): System/Next
 * updates, and which card is in each of the Next's two slots.
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
    BAR_QUIT,
    BAR_CARDS,
    BAR_PANEL       /* a click on the panel, not on a button there */
};
/* A click on a panel button: BAR_ACT + the button's action. */
#define BAR_ACT 1000

/* What the bar shows as on or off, and the window scale for its label. */
typedef struct otb_bar_state {
    int paused, turbo, muted, pinned, fullscreen, scale;
    int cards_lit;          /* the card button lit: something to look at */
    const char *note;       /* the middle of the bar when nothing else is */
} otb_bar_state;

/* Draw the bar for this state into pix (BAR_W x BAR_H, RGB565). */
void otb_bar_draw(uint16_t *pix, const otb_bar_state *st);

/* The pointer moved to x (bar coordinates; outside the bar: -1).
 * Returns 1 when the highlight changed, so the bar needs drawing. */
int otb_bar_hover(int x);

/* The button at x, or BAR_NONE. */
int otb_bar_hit(int x);

/* The panel: rows of text, each with up to two buttons on the right. */
#define PANEL_W 304
#define PANEL_ROW_H 9
#define PANEL_ROWS 14
typedef struct otb_panel_row {
    char text[64];
    char btn[2][24];        /* "" for none */
    int act[2];             /* what a click on each does (cards.h), above 0 */
    int dim;                /* the text greyed */
} otb_panel_row;
typedef struct otb_panel {
    int rows;
    otb_panel_row row[PANEL_ROWS];
} otb_panel;

static inline int otb_panel_h(const otb_panel *p)
{
    return p->rows * PANEL_ROW_H + 3;
}

/* Draw the panel into pix (PANEL_W x otb_panel_h), the button whose action
 * is hover lit. */
void otb_panel_draw(uint16_t *pix, const otb_panel *p, int hover);

/* The action of the button at x, y (panel coordinates); 0 for none. */
int otb_panel_hit(const otb_panel *p, int x, int y);

#endif
