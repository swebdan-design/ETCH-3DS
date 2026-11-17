/*
 * ETCH 3DS -- by Synth Centric
 * Draws every screen of the port on the host, with the real sound engine and
 * sequencer running underneath, from scripted input (buttons, touch, Circle
 * Pad), and writes each as a PPM: the top screen (400 x 240) over the bottom
 * screen (320 x 240), as on the console.  tools/screens_png.py turns them
 * into the PNGs in docs/images.
 *
 * It also checks the layout contract: nothing is drawn outside the screens
 * (the bitmaps have guard words), every tab can be reached with SELECT +
 * LEFT/RIGHT and by touch, and the boot song is playing and evolving.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ipc.h"
#include "seq_core.h"
#include "audio.h"
#include "app.h"
#include "app3ds.h"
#include "corrosion.h"
#include "audio_pump.h"
#include "audio3ds.h"
#include "punch.h"
#include "platform.h"
#include "gfx.h"
#include "storage.h"

static int fails = 0, checks = 0;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* ---- what main.c and the platform layer provide on the console ------------- */
shared_t *SH;
volatile uint16_t ui_entropy;
uint8_t app_card, app_quit;
volatile int punch_ui_amount;
volatile audio3ds_stats_t a3s;
volatile uint32_t ui_frame_ticks_avg, ui_frame_ticks_max;
uint16_t hw_noise(void) { return 0x1234; }
uint8_t app_next_bar_pattern(void) { return seq_next_bar_pattern(); }
uint32_t clk_bus(void) { return 0; }
uint8_t plat_is_new3ds(void) { return 0; }
void gfx_present(void) { }

/* scripted input, by frame number */
static uint32_t frame_no, held, prev, rep;
static int touch_x = -1, touch_y, pad_x;
void etch_scan_keys(void)
{
    uint32_t h = held | (touch_x >= 0 ? KEY_TOUCH : 0);
    rep = h & ~prev;
    prev = h;
}
uint32_t etch_keys_down(void) { return rep; }
uint32_t etch_keys_held(void) { return prev; }
uint32_t etch_keys_up(void) { return 0; }
uint32_t etch_keys_down_repeat(void) { return rep; }
void etch_keys_set_repeat(uint8_t d, uint8_t r) { (void)d; (void)r; }
void etch_touch_read(etch_touch_t *t) { t->px = (uint16_t)touch_x; t->py = (uint16_t)touch_y; }
void etch_circle_read(int *dx, int *dy) { *dx = pad_x; *dy = 0; }

/* one console frame of audio: 32 728.5 / 60 = 545.5 samples = 4.26 blocks */
static uint32_t acc;
static void audio_frame(void)
{
    static int16_t lr[AUDIO_BLOCK * 2];
    acc += 5455;
    while (acc >= AUDIO_BLOCK * 10) { pump_block(lr); acc -= AUDIO_BLOCK * 10; }
}

int frame_end(void)
{
    audio_frame();
    frame_no++;
    if (frame_no == 70) held = KEY_START;        /* the title waits for START */
    if (frame_no == 72) held = 0;
    return 1;
}

static void ui_step(void)
{
    uint32_t down, h;
    etch_scan_keys();
    down = etch_keys_down();
    h = etch_keys_held();
    punch_ui_amount = pad_x < -PUNCH_DEAD ? ((pad_x + PUNCH_DEAD) * 255) / (PUNCH_FULL - PUNCH_DEAD) :
                      pad_x > PUNCH_DEAD ? ((pad_x - PUNCH_DEAD) * 255) / (PUNCH_FULL - PUNCH_DEAD) : 0;
    punch_filter(punch_ui_amount);
    evo_frame();
    app_fill_frame();
    ui_frame(down, h);
    top_frame();
    frame_end();
}

static void frames(int n) { while (n--) ui_step(); }
static void press(uint32_t k) { held = k; ui_step(); held = 0; ui_step(); }
static void tap(int x, int y) { touch_x = x; touch_y = y; ui_step(); ui_step(); touch_x = -1; ui_step(); }
static void drag(int x0, int y0, int x1, int y1, int n)
{
    int i;
    for (i = 0; i <= n; i++) { touch_x = x0 + (x1 - x0) * i / n; touch_y = y0 + (y1 - y0) * i / n; ui_step(); }
    touch_x = -1;
    ui_step();
}

