/*
 * ETCH 3DS -- by Synth Centric
 * PUNCH (see punch.h): the retro buffer, the DJ filter, and the two
 * link-time taps into the shared core's renderer.
 *
 * The taps.  render.arm.c (core, unchanged) calls foundry_render() for each
 * 32-sample chunk and fx_limit() once per block.  The 3DS build links with
 * -Wl,--wrap=foundry_render,--wrap=fx_limit, so those calls land here:
 *   __wrap_foundry_render renders the drums into a zeroed chunk, copies it
 *       into the drum-bus retro buffer, and adds it to the mix bus.  The
 *       drums only ever add into the bus (L[i] += ...), so this is the same
 *       integer sum, bit for bit.
 *   __wrap_fx_limit runs the PUNCH filter on the mix bus, then the real
 *       limiter.  When nothing is punched in it returns before touching a
 *       sample.
 * The sound-parity test proves both claims by hashing the output.
 */
#include <math.h>
#include <string.h>
#include "punch.h"
#include "audio.h"

void __real_foundry_render(int32_t *L, int32_t *R, uint32_t n);
void __real_fx_limit(int32_t *l, int32_t *r, int16_t *ol, int16_t *or_, uint32_t n);
void __wrap_foundry_render(int32_t *L, int32_t *R, uint32_t n);
void __wrap_fx_limit(int32_t *l, int32_t *r, int16_t *ol, int16_t *or_, uint32_t n);
extern uint32_t (*host_clock)(void);
static inline uint32_t clk(void) { return host_clock ? host_clock() : 0; }

uint32_t punch_ticks, tap_ticks;

/* ---- retro buffer ------------------------------------------------------------ */
static int16_t mix_ring[RETRO_FRAMES * 2];
static int16_t drum_ring[RETRO_FRAMES * 2];
uint32_t retro_frames;                /* mix frames written */
static uint32_t drum_pos;             /* drum frames written */
#define RMASK (RETRO_FRAMES - 1u)

static inline int16_t sat16(int32_t v) { return (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v); }

const int16_t *retro_mix(void) { return mix_ring; }
const int16_t *retro_drums(void) { return drum_ring; }

void retro_read_mono(int16_t *out, uint32_t n)
{
    uint32_t p = retro_frames - n, i;
    for (i = 0; i < n; i++, p++) {
        uint32_t k = (p & RMASK) * 2;
        out[i] = (int16_t)((mix_ring[k] + mix_ring[k + 1]) >> 1);
    }
}

void __wrap_foundry_render(int32_t *L, int32_t *R, uint32_t n)
{
    int32_t dl[AUDIO_BLOCK], dr[AUDIO_BLOCK];
    uint32_t i, t;
    if (n > AUDIO_BLOCK) n = AUDIO_BLOCK;
    for (i = 0; i < n; i++) { dl[i] = 0; dr[i] = 0; }
    __real_foundry_render(dl, dr, n);
    t = clk();
    for (i = 0; i < n; i++) {
        uint32_t k = ((drum_pos + i) & RMASK) * 2;
        L[i] += dl[i];
        R[i] += dr[i];
        drum_ring[k] = sat16(dl[i]);
        drum_ring[k + 1] = sat16(dr[i]);
    }
    drum_pos += n;
    tap_ticks += clk() - t;
}

/* ---- the DJ filter ------------------------------------------------------------ */
#define NCUT 128
#define NRES 8
/* a1, a2, a3 in Q30; k (1/Q) in Q28 */
static int32_t tab_a1[NRES][NCUT], tab_a2[NRES][NCUT], tab_a3[NRES][NCUT], tab_k[NRES];

volatile uint8_t punch_quantise = 1;
volatile uint8_t punch_state, punch_mode;
volatile uint32_t punch_count;
static volatile int16_t req_amount;

static int32_t ic1[2], ic2[2];        /* filter state, L and R */
static int32_t wet;                   /* crossfade gain, Q15 */
static int32_t cut_q8;                /* smoothed cutoff index, Q8 */
static uint8_t res_idx;
static int32_t start_off = -1;        /* sample of this block where the punch starts */
static uint8_t blk_playing;
#define RAMP_STEP 512                 /* 32768 / 64 samples */

void punch_filter(int amount)
{
    if (amount > 255) amount = 255;
    if (amount < -255) amount = -255;
    req_amount = (int16_t)amount;
}

/* boot, main thread: build the coefficient tables (floating point is fine
 * here; nothing on the audio path uses it) */
void punch_init(void)
{
    int c, q;
    for (q = 0; q < NRES; q++) {
        double Q = 0.6 + q * 0.55;                      /* 0.6 .. 4.45 */
        double k = 1.0 / Q;
        tab_k[q] = (int32_t)(k * (1 << 28));
        for (c = 0; c < NCUT; c++) {
            double fc = 30.0 * pow(15000.0 / 30.0, c / 127.0);
            double g = tan(M_PI * fc / (double)FS_HZ);
            double a1 = 1.0 / (1.0 + g * (g + k)), a2 = g * a1, a3 = g * a2;
            tab_a1[q][c] = (int32_t)(a1 * (1 << 30));
            tab_a2[q][c] = (int32_t)(a2 * (1 << 30));
            tab_a3[q][c] = (int32_t)(a3 * (1 << 30));
        }
    }
    memset(mix_ring, 0, sizeof mix_ring);
    memset(drum_ring, 0, sizeof drum_ring);
    retro_frames = drum_pos = 0;
    punch_state = PUNCH_IDLE;
    req_amount = 0;
}

