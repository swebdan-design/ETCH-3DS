/*
 * ETCH 3DS -- by Synth Centric
 * The stats mailbox: one array of words the main loop refreshes every frame
 * with everything worth measuring.  The emulator harness
 * (tools/azahar_harness) finds `etch_stats` in the ELF and reads it out of
 * emulated RAM; tools/run_azahar.py reads the indices from this file.
 */
#ifndef ETCH_3DS_STATS_MAILBOX_H
#define ETCH_3DS_STATS_MAILBOX_H
#include <stdint.h>

enum {
    SM_MAGIC,           /* 0x53443345 "E3DS" */
    SM_FRAME,           /* main-loop frames */
    SM_BLOCKS,          /* audio blocks rendered */
    SM_DROPOUTS,        /* wakes that found the DSP queue empty */
    SM_PLAYING,
    SM_STEP,
    SM_COMMITS,         /* CORROSION evolutions committed */
    SM_STEPS_TOTAL,
    SM_GATES_TOTAL,
    SM_GATES_LATE,
    SM_WINDOWS,         /* CPU stats windows (256 blocks each) */
    SM_ST0,             /* 9 words: astats.avg_pct[ST_*], % x 100 of a block */
    SM_TOTAL = SM_ST0 + 9,
    SM_MAX_TOTAL,       /* worst block in the last window, % x 100 */
    SM_SEQ, SM_PUNCH, SM_TAP, SM_OUT,   /* 3DS stages, % x 100 */
    SM_UI_AVG, SM_UI_MAX,               /* UI frame, DS bus clocks */
    SM_LAT_SUM, SM_LAT_N, SM_LAT_MIN, SM_LAT_MAX,   /* samples ahead of the speaker */
    SM_QUEUED_MIN,      /* fewest blocks still queued at a wake */
    SM_WAKES,
    SM_PUNCH_STATE, SM_PUNCH_MODE, SM_PUNCH_COUNT,
    SM_NEW3DS, SM_NO_DSP, SM_CMD_WAITS,
    SM_CUR, SM_BPM, SM_STRESS, SM_DEPTH,
    SM_SEED,            /* the entropy of the last START (seeds the living song) */
    SM_FILL, SM_FILL_HOLD, SM_QUEUED, SM_SONG,      /* the series controls (SERIES_SPEC.md 2) */
    SM_NOTE0, SM_FLAGS0,                            /* step 1 of the playing pattern's bass */
    SM_COUNT
};
#define SM_MAGIC_VALUE 0x53443345u

extern volatile uint32_t etch_stats[64];
void stats_publish(void);
#endif
