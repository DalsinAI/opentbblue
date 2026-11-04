/*
 * The control bar (see bar.h).
 */
#include "bar.h"

#if defined(__AMIGA__) || defined(__amigaos__)
#define FULL_TIP "FULL SCREEN  HELP"   /* an Amiga keyboard stops at F10 */
#else
#define FULL_TIP "FULL SCREEN  F11"
#endif

#include <string.h>

#define RGB(r, g, b) (uint16_t)(((r) >> 3) << 11 | ((g) >> 2) << 5 | ((b) >> 3))

static const uint16_t C_BG = RGB(0x1c, 0x22, 0x2e);
static const uint16_t C_EDGE = RGB(0x3c, 0x5c, 0x8c);
static const uint16_t C_HOVER = RGB(0x34, 0x44, 0x5c);
static const uint16_t C_ICON = RGB(0xd0, 0xd8, 0xe4);
static const uint16_t C_ON = RGB(0x5c, 0xb8, 0xff);
static const uint16_t C_DIM = RGB(0x70, 0x7c, 0x8c);
static const uint16_t C_TEXT = RGB(0xff, 0xff, 0xff);

/* 8x8 icons, top row first, bit 7 on the left */
static const uint8_t ic_power[8] = { 0x18, 0x5a, 0x99, 0x99, 0x81, 0x81, 0x42, 0x3c };
static const uint8_t ic_reset[8] = { 0x3a, 0x46, 0x8e, 0x80, 0x81, 0x81, 0x42, 0x3c };
static const uint8_t ic_play[8] = { 0x40, 0x60, 0x70, 0x78, 0x78, 0x70, 0x60, 0x40 };
static const uint8_t ic_pause[8] = { 0x00, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x00 };
static const uint8_t ic_turbo[8] = { 0x00, 0x88, 0xcc, 0xee, 0xff, 0xee, 0xcc, 0x88 };
static const uint8_t ic_sound[8] = { 0x08, 0x1a, 0x79, 0x79, 0x79, 0x79, 0x1a, 0x08 };
static const uint8_t ic_muted[8] = { 0x08, 0x18, 0x7d, 0x7a, 0x7a, 0x7d, 0x18, 0x08 };
static const uint8_t ic_minus[8] = { 0x00, 0x00, 0x00, 0x7e, 0x7e, 0x00, 0x00, 0x00 };
static const uint8_t ic_plus[8] = { 0x00, 0x18, 0x18, 0x7e, 0x7e, 0x18, 0x18, 0x00 };
static const uint8_t ic_full[8] = { 0xe7, 0x81, 0x81, 0x00, 0x00, 0x81, 0x81, 0xe7 };
static const uint8_t ic_pin[8] = { 0x3c, 0x3c, 0x3c, 0x7e, 0xff, 0x18, 0x18, 0x18 };
static const uint8_t ic_quit[8] = { 0x81, 0x42, 0x24, 0x18, 0x18, 0x24, 0x42, 0x81 };
static const uint8_t ic_card[8] = { 0x3e, 0x6a, 0xea, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe };

/* 3x5 letters for the labels: five rows of three bits */
static const struct { char c; const char *rows; } font[] = {
    { 'A', "010101111101101" }, { 'B', "110101110101110" }, { 'C', "011100100100011" },
    { 'D', "110101101101110" }, { 'E', "111100110100111" }, { 'F', "111100110100100" },
    { 'G', "011100101101011" }, { 'H', "101101111101101" }, { 'I', "111010010010111" },
    { 'J', "001001001101010" }, { 'K', "101101110101101" }, { 'L', "100100100100111" },
    { 'M', "101111111101101" }, { 'N', "110101101101101" }, { 'O', "010101101101010" },
    { 'P', "110101110100100" }, { 'Q', "010101101110011" }, { 'R', "110101110101101" },
    { 'S', "011100010001110" }, { 'T', "111010010010010" }, { 'U', "101101101101111" },
    { 'V', "101101101101010" }, { 'W', "101101111111101" }, { 'X', "101101010101101" },
    { 'Y', "101101010010010" }, { 'Z', "111001010100111" },
    { '0', "111101101101111" }, { '1', "010110010010111" }, { '2', "110001010100111" },
    { '3', "110001010001110" }, { '4', "101101111001001" }, { '5', "111100110001110" },
    { '6', "011100111101111" }, { '7', "111001010010010" }, { '8', "111101111101111" },
    { '9', "111101111001110" }, { '-', "000000111000000" }, { ' ', "000000000000000" },
    { ':', "000010000010000" }, { '/', "001001010100100" }, { '.', "000000000000010" },
    { '(', "010100100100010" }, { ')', "010001001001010" }, { ',', "000000000010100" },
    { '?', "110001010000010" }, { '_', "000000000000111" }, { '+', "000010111010000" },
};

