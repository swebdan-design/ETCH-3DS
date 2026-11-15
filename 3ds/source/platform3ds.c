/*
 * ETCH 3DS -- by Synth Centric
 * The platform layer (see platform.h): input with ETCH DS's vocabulary, the
 * screen copy, the clock.
 */
#include <3ds.h>
#include <string.h>
#define ETCH_PLATFORM_IMPL
#include "platform.h"
#include "gfx.h"

/* ---- input ------------------------------------------------------------------- */
static uint32_t k_down, k_held, k_up, k_rep;
static uint8_t rep_delay = 15, rep_rate = 4, rep_count;

void etch_keys_set_repeat(uint8_t delay, uint8_t repeat) { rep_delay = delay; rep_rate = repeat; }

void etch_scan_keys(void)
{
    uint32_t held;
    hidScanInput();
    held = hidKeysHeld() & ETCH_KEY_BUTTONS;            /* the sticks are not keys */
    k_down = held & ~k_held;
    k_up = k_held & ~held;
    k_held = held;
    /* auto-repeat, as libnds: the first press, then every rep_rate frames
     * after rep_delay frames of holding */
    k_rep = k_down;
    if (k_down) rep_count = rep_delay;
    else if (held && rep_count && !--rep_count) { k_rep = held; rep_count = rep_rate; }
}

uint32_t etch_keys_down(void) { return k_down; }
uint32_t etch_keys_held(void) { return k_held; }
uint32_t etch_keys_up(void) { return k_up; }
uint32_t etch_keys_down_repeat(void) { return k_rep; }

void etch_touch_read(etch_touch_t *t)
{
    touchPosition p;
    hidTouchRead(&p);
    t->px = p.px;
    t->py = p.py;
}

void etch_circle_read(int *dx, int *dy)
{
    circlePosition c;
    hidCircleRead(&c);
    *dx = c.dx;
    *dy = c.dy;
}

/* ---- clock --------------------------------------------------------------------- */
/* SYSCLOCK_ARM11 / 8 = 33 513 982 Hz, exactly the DS bus clock */
uint32_t clk_bus(void) { return (uint32_t)(svcGetSystemTick() >> 3); }

static uint8_t is_new;
uint8_t plat_is_new3ds(void) { return is_new; }

/* ---- screens ------------------------------------------------------------------ */
typedef struct { int x0, y0, x1, y1; } drect_t;
extern drect_t gfx_dirty[2];
static uint16_t lut[32768];                         /* BGR555 -> RGB565 */
volatile uint32_t ui_frame_ticks_avg, ui_frame_ticks_max;

void plat_init(void)
{
    uint32_t c;
    bool n = false;
    APT_CheckNew3DS(&n);
    is_new = n;
    if (is_new) osSetSpeedupEnable(true);           /* 804 MHz, L2 cache */
    gfxInit(GSP_RGB565_OES, GSP_RGB565_OES, false);
    gfxSetDoubleBuffering(GFX_TOP, false);          /* dirty rectangles: one buffer */
    gfxSetDoubleBuffering(GFX_BOTTOM, false);
    gfxSet3D(false);
    for (c = 0; c < 32768; c++) {
        uint32_t r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
        lut[c] = (uint16_t)((r << 11) | (((g << 1) | (g >> 4)) << 5) | b);
    }
    gfx_init();
}

static void blit(gfxScreen_t scr, const uint16_t *src, int W, drect_t *d)
{
    u16 fw, fh;
    uint16_t *dst;
    int x, y;
    if (d->x0 >= d->x1) return;
    dst = (uint16_t *)gfxGetFramebuffer(scr, GFX_LEFT, &fw, &fh);   /* 240 x W, rotated */
    for (x = d->x0; x < d->x1; x++) {
        uint16_t *col = dst + x * 240 + 239;
        const uint16_t *s = src + x;
        for (y = d->y0; y < d->y1; y++) col[-y] = lut[s[y * W] & 0x7FFF];
    }
    d->x0 = d->x1 = 0;
}

void gfx_present(void)
{
    blit(GFX_TOP, fb_top, TOP_W, &gfx_dirty[0]);
    blit(GFX_BOTTOM, fb_bot, BOT_W, &gfx_dirty[1]);
    gfxFlushBuffers();
    gfxSwapBuffers();
}

static uint32_t frame_t0;
static uint32_t ft_sum, ft_n, ft_max;

/* the end of a frame: present, wait for VBlank.  The time from the end of the
 * previous VBlank wait to here is the UI frame time (input, CORROSION, both
 * screens, the copy), measured excluding the wait. */
int frame_end(void)
{
    uint32_t t;
    gfx_present();
    if (frame_t0) {                                  /* not the very first frame */
        t = clk_bus() - frame_t0;
        ft_sum += t;
        if (t > ft_max) ft_max = t;
        if (++ft_n >= 64) {                          /* publish the last 64 frames */
            ui_frame_ticks_avg = ft_sum >> 6;
            ui_frame_ticks_max = ft_max;
            ft_sum = 0; ft_n = 0; ft_max = 0;
        }
    }
    gspWaitForVBlank();
    frame_t0 = clk_bus();
    return aptMainLoop();
}

void plat_exit(void)
{
    gfxExit();
}