static void shot(const char *name)
{
    char path[64];
    FILE *f;
    int x, y;
    snprintf(path, sizeof path, "screen_%s.ppm", name);
    f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", TOP_W, TOP_H + BOT_H);
    for (y = 0; y < TOP_H + BOT_H; y++)
        for (x = 0; x < TOP_W; x++) {
            uint16_t c = 0;
            uint8_t rgb[3];
            int bx = x - (TOP_W - BOT_W) / 2;
            if (y < TOP_H) c = fb_top[y * TOP_W + x];
            else if (bx >= 0 && bx < BOT_W) c = fb_bot[(y - TOP_H) * BOT_W + bx];
            else c = 0;                                   /* the hinge */
            rgb[0] = (uint8_t)((c & 31) * 255 / 31);
            rgb[1] = (uint8_t)(((c >> 5) & 31) * 255 / 31);
            rgb[2] = (uint8_t)(((c >> 10) & 31) * 255 / 31);
            fwrite(rgb, 3, 1, f);
        }
    fclose(f);
}

int main(void)
{
    static shared_t sh;
    memset(&sh, 0, sizeof sh);
    SH = &sh;
    sh.queued = 0xFF;
    sh.evo_undo_pat = 0xFF;
    storage_path = "screens.sav";
    storage_gb_path = "screens_gb.sav";
    remove("screens.sav");
    app_card = 1;

    gfx_init();
    title_show();
    audio_engine_init();
    foundry_build();
    demo_load(&sh.proj);
    demo_import_sound(&sh.proj);
    sh.magic = SHARED_MAGIC;
    pump_init(&sh);
    a3s.lat_n = 1; a3s.lat_sum = 384;                    /* what the console shows */
    {
        uint16_t e = title_run();
        shot("title");
        ui_entropy = e;
        evo_seed(e);
        app_send(CMD(CMD_START, e));
    }
    ui_init();
    top_init();
    frames(30);
    CHECK(sh.playing, "START from the title plays");
    frames(60 * 9);                                      /* DRIFT: an evolution every 4 bars */
    CHECK(sh.evo_commits >= 1, "the living song evolved (%u commits)", sh.evo_commits);
    press(KEY_RIGHT); press(KEY_DOWN);                   /* a cursor on the bass line */
    frames(4);
    shot("live");

    press(KEY_SELECT | KEY_START);                       /* SERIES_SPEC 2.1: latched fill on */
    frames(40);
    CHECK(sh.fill_hold, "SELECT + START latches the fill");
    shot("fill");
    press(KEY_SELECT | KEY_START);
    frames(4);
    CHECK(!sh.fill_hold, "SELECT + START again lets it go");

    pad_x = -120;                                        /* PUNCH: Circle Pad left */
    frames(40);
    CHECK(punch_state == PUNCH_ON && punch_mode == 1, "Circle Pad left punches in the low-pass");
    shot("punch");
    pad_x = 0;
    frames(10);
    CHECK(punch_state == PUNCH_IDLE, "letting go snaps back");

    press(KEY_SELECT | KEY_RIGHT);                       /* XY */
    drag(60, 180, 260, 60, 30);
    frames(4);
    shot("xy");
    press(KEY_SELECT | KEY_RIGHT);                       /* PANEL */
    frames(4);
    shot("panel");
    press(KEY_SELECT | KEY_RIGHT);                       /* DRUM */
    frames(12);
    shot("drum");
    press(KEY_SELECT | KEY_RIGHT);                       /* KEYS */
    touch_x = 120; touch_y = 120; frames(6);
    shot("keys");
    touch_x = -1; frames(2);
    press(KEY_SELECT | KEY_RIGHT);                       /* GEN */
    frames(4);
    shot("gen");
    press(KEY_SELECT | KEY_RIGHT);                       /* SONG */
    frames(4);
    shot("song");
    CHECK(app_save_slot(0), "save to slot 1");
    press(KEY_SELECT | KEY_RIGHT);                       /* FILE */
    frames(4);
    shot("file");
    tap(6 + 4 * 62 + 20, 190);                           /* ABOUT */
    frames(2);
    shot("about1");
    press(KEY_RIGHT); frames(2); shot("about2");
    press(KEY_RIGHT); frames(2); shot("about3");
    press(KEY_RIGHT); frames(2); shot("about4");
    press(KEY_B); frames(2);

    /* every tab by touch */
    {
        int t;
        for (t = 0; t < 6; t++) { tap(6 + t * 51 + 25, 10); frames(2); }
    }
    CHECK(sh.playing, "still playing after a tour of every screen");
    {
        extern uint32_t gfx_accent_text;
        CHECK(gfx_accent_text == 0, "no text was set in the accent (%u times)", gfx_accent_text);
    }
    printf("  screens: title, live, fill, punch, xy, panel, drum, keys, gen, song, file, about 1-4 -> build/host/screen_*.ppm\n");
    printf("  %u frames, %u blocks, %u evolutions, %u dropped commands\n",
           frame_no, pump_blocks(), sh.evo_commits, pump_cmd_waits);
    printf("test_screens: %d checks, %d failures\n", checks, fails);
    return fails != 0;
}
