/*
 * ETCH 3DS -- by Synth Centric
 * Boot and the main loop.  Ported from ETCH DS (arm9/source/main.c).
 *
 * Boot: the title is up first.  Then the sound engine (FOUNDRY renders its
 * drum sounds), the shared block with the boot song (ETCH's CORRODE from its
 * seed code 0F1C-9009-5870, in DRIFT), the SD card, and the audio thread,
 * which streams silence until playback starts.  Press START: the moment of
 * the press seeds the living song, and acid plays.
 *
 * Main loop (60 Hz, main thread, core 0, below the audio thread): input,
 * PUNCH (Circle Pad), CORROSION (evolve.c), the bottom screen (ui.c), the top
 * screen (scr_top.c), the copy to the framebuffers.
 */
#include <3ds.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#define ETCH_PLATFORM_IMPL
#include "platform.h"
#include "app.h"
#include "app3ds.h"
#include "gfx.h"
#include "audio.h"
#include "storage.h"
#include "audio_pump.h"
#include "audio3ds.h"
#include "punch.h"
#include "stats_mailbox.h"

static shared_t shared_store __attribute__((aligned(32)));

shared_t *SH;
uint8_t app_card, app_quit;
volatile int punch_ui_amount;
volatile uint16_t ui_entropy;

void plat_init(void);
void plat_exit(void);

uint16_t hw_noise(void) { return (uint16_t)svcGetSystemTick(); }

/* the pattern after the next bar line (advisory, as in ETCH's main loop) */
uint8_t app_next_bar_pattern(void)
{
    uint8_t p = SH->cur;
    if (SH->proj.chain.song) {
        if ((uint8_t)(SH->chain_bar + 1) >= SH->proj.chain.rep[SH->chain_pos]) {
            uint8_t pos = SH->chain_pos, i;
            for (i = 0; i < CHAIN_LEN; i++) {
                pos = (uint8_t)((pos + 1) & (CHAIN_LEN - 1));
                if (SH->proj.chain.pat[pos] < NUM_PATTERNS) { p = SH->proj.chain.pat[pos]; break; }
                if (SH->proj.chain.pat[pos] == 0xFF) pos = CHAIN_LEN - 1;
            }
        }
    } else if (SH->queued != 0xFF) {
        p = SH->queued;
    }
    return p;
}

static void shared_init(void)
{
    SH = &shared_store;
    memset(SH, 0, sizeof *SH);
    SH->queued = 0xFF;
    SH->evo_undo_pat = 0xFF;
    demo_load(&SH->proj);
    demo_import_sound(&SH->proj);
    SH->magic = SHARED_MAGIC;
}

/* ---- the stats mailbox (see stats_mailbox.h) ------------------------------ */
static uint16_t start_seed;
volatile uint32_t etch_stats[64] __attribute__((used));
void stats_publish(void)
{
    volatile uint32_t *m = etch_stats;
    int i;
    m[SM_MAGIC] = SM_MAGIC_VALUE;
    m[SM_FRAME]++;
    m[SM_BLOCKS] = pump_blocks();
    m[SM_DROPOUTS] = SH->dropouts;
    m[SM_PLAYING] = SH->playing;
    m[SM_STEP] = SH->step;
    m[SM_COMMITS] = SH->evo_commits;
    m[SM_STEPS_TOTAL] = SH->steps_total;
    m[SM_GATES_TOTAL] = SH->gates_total;
    m[SM_GATES_LATE] = SH->gates_late;
    m[SM_WINDOWS] = astats.windows;
    for (i = 0; i < 9; i++) m[SM_ST0 + i] = astats.avg_pct[i];
    m[SM_TOTAL] = astats.avg_total;
    m[SM_MAX_TOTAL] = astats.max_total_pct;
    m[SM_SEQ] = pstats.seq_pct;
    m[SM_PUNCH] = pstats.punch_pct;
    m[SM_TAP] = pstats.tap_pct;
    m[SM_OUT] = pstats.out_pct;
    m[SM_UI_AVG] = ui_frame_ticks_avg;
    m[SM_UI_MAX] = ui_frame_ticks_max;
    m[SM_LAT_SUM] = a3s.lat_sum;
    m[SM_LAT_N] = a3s.lat_n;
    m[SM_LAT_MIN] = a3s.lat_min;
    m[SM_LAT_MAX] = a3s.lat_max;
    m[SM_QUEUED_MIN] = a3s.queued_min;
    m[SM_WAKES] = a3s.wakes;
    m[SM_PUNCH_STATE] = punch_state;
    m[SM_PUNCH_MODE] = punch_mode;
    m[SM_PUNCH_COUNT] = punch_count;
    m[SM_NEW3DS] = plat_is_new3ds();
    m[SM_NO_DSP] = a3s.no_dsp;
    m[SM_CMD_WAITS] = pump_cmd_waits;
    m[SM_CUR] = SH->cur;
    m[SM_BPM] = SH->proj.synth.bpm;
    m[SM_STRESS] = SH->stress;
    m[SM_DEPTH] = audio3ds_depth;
    m[SM_SEED] = start_seed;
}