/* The buttons: where they are, and what the label says on hover */
typedef struct { int id, x; const char *name; } button;

static const button buttons[] = {
    { BAR_HARD_RESET, 2, "HARD RESET  F1" },
    { BAR_SOFT_RESET, 14, "SOFT RESET  F4" },
    { BAR_PAUSE, 26, "PAUSE" },
    { BAR_TURBO, 38, "FULL SPEED  F9" },
    { BAR_MUTE, 50, "SOUND" },
    { BAR_SMALLER, 66, "SMALLER WINDOW" },
    { BAR_BIGGER, 90, "BIGGER WINDOW" },
    { BAR_CARDS, BAR_W - 54, "SD CARDS AND SYSTEM/NEXT" },
    { BAR_FULLSCREEN, BAR_W - 38, FULL_TIP },
    { BAR_PIN, BAR_W - 26, "PIN THE BAR" },
    { BAR_QUIT, BAR_W - 14, "QUIT  F10" },
};
#define NBUTTONS (int)(sizeof buttons / sizeof buttons[0])
#define BUTTON_W 12

static int hover = BAR_NONE;

static void fill(uint16_t *pix, int x, int y, int w, int h, uint16_t c)
{
    int i, j;
    for (j = y; j < y + h; j++)
        for (i = x; i < x + w; i++)
            if (i >= 0 && i < BAR_W && j >= 0 && j < BAR_H)
                pix[j * BAR_W + i] = c;
}

static void icon(uint16_t *pix, int x, int y, const uint8_t *rows, uint16_t c)
{
    int i, j;
    for (j = 0; j < 8; j++)
        for (i = 0; i < 8; i++)
            if (rows[j] >> (7 - i) & 1)
                pix[(y + j) * BAR_W + x + i] = c;
}

static int text_width(const char *s)
{
    return (int)strlen(s) * 4 - 1;
}

/* Text into a picture pitch wide (lower case shown as capitals). */
static void textp(uint16_t *pix, int pitch, int x, int y, const char *s, uint16_t c)
{
    for (; *s; s++, x += 4) {
        size_t k;
        char ch = *s >= 'a' && *s <= 'z' ? (char)(*s - 32) : *s;
        for (k = 0; k < sizeof font / sizeof font[0]; k++)
            if (font[k].c == ch) {
                int r, b;
                for (r = 0; r < 5; r++)
                    for (b = 0; b < 3; b++)
                        if (font[k].rows[r * 3 + b] == '1' && x + b >= 0 && x + b < pitch)
                            pix[(y + r) * pitch + x + b] = c;
                break;
            }
    }
}

static void text(uint16_t *pix, int x, int y, const char *s, uint16_t c)
{
    textp(pix, BAR_W, x, y, s, c);
}

int otb_bar_hit(int x)
{
    int i;
    for (i = 0; i < NBUTTONS; i++)
        if (x >= buttons[i].x && x < buttons[i].x + BUTTON_W)
            return buttons[i].id;
    return BAR_NONE;
}

int otb_bar_hover(int x)
{
    int h = x < 0 ? BAR_NONE : otb_bar_hit(x);
    if (h == hover)
        return 0;
    hover = h;
    return 1;
}

