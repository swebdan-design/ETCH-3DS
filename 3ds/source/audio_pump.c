/*
 * ETCH 3DS -- by Synth Centric
 * The block pump (see audio_pump.h).
 */
#include <string.h>
#include "audio_pump.h"
#include "seq_core.h"
#include "audio.h"
#include "punch.h"

static shared_t *psh;
static uint32_t blk;                    /* next block to render */

/* ---- command ring: UI thread -> audio thread ------------------------------ */
static volatile uint32_t cmdq[PUMP_CMDQ];
static volatile uint32_t cmd_head, cmd_tail;
void (*pump_wait_hook)(void);
volatile uint32_t pump_cmd_waits;
volatile pump_stats_t pstats;

#define BARRIER() __sync_synchronize()

/* ETCH DS's app_send: the command reaches the sequencer before the next block
 * is sequenced, as a FIFO command reached the ARM7 between two blocks. */
void app_send(uint32_t cmd)
{
    uint32_t h = cmd_head;
    while (h - cmd_tail >= PUMP_CMDQ) {          /* full: let the audio thread drain */
        pump_cmd_waits++;
        if (pump_wait_hook) pump_wait_hook();
        else return;                              /* host, single thread: drop */
    }
    cmdq[h & (PUMP_CMDQ - 1)] = cmd;
    BARRIER();                                    /* SH writes (edit_pat...) before the command */
    cmd_head = h + 1;
}

void pump_init(shared_t *sh)
{
    psh = sh;
    blk = 0;
    cmd_head = cmd_tail = 0;
    seq_init(sh);
    punch_init();
}

uint32_t pump_blocks(void) { return blk; }

static inline uint32_t clk(void) { return host_clock ? host_clock() : 0; }

/* percent x 100 of the block period for a sum over 256 blocks (as render.arm.c) */
static inline uint16_t pct(uint32_t sum) { return (uint16_t)(((uint64_t)sum * 10000u) >> 25); }

void pump_block(int16_t *lr)
{
    static int16_t l[AUDIO_BLOCK], r[AUDIO_BLOCK];
    uint32_t k = blk, t, i;
    const evslot_t *slot;

    /* 1: commands (the ARM7's FIFO interrupt) */
    t = clk();
    while (cmd_tail != cmd_head) {
        uint32_t c;
        BARRIER();
        c = cmdq[cmd_tail & (PUMP_CMDQ - 1)];
        seq_command(c);
        cmd_tail++;
    }

    /* 2: sequence this block (the ARM7's block interrupt) */
    seq_render_block(k);
    psh->blk_requested = k + 1;
    psh->blk_played = k;
    pstats.seq_sum += clk() - t;

    /* 3: render it (the ARM9's render interrupt), with the PUNCH taps */
    slot = &psh->ev[k & (AUDIO_NBLK - 1)];
    punch_begin_block(slot, k, psh->playing);
    audio_render_block(slot, k, l, r);

    /* 4: retro buffer, interleave */
    t = clk();
    punch_end_block(l, r);
    for (i = 0; i < AUDIO_BLOCK; i++) { lr[2 * i] = l[i]; lr[2 * i + 1] = r[i]; }
    pstats.out_sum += clk() - t;

    psh->blk_done = k + 1;
    blk = k + 1;

    if (++pstats.blocks >= 256u) {
        pstats.punch_pct = pct(punch_ticks); punch_ticks = 0;
        pstats.tap_pct = pct(tap_ticks); tap_ticks = 0;
        pstats.out_pct = pct(pstats.out_sum); pstats.out_sum = 0;
        pstats.seq_pct = pct(pstats.seq_sum); pstats.seq_sum = 0;
        pstats.blocks = 0;
        pstats.windows++;
    }
}

/* The newest audio for the oscilloscope: three blocks back from the newest
 * (the retro buffer always has them), from a rising zero crossing so the
 * trace stands still. */
uint32_t audio_scope(int16_t *out, uint32_t n)
{
    int16_t mono[3 * AUDIO_BLOCK];
    uint32_t i, trig = 0;
    if (blk < 4) return 0;
    retro_read_mono(mono, 3 * AUDIO_BLOCK);
    for (i = 1; i < AUDIO_BLOCK; i++)
        if (mono[i - 1] < 0 && mono[i] >= 0) { trig = i; break; }
    if (n > 3 * AUDIO_BLOCK - trig) n = 3 * AUDIO_BLOCK - trig;
    for (i = 0; i < n; i++) out[i] = mono[trig + i];
    return n;
}
