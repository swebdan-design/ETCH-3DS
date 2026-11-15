/*
 * ETCH 3DS -- by Synth Centric
 * PUNCH: live punch-in effects.  Milestone 1 ships the architecture and one
 * effect, the Circle Pad's filter axis:
 *
 *   * the RETRO BUFFER always holds the last bar (at the slowest tempo, 60
 *     BPM: 4 s, so at least one bar at every tempo), of the mix and of the
 *     drum bus on its own.  BEAT REPEAT will play from it; in milestone 1 the
 *     oscilloscope reads it.
 *   * the DJ FILTER: Circle Pad left = resonant low-pass, right = high-pass,
 *     the further the push the stronger.  A punch starts quantised to the next
 *     16th (or at once with punch_quantise = 0, or when stopped), and letting
 *     go snaps straight back: the sequencer never stops underneath.  Starts
 *     and stops crossfade over 64 samples (2 ms), so there are no clicks.
 *
 * The filter is a fixed-point, zero-delay-feedback state-variable filter
 * (Simper's trapezoidal SVF, coefficients from a table built at boot).  The
 * audio path has no division and no floating point, like the shared core.
 * While nothing is punched in, the mix passes through untouched, bit for bit.
 */
#ifndef ETCH_3DS_PUNCH_H
#define ETCH_3DS_PUNCH_H
#include <stdint.h>
#include "ipc.h"

#define RETRO_FRAMES (1u << 17)     /* 131 072 frames = 4.0 s at 32 728 Hz   */

/* UI thread: the filter axis, -255 (full low-pass) .. 0 (off) .. +255 (full
 * high-pass).  Read by the audio thread once per block. */
void punch_filter(int amount);
extern volatile uint8_t punch_quantise;        /* 1: start on the next 16th */

/* what the audio thread is doing, for the screens */
enum { PUNCH_IDLE, PUNCH_ARMED, PUNCH_ON, PUNCH_RELEASE };
extern volatile uint8_t punch_state, punch_mode;   /* mode: 1 LP, 2 HP     */
extern volatile uint32_t punch_count;              /* punches started      */

/* audio thread (audio_pump.c) */
void punch_init(void);
void punch_begin_block(const evslot_t *slot, uint32_t blk, uint8_t playing);
void punch_end_block(const int16_t *l, const int16_t *r);
extern uint32_t punch_ticks, tap_ticks;            /* DS bus clocks        */

/* the retro buffer: newest frames written so far, and readers */
extern uint32_t retro_frames;                      /* total frames written */
void retro_read_mono(int16_t *out, uint32_t n);    /* newest n frames, mix */
const int16_t *retro_mix(void);                    /* RETRO_FRAMES x L,R   */
const int16_t *retro_drums(void);                  /* RETRO_FRAMES x L,R   */
#endif
