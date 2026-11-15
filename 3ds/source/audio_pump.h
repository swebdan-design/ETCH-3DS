/*
 * ETCH 3DS -- by Synth Centric
 * The block pump: what the DS split between the ARM7 block interrupt and the
 * ARM9 render interrupt, done by one audio thread on the 3DS.
 *
 * For every block (AUDIO_BLOCK = 128 samples at 32 728 Hz, 3.91 ms):
 *   1. drain the command ring (the UI's app_send) into seq_command, exactly
 *      as the ARM7's FIFO interrupt ran them between two block interrupts
 *   2. seq_render_block: ETCH's step rules, the fixed swing clock and the
 *      edit epoch, in render time (common/seq_core.c, unchanged)
 *   3. audio_render_block: ACID -> FOUNDRY -> FX (core/engine, unchanged).
 *      Two link-time taps (-Wl,--wrap) add the 3DS layer without editing the
 *      core: the drum bus is copied into PUNCH's retro buffer, and the PUNCH
 *      filter runs on the mix bus just before the limiter.  Both are exact
 *      pass-throughs while nothing is punched in.
 *   4. the mix goes into the retro buffer and out, interleaved, for ndsp
 *
 * Portable C with no 3DS calls: the host tests run this exact file (the
 * sound-parity test renders the boot song through it and through the DS
 * path and compares the hashes).
 *
 * Threads: app_send() is called by the UI thread only (single producer); the
 * ring is drained by the audio thread only (single consumer).  Nothing here
 * allocates, blocks or takes a lock.
 */
#ifndef ETCH_3DS_AUDIO_PUMP_H
#define ETCH_3DS_AUDIO_PUMP_H
#include <stdint.h>
#include "ipc.h"

#define PUMP_CMDQ 256u                 /* commands in flight (power of 2) */

void     pump_init(shared_t *sh);      /* seq_init + clear the ring       */
/* render the next block into lr (AUDIO_BLOCK interleaved stereo frames) */
void     pump_block(int16_t *lr);
uint32_t pump_blocks(void);            /* blocks rendered so far           */

/* the UI side of the ring, if it is full (it never should be) */
extern void (*pump_wait_hook)(void);
extern volatile uint32_t pump_cmd_waits;

/* the newest audio for the oscilloscope (mono, from the retro buffer) */
uint32_t audio_scope(int16_t *out, uint32_t n);

/* cost of the 3DS-only stages, in DS bus clocks per stats window, same
 * units as astats (percent x 100 of the block period) */
typedef struct {
    uint32_t seq_sum, out_sum, blocks;
    uint16_t seq_pct, punch_pct, tap_pct, out_pct;   /* last window */
    uint32_t windows;
} pump_stats_t;
extern volatile pump_stats_t pstats;
extern uint32_t (*host_clock)(void);    /* render.arm.c's clock hook       */

#endif
