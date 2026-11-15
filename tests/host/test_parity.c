/*
 * ETCH 3DS -- by Synth Centric
 * Sound parity: the shared core renders the boot song identically in the DS
 * configuration and in the 3DS configuration.
 *
 * Built twice from this one file:
 *
 *   parity_ds   the DS path, as ETCH DS runs it: a command reaches
 *               seq_command directly between two blocks (the ARM7's FIFO
 *               interrupt), then seq_render_block (the ARM7's block
 *               interrupt), then audio_render_block into two int16 buffers
 *               (the ARM9's render interrupt).  Nothing from 3ds/ is linked.
 *
 *   parity_3ds  the 3DS path, as ETCH 3DS runs it: commands go through the
 *               UI -> audio command ring (app_send), and every block is
 *               rendered by pump_block (3ds/source/audio_pump.c), linked with
 *               -Wl,--wrap=foundry_render,--wrap=fx_limit so the PUNCH taps
 *               (drum-bus retro buffer, PUNCH filter, mix retro buffer) are in
 *               the signal path, and interleaved for ndsp.
 *
 * Both run the same 64-second script against the same core: START from a
 * seed, DRIFT evolutions from the main loop, XY sweeps, FX changes, bass and
 * drum edits, a tempo change, a drum re-synthesis while playing, a fill, the
 * CPU stress mode, GEN, EVOLVE, mutes, KEYS live notes, STOP / START, UNDO
 * and CORRODE.  Each writes its output (interleaved L/R int16) and prints a
 * 64-bit FNV-1a hash; the Makefile requires the two files to be identical.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ipc.h"
#include "seq_core.h"
#include "audio.h"
#include "app.h"
#include "corrosion.h"
#ifdef PARITY_3DS
#include "audio_pump.h"
#include "punch.h"
#endif

shared_t *SH;
volatile uint16_t ui_entropy;
uint8_t app_card;
uint16_t hw_noise(void) { return 0x5A5A; }
uint8_t app_next_bar_pattern(void) { return seq_next_bar_pattern(); }
#ifndef PARITY_3DS
void app_send(uint32_t cmd) { seq_command(cmd); }      /* the ARM7's FIFO interrupt */
#endif

#define NBLK 16384u                                      /* 64.1 s */

static void script(uint32_t b)
{
    if (b == 0) { evo_seed(0xC0DE); app_send(CMD(CMD_START, 0xC0DE)); }
    if ((b & 3) == 0) { evo_frame(); app_fill_frame(); }    /* the 60 Hz main loop */
    if (b >= 1000 && b < 3000) {                           /* XY pad sweep */
        acid_p.cutoff = (uint8_t)(20 + ((b - 1000) * 100) / 2000);
        acid_p.res = (uint8_t)(127 - ((b - 1000) * 90) / 2000);
    }
    if (b == 3500) { fx_p.dly_mix = 60; fx_p.dly_fb = 80; fx_p.comb_mix = 40; fx_p.drive = 50;
                     fx_p.crush_bits = 10; fx_p.crush_hold = 3; }
    if (b == 4000) { app_set_bass(3, 30, STEP_NOTE | STEP_ACCENT); app_toggle_drum(2, 5); }
    if (b == 4500) { SH->proj.synth.bpm = 150; fx_p.bpm = 150; app_send(CMD(CMD_TEMPO, 0)); }
    if (b == 5000) { foundry_defs[0][SH->proj.pat[SH->cur].kit[0] & 3].f0 = 80;
                     foundry_rebuild(0, SH->proj.pat[SH->cur].kit[0] & 3); }
    if (b == 5500) app_fill();
    if (b == 6000) app_send(CMD(CMD_STRESS, 1));
    if (b == 6500) app_send(CMD(CMD_STRESS, 0));
    if (b == 7000) app_generate(GEN_DRUMS);
    if (b == 8000) app_evolve();
    if (b == 8500) app_toggle_mute(1);
    if (b == 9000) app_toggle_mute(1);
    if (b == 9500) live_note(EV_NOTE, 24, 1);
    if (b == 9600) live_note(EV_GLIDE, 31, 0);
    if (b == 9700) live_note(EV_GATE_OFF, 0, 0);
    if (b == 10000) app_send(CMD(CMD_STOP, 0));
    if (b == 10200) { evo_seed(0x5EED); app_send(CMD(CMD_START, 0x5EED)); }
    if (b == 11000) app_undo();
    if (b == 12000) SH->proj.gen.mode = EVO_CORRODE;
    if (b == 13000) { acid_p.wave = 3; acid_p.drive = DRV_FOLD; acid_p.drive_amt = 90; }
    if (b == 14000) { acid_p.keytrack = CUT_KEY; acid_p.slide = 100; }
    if (b == 15000) app_regen(GEN_ALL);
}

int main(int argc, char **argv)
{
    static shared_t sh;
    static int16_t out[NBLK * AUDIO_BLOCK * 2];
    uint64_t h = 1469598103934665603ull;
    uint32_t b, i;
    const char *path = argc > 1 ? argv[1] : "parity.raw";
    FILE *f;

    memset(&sh, 0, sizeof sh);
    SH = &sh;
    sh.queued = 0xFF;
    sh.evo_undo_pat = 0xFF;
    demo_load(&sh.proj);
    demo_import_sound(&sh.proj);
    audio_engine_init();
    foundry_build();
#ifdef PARITY_3DS
    pump_init(&sh);
#else
    seq_init(&sh);
#endif
    for (b = 0; b < NBLK; b++) {
        int16_t *lr = out + (size_t)b * AUDIO_BLOCK * 2;
        script(b);
#ifdef PARITY_3DS
        pump_block(lr);
#else
        {
            int16_t l[AUDIO_BLOCK], r[AUDIO_BLOCK];
            seq_render_block(b);
            audio_render_block(&sh.ev[b & (AUDIO_NBLK - 1)], b, l, r);
            for (i = 0; i < AUDIO_BLOCK; i++) { lr[2 * i] = l[i]; lr[2 * i + 1] = r[i]; }
        }
#endif
    }
    for (i = 0; i < NBLK * AUDIO_BLOCK * 2; i++) {
        uint16_t v = (uint16_t)out[i];
        h = (h ^ (v & 0xFF)) * 1099511628211ull;
        h = (h ^ (v >> 8)) * 1099511628211ull;
    }
    f = fopen(path, "wb");
    if (!f || fwrite(out, sizeof out, 1, f) != 1) { printf("cannot write %s\n", path); return 1; }
    fclose(f);
#ifdef PARITY_3DS
    printf("  3DS path: %u blocks, %u commits, hash %016llx, retro buffer %lu frames\n",
           NBLK, sh.evo_commits, (unsigned long long)h, (unsigned long)retro_frames);
#else
    printf("  DS path:  %u blocks, %u commits, hash %016llx\n", NBLK, sh.evo_commits, (unsigned long long)h);
#endif
    return 0;
}
