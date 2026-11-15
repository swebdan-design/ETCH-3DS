/*
 * ETCH 3DS -- by Synth Centric
 * Host tests for the 3DS layer: the block pump, the PUNCH filter and the
 * retro buffer (3ds/source/audio_pump.c, punch.c), linked with the same
 * --wrap taps as the .3dsx.
 *
 *   1. the drum tap copies the drum bus exactly and adds it exactly
 *   2. the retro buffer holds the newest audio, a bar at the slowest tempo
 *   3. a punch starts on the next 16th, exactly: the output is the unpunched
 *      output, bit for bit, until the limiter's look-ahead before that sample
 *   4. a punch while stopped (or with quantise off) starts at once
 *   5. low-pass takes the top off, high-pass takes the bottom off, and the
 *      filter is stable at every setting
 *   6. letting go snaps back: the filter is bypassed within a block and the
 *      sequencer never stopped
 *   7. the command ring keeps order across many blocks
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "ipc.h"
#include "seq_core.h"
#include "audio.h"
#include "app.h"
#include "corrosion.h"
#include "audio_pump.h"
#include "punch.h"

static int fails = 0, checks = 0;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

shared_t *SH;
volatile uint16_t ui_entropy;
uint8_t app_card;
uint16_t hw_noise(void) { return 0; }
uint8_t app_next_bar_pattern(void) { return seq_next_bar_pattern(); }
void __wrap_foundry_render(int32_t *L, int32_t *R, uint32_t n);

static shared_t sh;

static void boot(uint16_t seed, int play)
{
    static int16_t junk[AUDIO_BLOCK * 2];
    if (SH) {                       /* settle: let every voice and delay die away */
        uint32_t b;
        app_send(CMD(CMD_STOP, 0));
        punch_filter(0);
        foundry_kill();
        for (b = 0; b < 600; b++) pump_block(junk);
    }
    memset(&sh, 0, sizeof sh);
    SH = &sh;
    sh.queued = 0xFF;
    sh.evo_undo_pat = 0xFF;
    demo_load(&sh.proj);
    demo_import_sound(&sh.proj);
    audio_engine_init();
    {   /* FOUNDRY's noise sounds come from a free-running generator, so the
         * drums are built once: every run then hears the same kit */
        static int built;
        if (!built) { foundry_build(); built = 1; }
    }
    pump_init(&sh);
    punch_filter(0);
    punch_quantise = 1;
    if (play) { evo_seed(seed); app_send(CMD(CMD_START, seed)); }
}

/* render n blocks into out (interleaved) */
static void run(int16_t *out, uint32_t n)
{
    uint32_t b;
    for (b = 0; b < n; b++) pump_block(out + (size_t)b * AUDIO_BLOCK * 2);
}

/* energy of the first difference (treble) and of a 64-sample moving average
 * (bass), left channel */
static void bands(const int16_t *lr, uint32_t frames, double *hi, double *lo)
{
    double h = 0, l = 0, acc = 0;
    uint32_t i;
    for (i = 1; i < frames; i++) {
        double d = (double)lr[2 * i] - lr[2 * i - 2];
        h += d * d;
        acc += lr[2 * i];
        if (i >= 64) acc -= lr[2 * (i - 64)];
        l += (acc / 64) * (acc / 64);
    }
    *hi = h / frames;
    *lo = l / frames;
}

#define NB 2048u
static int16_t ref[NB * AUDIO_BLOCK * 2], got[NB * AUDIO_BLOCK * 2];

