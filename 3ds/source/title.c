/*
 * ETCH 3DS -- by Synth Centric
 * The opening screen, as on ETCH, ETCH II and ETCH DS: the name, "a
 * generative acid machine", "by Synth Centric", and PRESS START.  Audio is
 * already running (silently); START -- or a tap, though touch is never
 * required -- returns, and the moment it was pressed seeds the living song.
 * The wave on the bottom screen is a filtered saw whose cutoff breathes,
 * drawn as a single hairline.  Ported from ETCH DS (arm9/source/title.c),
 * laid out for 400 x 240 and 320 x 240.
 */
#include <stdio.h>
#include <string.h>
#include "platform.h"
#include "gfx.h"
#include "app.h"
#include "app3ds.h"
#include "dsp_tables.h"
#include "audio3ds.h"

#define WAVE_X 6
#define WAVE_W (BOT_W - 12)
#define WAVE_Y 84
#define WAVE_H 72

static void wordmark(uint16_t *fb, int y)
{
    int s = 5, w1 = (gfx_text_w(F_MAIN, "ETCH") + 1) * s, w2 = (gfx_text_w(F_MAIN, "3DS") + 1) * s, gap = 12;
    int x = TOP_W / 2 - (w1 + gap + w2 - s) / 2;
    x = gfx_text_scaled(fb, x, y, F_MAIN, s, COL_TEXT, "ETCH");
    gfx_text_scaled(fb, x + gap - s, y, F_MAIN, s, COL_TEXT3, "3DS");
}

static void draw_top(void)
{
    gfx_clear(fb_top, COL_BG);
    wordmark(fb_top, 64);
    gfx_text_c(fb_top, TOP_W / 2, 118, F_SMALL, COL_TEXT2, "A GENERATIVE ACID MACHINE");
    gfx_hline(fb_top, TOP_W / 2 - 20, 134, 40, COL_SURF3);
    gfx_text_c(fb_top, TOP_W / 2, 146, F_MAIN, COL_TEXT, "BY SYNTH CENTRIC");
    gfx_text_c(fb_top, TOP_W / 2, 222, F_SMALL, COL_TEXT3, "VERSION " ETCH_3DS_VERSION "   CORROSION FROM ETCH 2.5");
}

/* one cycle of a band-limited saw through a resonant low-pass: a few
 * harmonics, the cutoff swinging slowly with t */
static void draw_wave(uint32_t t)
{
    static int16_t prev[WAVE_W];
    int x, cut = 3 + ((sine_tab[(t * 3) & 1023] + 32768) >> 13);      /* 3..10 harmonics */
    gfx_rect(fb_bot, WAVE_X, WAVE_Y - WAVE_H / 2 - 2, WAVE_W, WAVE_H + 4, COL_BG);
    for (x = 0; x < WAVE_W; x++) {
        int k, v = 0;
        uint32_t ph = (uint32_t)(x * 2 * 1024 / WAVE_W + t * 4);   /* two cycles */
        for (k = 1; k <= 10; k++) {
            int amp = 32767 / k;
            if (k > cut) amp >>= (k - cut) * 2;                     /* roll-off */
            if (k == cut) amp = amp * 5 / 2;                        /* resonance */
            v += (sine_tab[(ph * k) & 1023] * amp) >> 15;
        }
        prev[x] = (int16_t)(WAVE_Y - ((v * (WAVE_H / 2 - 2)) >> 16));
    }
    for (x = 1; x < WAVE_W; x++) gfx_line(fb_bot, WAVE_X - 1 + x, prev[x - 1], WAVE_X + x, prev[x], COL_TEXT);
}

void title_show(void)
{
    draw_top();
    gfx_clear(fb_bot, COL_BG);
    gfx_text_c(fb_bot, BOT_W / 2, 214, F_SMALL, COL_TEXT3, "HEADPHONES RECOMMENDED");
}

uint16_t title_run(void)
{
    uint32_t t = 0;
    uint16_t entropy = 0;
    if (a3s.no_dsp) {                    /* ndsp needs the DSP firmware dump */
        gfx_text_c(fb_top, TOP_W / 2, 176, F_MAIN, COL_TEXT, "NO SOUND: DSP FIRMWARE MISSING");
        gfx_text_c(fb_top, TOP_W / 2, 188, F_SMALL, COL_TEXT2, "RUN DSP1 ONCE TO CREATE SD:/3DS/DSPFIRM.CDC");
    }
    for (;;) {
        uint32_t down;
        scanKeys();
        down = keysDown();
        entropy = (uint16_t)(entropy * 31u + hw_noise() + down);
        if ((t & 1) == 0) draw_wave(t >> 1);
        if ((t & 31) == 0) {
            gfx_rect(fb_bot, 0, 164, BOT_W, 10, COL_BG);
            if (t & 32) gfx_text_c(fb_bot, BOT_W / 2, 166, F_MAIN, COL_TEXT, "PRESS START");
        }
        if (down & (KEY_START | KEY_TOUCH)) break;
        if (!frame_end()) { app_quit = 1; return 0; }
        t++;
    }
    while (keysHeld() & KEY_TOUCH) { if (!frame_end()) { app_quit = 1; return 0; } scanKeys(); }   /* the tap is not a UI touch */
    return (uint16_t)(entropy ^ hw_noise());
}
