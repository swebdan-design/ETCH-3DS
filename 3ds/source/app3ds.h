/*
 * ETCH 3DS -- by Synth Centric
 * 3DS additions to ETCH DS's application glue (core/engine/app.h).
 */
#ifndef ETCH_3DS_APP3DS_H
#define ETCH_3DS_APP3DS_H
#include <stdint.h>

/* PUNCH pad: the Circle Pad reads about +-156; past PUNCH_DEAD the filter
 * starts, at PUNCH_FULL it is all the way */
#define PUNCH_DEAD 28
#define PUNCH_FULL 150
extern volatile int punch_ui_amount;        /* -255 LP .. 0 .. +255 HP */

extern uint8_t app_quit;                    /* the system asked us to close */
#endif