int main(void)
{
    uint32_t i, b;

    /* 1. the drum tap */
    {
        int32_t L[CTRL_LEN], R[CTRL_LEN], L0[CTRL_LEN], R0[CTRL_LEN];
        const int16_t *dr;
        int ok = 1, any = 0;
        boot(1, 0);
        foundry_trigger(0x01, 0);                      /* the kick: within int16 */
        for (i = 0; i < CTRL_LEN; i++) { L[i] = L0[i] = (int32_t)(i * 37) - 600; R[i] = R0[i] = 900 - (int32_t)i * 11; }
        punch_begin_block(&sh.ev[0], 0, 0);
        __wrap_foundry_render(L, R, CTRL_LEN);
        dr = retro_drums();
        for (i = 0; i < CTRL_LEN; i++) {
            if (L[i] != L0[i] + dr[2 * i] || R[i] != R0[i] + dr[2 * i + 1]) {
                if (ok) printf("  tap %u: L %d L0 %d dr %d | R %d R0 %d dr %d\n", i, L[i], L0[i], dr[2 * i], R[i], R0[i], dr[2 * i + 1]);
                ok = 0;
            }
            if (dr[2 * i] || dr[2 * i + 1]) any = 1;
        }
        CHECK(ok, "drum tap: bus = bus + drums, exactly, and the drums are in the retro buffer");
        CHECK(any, "drum tap: the drums are not silent");
    }

    /* 2. the retro buffer */
    boot(0xAB, 1);
    run(ref, NB);
    {
        const int16_t *m = retro_mix();
        uint32_t base = NB * AUDIO_BLOCK - RETRO_FRAMES, ok = 1, nz = 0;
        for (i = 0; i < RETRO_FRAMES; i++) {
            uint32_t k = ((base + i) & (RETRO_FRAMES - 1)) * 2;
            if (m[k] != ref[2 * (base + i)] || m[k + 1] != ref[2 * (base + i) + 1]) ok = 0;
        }
        CHECK(ok, "retro buffer = the newest %u frames of the mix, exactly", RETRO_FRAMES);
        CHECK(RETRO_FRAMES * 1000u / FS_HZ >= 4000u, "retro buffer holds a bar at 60 BPM (%u ms)", RETRO_FRAMES * 1000u / FS_HZ);
        for (i = 0; i < RETRO_FRAMES * 2; i++) nz += retro_drums()[i] != 0;
        CHECK(nz > RETRO_FRAMES / 4, "drum-bus retro buffer is filled (%u nonzero)", nz);
        printf("  retro buffer: %u frames (%.2f s) of mix and drum bus\n", RETRO_FRAMES, RETRO_FRAMES / 32728.5);
    }

    /* 3. quantised start: request mid-bar, the punch begins on the next 16th */
    {
        uint32_t req_blk = 600, start = 0, first_diff = 0xFFFFFFFF, armed_blocks = 0;
        boot(0xAB, 1);
        for (b = 0; b < NB; b++) {
            if (b == req_blk) punch_filter(-255);
            pump_block(got + (size_t)b * AUDIO_BLOCK * 2);
            if (b >= req_blk && !start) {
                if (punch_state == PUNCH_ARMED) armed_blocks++;
                if (punch_state == PUNCH_ON) {
                    const evslot_t *s = &sh.ev[b & (AUDIO_NBLK - 1)];
                    uint32_t k;
                    for (k = 0; k < s->n; k++) if (s->ev[k].type == EV_STEP) { start = b * AUDIO_BLOCK + s->ev[k].off; break; }
                }
            }
        }
        for (i = 0; i < NB * AUDIO_BLOCK; i++)
            if (got[2 * i] != ref[2 * i] || got[2 * i + 1] != ref[2 * i + 1]) { first_diff = i; break; }
        CHECK(start > 0, "the punch started");
        CHECK(armed_blocks >= 1 && armed_blocks <= 32, "it waited for the next 16th (%u blocks armed)", armed_blocks);
        CHECK(first_diff >= start - 2 * CTRL_LEN && first_diff < start + 64,
              "output identical until the punch (first difference at %u, punch at %u)", first_diff, start);
        printf("  quantised start: requested at sample %u, punched in at %u (a 16th), first changed sample %u\n",
               req_blk * AUDIO_BLOCK, start, first_diff);
    }

    /* 4. stopped, or quantise off: at once */
    {
        boot(0xAB, 0);
        punch_filter(200);
        pump_block(got);
        CHECK(punch_state == PUNCH_ON && punch_mode == 2, "stopped: punches in at once (state %u)", punch_state);
        boot(0xAB, 1);
        run(got, 50);
        punch_quantise = 0;
        punch_filter(-100);
        pump_block(got);
        CHECK(punch_state == PUNCH_ON, "quantise off: punches in at once");
    }

    /* 5. low-pass and high-pass do what they say, and stay stable */
    {
        double hi0, lo0, hi1, lo1, hi2, lo2;
        int amt, stable = 1;
        int16_t *seg = got;
        uint32_t frames = 512 * AUDIO_BLOCK;
        bands(ref + 2 * 1024 * AUDIO_BLOCK, frames, &hi0, &lo0);
        boot(0xAB, 1);
        punch_quantise = 0;
        run(seg, 1024);
        punch_filter(-255);
        run(seg, 512);
        bands(seg, frames, &hi1, &lo1);
        boot(0xAB, 1);
        punch_quantise = 0;
        run(seg, 1024);
        punch_filter(255);
        run(seg, 512);
        bands(seg, frames, &hi2, &lo2);
        CHECK(hi1 < hi0 * 0.05, "full low-pass: treble down %.1f dB", 10 * log10(hi1 / hi0));
        CHECK(lo2 < lo0 * 0.10, "full high-pass: bass down %.1f dB", 10 * log10(lo2 / lo0));
        printf("  filter: full LP treble %.1f dB, full HP bass %.1f dB (vs. unpunched)\n",
               10 * log10(hi1 / hi0), 10 * log10(lo2 / lo0));
        /* sweep both ways, every setting, watching for blow-ups */
        boot(0x77, 1);
        punch_quantise = 0;
        for (amt = -255; amt <= 255; amt += 5) {
            punch_filter(amt ? amt : 1);
            run(seg, 8);
            for (i = 0; i < 8 * AUDIO_BLOCK * 2; i++) if (seg[i] == 32767 || seg[i] == -32768) stable = 0;
        }
        CHECK(stable, "filter sweep -255..255: no runaway (output never pinned)");
    }

    /* 6. letting go snaps back, and the song kept going underneath */
    {
        uint32_t steps0, steps1;
        boot(0xAB, 1);
        punch_quantise = 0;
        run(got, 100);
        punch_filter(-255);
        run(got, 200);
        steps0 = sh.steps_total;
        punch_filter(0);
        pump_block(got);
        CHECK(punch_state == PUNCH_IDLE, "released: bypassed again within one block (state %u)", punch_state);
        run(got, 200);
        steps1 = sh.steps_total;
        CHECK(steps1 > steps0, "the sequencer never stopped underneath (%u -> %u steps)", steps0, steps1);
        CHECK(punch_count >= 1, "punch counted");
    }

    /* 7. the command ring: many commands, all in order, none lost */
    {
        uint32_t k;
        boot(0xAB, 1);
        for (k = 0; k < 16; k++) app_set_bass((uint8_t)k, (uint8_t)(10 + k), STEP_NOTE);
        for (k = 0; k < 200; k++) app_send(CMD(CMD_MUTES, k & 31));   /* last one wins */
        pump_block(got);
        for (k = 0; k < 16; k++) if (sh.proj.pat[sh.cur].note[k] != 10 + k) break;
        CHECK(k == 16, "216 queued commands reached the sequencer in order (%u bass edits)", k);
        CHECK(sh.proj.mutes == (199 & 31), "the last mute command wins (%u)", sh.proj.mutes);
        CHECK(pump_cmd_waits == 0, "the ring never filled");
    }

    printf("test_punch: %d checks, %d failures\n", checks, fails);
    return fails != 0;
}