/* the UI thread waits for the audio thread (command ring full: never seen) */
static void wait_audio(void) { svcSleepThread(1000000LL); }

/* PUNCH: the Circle Pad's filter axis.  Left = low-pass, right = high-pass,
 * by how far it is pushed past a dead zone. */
static void punch_frame(void)
{
    int dx, dy, a = 0;
    etch_circle_read(&dx, &dy);
    (void)dy;
    if (dx > PUNCH_DEAD) a = ((dx - PUNCH_DEAD) * 255) / (PUNCH_FULL - PUNCH_DEAD);
    else if (dx < -PUNCH_DEAD) a = ((dx + PUNCH_DEAD) * 255) / (PUNCH_FULL - PUNCH_DEAD);
    punch_ui_amount = a < -255 ? -255 : a > 255 ? 255 : a;
    punch_filter(punch_ui_amount);
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    plat_init();
    title_show();                        /* the name is up before anything else */
    frame_end();

    host_clock = clk_bus;                /* the CPU meter's clock: DS bus clocks */
    audio_engine_init();
    foundry_build();
    shared_init();
    pump_wait_hook = wait_audio;
    pump_init(SH);
    mkdir("sdmc:/etch_ds", 0777);
    app_card = storage_init();
    audio3ds_start(SH);                  /* streams silence until START */

    /* Press START, hear music: the title waits for START (or a tap); the
     * moment of the press seeds the living song, and playback starts. */
    {
        uint16_t e = title_run();
        if (app_quit) goto out;
        ui_entropy = e;
        evo_seed(e);
        start_seed = e;
        app_send(CMD(CMD_START, e));
    }
    ui_init();
    top_init();

    for (;;) {
        uint32_t down, held;
        etch_scan_keys();
        down = etch_keys_down();
        held = etch_keys_held();
        if (down) ui_entropy = (uint16_t)((ui_entropy * 31u) ^ hw_noise() ^ down);

        if (down & KEY_START) {
            if (!SH->playing) {
                uint16_t e = (uint16_t)(ui_entropy ^ hw_noise());
                evo_seed(e);                        /* the moment START was pressed */
                start_seed = e;
                app_send(CMD(CMD_START, e));
            } else {
                app_send(CMD(CMD_STOP, 0));
            }
        }
        if ((held & (KEY_SELECT | KEY_START | KEY_L | KEY_R)) == (KEY_SELECT | KEY_START | KEY_L | KEY_R)) break;
        punch_frame();
        evo_frame();
        app_fill_frame();
        ui_frame(down, held);
        top_frame();
        stats_publish();
        if (!frame_end()) break;
    }
out:
    audio3ds_stop();
    plat_exit();
    return 0;
}
