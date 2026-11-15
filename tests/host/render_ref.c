/*
 * ETCH 3DS -- by Synth Centric
 * The reference render for the emulator check (tools/run_azahar.py): the boot
 * song, started with a given seed, through the DS path of the shared core, as
 * interleaved L/R int16.  The emulator's recording of the .3dsx must contain
 * exactly these samples from the moment START landed.
 *
 *   render_ref SEED SECONDS OUT.raw
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ipc.h"
#include "seq_core.h"
#include "audio.h"
#include "app.h"

shared_t *SH;
volatile uint16_t ui_entropy;
uint8_t app_card;
uint16_t hw_noise(void) { return 0; }
uint8_t app_next_bar_pattern(void) { return seq_next_bar_pattern(); }
void app_send(uint32_t cmd) { seq_command(cmd); }

int main(int argc, char **argv)
{
    static shared_t sh;
    uint16_t seed;
    uint32_t nblk, b, i;
    FILE *f;
    if (argc < 4) { printf("usage: render_ref SEED SECONDS OUT.raw\n"); return 2; }
    seed = (uint16_t)strtoul(argv[1], NULL, 0);
    nblk = (uint32_t)(atof(argv[2]) * 32728.5 / AUDIO_BLOCK);
    f = fopen(argv[3], "wb");
    if (!f) return 1;
    memset(&sh, 0, sizeof sh);
    SH = &sh;
    sh.queued = 0xFF;
    sh.evo_undo_pat = 0xFF;
    demo_load(&sh.proj);
    demo_import_sound(&sh.proj);
    audio_engine_init();
    foundry_build();
    seq_init(&sh);
    evo_seed(seed);
    app_send(CMD(CMD_START, seed));
    for (b = 0; b < nblk; b++) {
        int16_t l[AUDIO_BLOCK], r[AUDIO_BLOCK], lr[2 * AUDIO_BLOCK];
        if ((b & 3) == 0) evo_frame();
        seq_render_block(b);
        audio_render_block(&sh.ev[b & (AUDIO_NBLK - 1)], b, l, r);
        for (i = 0; i < AUDIO_BLOCK; i++) { lr[2 * i] = l[i]; lr[2 * i + 1] = r[i]; }
        fwrite(lr, sizeof lr, 1, f);
    }
    fclose(f);
    return 0;
}