static void target(int amt, int mode, int32_t *cut, uint8_t *res)
{
    if (mode == 1) *cut = (127 - ((amt * 107) >> 8)) << 8;   /* LP: 15 kHz .. 90 Hz */
    else *cut = ((amt * 100) >> 8) << 8;                      /* HP: 30 Hz .. 3.5 kHz */
    *res = (uint8_t)(amt >> 5);
}

void punch_begin_block(const evslot_t *slot, uint32_t blk, uint8_t playing)
{
    int a = req_amount, mode = a < 0 ? 1 : a > 0 ? 2 : 0;
    uint32_t i;
    drum_pos = blk * AUDIO_BLOCK;                     /* drums align with the mix */
    blk_playing = playing;
    start_off = -1;
    switch (punch_state) {
    case PUNCH_IDLE:
    case PUNCH_ARMED:
        if (!mode) { punch_state = PUNCH_IDLE; break; }
        if (!punch_quantise || !playing) start_off = 0;
        else if (slot->block == blk)                   /* the next 16th: the block's step event */
            for (i = 0; i < slot->n && i < EV_MAX; i++)
                if (slot->ev[i].type == EV_STEP) { start_off = slot->ev[i].off; break; }
        if (start_off < 0) { punch_state = PUNCH_ARMED; punch_mode = (uint8_t)mode; break; }
        punch_state = PUNCH_ON;
        punch_mode = (uint8_t)mode;
        punch_count++;
        ic1[0] = ic1[1] = ic2[0] = ic2[1] = 0;
        wet = 0;
        target(a < 0 ? -a : a, mode, &cut_q8, &res_idx);  /* start where the stick is */
        break;
    case PUNCH_ON:
        if (mode != punch_mode) punch_state = PUNCH_RELEASE;   /* let go (or crossed over) */
        break;
    default:
        break;
    }
}

static void filter_run(int32_t *l, int32_t *r, uint32_t from, uint32_t n)
{
    int a = req_amount, amt = a < 0 ? -a : a;
    int32_t tcut;
    uint8_t tres;
    uint32_t i, c;
    if (punch_state == PUNCH_ON && amt) target(amt, punch_mode, &tcut, &tres);
    else { tcut = cut_q8; tres = res_idx; }
    for (c = from; c < n; ) {
        uint32_t end = (c & ~31u) + 32u, ci;
        int32_t a1, a2, a3, k;
        int ch;
        if (end > n) end = n;
        /* control rate (32 samples): glide the cutoff an eighth of the way */
        cut_q8 += (tcut - cut_q8) >> 3;
        res_idx = tres;
        ci = (uint32_t)cut_q8 >> 8;
        if (ci > NCUT - 1) ci = NCUT - 1;
        a1 = tab_a1[res_idx][ci]; a2 = tab_a2[res_idx][ci]; a3 = tab_a3[res_idx][ci]; k = tab_k[res_idx];
        for (i = c; i < end; i++) {
            int32_t *buf[2] = { l, r };
            if (punch_state == PUNCH_RELEASE) { wet -= RAMP_STEP; if (wet < 0) wet = 0; }
            else if (wet < 32768) wet += RAMP_STEP;
            for (ch = 0; ch < 2; ch++) {
                int32_t x = buf[ch][i], v1, v2, v3, y;
                v3 = x - ic2[ch];
                v1 = (int32_t)(((int64_t)a1 * ic1[ch] + (int64_t)a2 * v3) >> 30);
                v2 = ic2[ch] + (int32_t)(((int64_t)a2 * ic1[ch] + (int64_t)a3 * v3) >> 30);
                ic1[ch] = 2 * v1 - ic1[ch];
                ic2[ch] = 2 * v2 - ic2[ch];
                if (punch_mode == 1) y = v2;
                else y = x - (int32_t)(((int64_t)k * v1) >> 28) - v2;
                buf[ch][i] = x + (int32_t)(((int64_t)(y - x) * wet) >> 15);
            }
        }
        c = end;
    }
    if (punch_state == PUNCH_RELEASE && wet == 0) {     /* fully dry again: bypass */
        punch_state = PUNCH_IDLE;
        ic1[0] = ic1[1] = ic2[0] = ic2[1] = 0;
    }
}

void __wrap_fx_limit(int32_t *l, int32_t *r, int16_t *ol, int16_t *or_, uint32_t n)
{
    if (punch_state == PUNCH_ON || punch_state == PUNCH_RELEASE) {
        uint32_t t = clk();
        filter_run(l, r, start_off > 0 ? (uint32_t)start_off : 0u, n);
        punch_ticks += clk() - t;
    }
    start_off = -1;
    __real_fx_limit(l, r, ol, or_, n);
}

void punch_end_block(const int16_t *l, const int16_t *r)
{
    uint32_t i;
    for (i = 0; i < AUDIO_BLOCK; i++) {
        uint32_t k = ((retro_frames + i) & RMASK) * 2;
        mix_ring[k] = l[i];
        mix_ring[k + 1] = r[i];
    }
    retro_frames += AUDIO_BLOCK;
    (void)blk_playing;
}
