/*
 * The card panel (the SD card button on the bar): System/Next updates and
 * the Next's two card slots, while the Next runs.
 *
 *   - Look for a newer System/Next (at each start too, when check=1 in
 *     opentbblue.ini; the card button lights up when there is one).
 *   - Download it, the bar counting the MB; the official archive is kept in
 *     media/ as it came, and a broken download carries on where it stopped.
 *   - Then either update the card in SD0 (the distribution's files written
 *     over, the card's own files left alone; the Next starts again on it)
 *     or write it to a new card, drives/SystemNext-<date>, leaving SD0 as
 *     it is.
 *   - Any card folder in drives/ can be put in SD0 or SD1, and SD1 can be
 *     emptied. The choice is kept in opentbblue.ini.
 */
#ifndef OTB_CARDS_H
#define OTB_CARDS_H

#include "bar.h"

/* What the panel needs the program to do with the machine. */
typedef struct otb_cards_host {
    /* Put the card folder path in slot (0 or 1); slot 0 starts the Next
     * again. 0 on success. */
    int (*insert)(int slot, const char *path);
    /* Take the card out of slot (written back first). */
    void (*eject)(int slot);
} otb_cards_host;

/* The slots' card folders (full paths; sd1 "" for none), and whether to
 * look for a newer System/Next now. */
void otb_cards_init(const otb_cards_host *host, const char *sd0, const char *sd1, int check);

/* Once a frame: moves a download or a lookup on, finishes a write. */
void otb_cards_tick(void);

/* 1 while the Next must wait (its SD0 card is being written). */
int otb_cards_busy(void);

/* The panel as it stands, and a click on one of its buttons (1: close
 * the panel). */
void otb_cards_panel(otb_panel *p);
int otb_cards_action(int act);

/* The card button lit (there is news), and the note for the bar. */
int otb_cards_lit(void);
const char *otb_cards_note(void);

/* The panel was opened: read drives/ again. */
void otb_cards_opened(void);

#endif