void otb_bar_draw(uint16_t *pix, const otb_bar_state *st)
{
    int i;
    char label[24];
    const char *mid = NULL;

    fill(pix, 0, 0, BAR_W, BAR_H, C_BG);
    fill(pix, 0, 0, BAR_W, 1, C_EDGE);
    for (i = 0; i < NBUTTONS; i++) {
        const button *b = &buttons[i];
        const uint8_t *ic = NULL;
        uint16_t c = C_ICON;
        if (b->id == hover) {
            fill(pix, b->x, 1, BUTTON_W, BAR_H - 1, C_HOVER);
            mid = b->name;
        }
        switch (b->id) {
        case BAR_HARD_RESET: ic = ic_power; break;
        case BAR_SOFT_RESET: ic = ic_reset; break;
        case BAR_PAUSE: ic = st->paused ? ic_play : ic_pause; if (st->paused) c = C_ON; break;
        case BAR_TURBO: ic = ic_turbo; if (st->turbo) c = C_ON; break;
        case BAR_MUTE: ic = st->muted ? ic_muted : ic_sound; if (st->muted) c = C_DIM; break;
        case BAR_SMALLER: ic = ic_minus; if (st->fullscreen || st->scale <= 1) c = C_DIM; break;
        case BAR_BIGGER: ic = ic_plus; if (st->fullscreen || st->scale >= 6) c = C_DIM; break;
        case BAR_FULLSCREEN: ic = ic_full; if (st->fullscreen) c = C_ON; break;
        case BAR_PIN: ic = ic_pin; c = st->pinned ? C_ON : C_DIM; break;
        case BAR_QUIT: ic = ic_quit; break;
        case BAR_CARDS: ic = ic_card; if (st->cards_lit) c = C_ON; break;
        }
        if (ic)
            icon(pix, b->x + 2, 3, ic, c);
    }
    /* the window scale between - and + */
    label[0] = 'X';
    label[1] = (char)('0' + (st->scale < 1 ? 1 : st->scale > 9 ? 9 : st->scale));
    label[2] = 0;
    text(pix, 78 + (BUTTON_W - text_width(label)) / 2, 5, label, st->fullscreen ? C_DIM : C_ICON);

    /* the middle: the button under the pointer, else what is going on */
    if (!mid)
        mid = st->paused ? "PAUSED" : st->turbo ? "FULL SPEED" : st->note;
    if (hover == BAR_PIN)
        mid = st->pinned ? "AUTO-HIDE THE BAR" : "PIN THE BAR";
    if (hover == BAR_PAUSE)
        mid = st->paused ? "CARRY ON" : "PAUSE";
    if (hover == BAR_MUTE)
        mid = st->muted ? "SOUND ON" : "SOUND OFF";
    if (mid) {
        int w = text_width(mid);
        text(pix, (BAR_W - w) / 2, 5, mid, C_TEXT);
    }
}

/* ---- the panel ---------------------------------------------------------- */

/* Where a row's buttons are: x and width, right to left. */
static int btn_place(const otb_panel_row *r, int i, int *w)
{
    int x = PANEL_W - 3, k;
    for (k = 1; k >= 0; k--) {
        int bw;
        if (!r->btn[k][0])
            continue;
        bw = text_width(r->btn[k]) + 6;
        x -= bw;
        if (k == i) {
            *w = bw;
            return x;
        }
        x -= 3;
    }
    *w = 0;
    return -1;
}

static void fillp(uint16_t *pix, int h, int x, int y, int w, int hh, uint16_t c)
{
    int i, j;
    for (j = y; j < y + hh; j++)
        for (i = x; i < x + w; i++)
            if (i >= 0 && i < PANEL_W && j >= 0 && j < h)
                pix[j * PANEL_W + i] = c;
}

void otb_panel_draw(uint16_t *pix, const otb_panel *p, int hover_act)
{
    int h = otb_panel_h(p), i, k;
    fillp(pix, h, 0, 0, PANEL_W, h, C_BG);
    fillp(pix, h, 0, 0, PANEL_W, 1, C_EDGE);
    fillp(pix, h, 0, 0, 1, h, C_EDGE);
    fillp(pix, h, PANEL_W - 1, 0, 1, h, C_EDGE);
    for (i = 0; i < p->rows; i++) {
        const otb_panel_row *r = &p->row[i];
        int y = 2 + i * PANEL_ROW_H;
        textp(pix, PANEL_W, 4, y + 2, r->text, r->dim ? C_DIM : C_TEXT);
        for (k = 0; k < 2; k++) {
            int w, x = btn_place(r, k, &w);
            if (x < 0)
                continue;
            fillp(pix, h, x, y, w, PANEL_ROW_H - 1, r->act[k] == hover_act ? C_HOVER : C_EDGE);
            textp(pix, PANEL_W, x + 3, y + 2, r->btn[k], r->act[k] == hover_act ? C_ON : C_ICON);
        }
    }
}

int otb_panel_hit(const otb_panel *p, int x, int y)
{
    int i = (y - 2) / PANEL_ROW_H, k;
    if (y < 2 || i < 0 || i >= p->rows)
        return 0;
    for (k = 0; k < 2; k++) {
        int w, bx = btn_place(&p->row[i], k, &w);
        if (bx >= 0 && x >= bx && x < bx + w)
            return p->row[i].act[k];
    }
    return 0;
}
