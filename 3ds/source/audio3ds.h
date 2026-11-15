/*
 * ETCH 3DS -- by Synth Centric
 * The audio thread and ndsp streaming (see audio3ds.c).
 */
#ifndef ETCH_3DS_AUDIO3DS_H
#define ETCH_3DS_AUDIO3DS_H
#include <stdint.h>
#include "ipc.h"

#define AUDIO3DS_DEPTH_DEFAULT 3    /* blocks queued ahead: 3 x 3.91 ms */

typedef struct {
    uint32_t wakes, blocks;
    uint32_t queued_min;            /* fewest blocks still queued at a wake */
    uint32_t lat_sum, lat_n, lat_min, lat_max;   /* samples ahead of the speaker */
    uint8_t  started, no_dsp;       /* no_dsp: ndspInit failed (no DSP firmware) */
} audio3ds_stats_t;
extern volatile audio3ds_stats_t a3s;
extern uint8_t audio3ds_depth;

int  audio3ds_start(shared_t *sh);   /* 0 = no audio (see a3s.no_dsp) */
void audio3ds_stop(void);
#endif
