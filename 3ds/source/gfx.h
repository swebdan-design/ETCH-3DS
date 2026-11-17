/*
 * ETCH 3DS -- by Synth Centric
 * Drawing for the two screens, ported from ETCH DS (arm9/source/gfx.h).
 *
 * Each screen is one 16-bit bitmap in ordinary RAM, row-major, in ETCH DS's
 * colour format (BGR555 with bit 15 set), so every drawing routine and every
 * colour token is the DS one.  The top screen is 400 x 240, the bottom (touch)
 * screen 320 x 240.  gfx_present() copies what changed since the last frame
 * (a dirty rectangle per screen) into the 3DS framebuffers, converting the
 * colour and rotating, once per frame.
 *
 * Design system (unchanged from ETCH DS): flat surfaces, 1 px borders, 2 px
 * rounded corners, 6 px outer margin, 4 px gaps.  Small grey captions over
 * black values; black is what is active (lit buttons invert to white on
 * black), and steps CORROSION just changed are washed in grey, fading.
 */
#ifndef ETCH_3DS_GFX_H
#define ETCH_3DS_GFX_H
#include <stdint.h>

#ifndef BIT
#define BIT(n) (1u << (n))
#endif
#define RGB15(r, g, b) ((r) | ((g) << 5) | ((b) << 10))
#define C(r, g, b) ((uint16_t)(RGB15(r, g, b) | BIT(15)))

#define TOP_W 400
#define TOP_H 240
#define BOT_W 320
#define BOT_H 240

#define COL_BG      C(31, 31, 31)   /* white screen            */
#define COL_SURF    C(29, 29, 29)   /* panels: barely there     */
#define COL_SURF2   C(27, 27, 27)   /* controls, cells off      */
#define COL_SURF3   C(23, 23, 23)   /* tracks, beat ticks       */
#define COL_BORDER  C(24, 24, 24)
#define COL_TEXT    C(2, 2, 2)      /* values: black            */
#define COL_TEXT2   C(13, 13, 13)   /* captions                 */
#define COL_TEXT3   C(21, 21, 21)   /* disabled / hints         */
#define COL_ACCENT  C(30, 10, 4)    /* active: the series accent #F45423 (SERIES_SPEC.md 4.2); never for text */
#define COL_ACC_DIM C(18, 18, 18)
#define COL_ACC_LO  C(25, 25, 25)
#define COL_NOTE    C(11, 11, 11)   /* notes and hits           */
#define COL_HOT     C(0, 0, 0)      /* accented notes, kick     */
#define COL_EVOLVE  C(30, 10, 4)    /* CORROSION's changes: the accent, fading */
#define COL_GLOW    C(31, 27, 25)   /* evolved column wash: ACC_TINT */
#define COL_RED     C(0, 0, 0)      /* warnings (by inversion)  */
#define COL_PLAY    C(31, 27, 25)   /* playhead column: ACC_TINT */
#define COL_BEAT    C(28, 28, 28)   /* every 4th step           */

enum { F_SMALL, F_MAIN };           /* 5 px caps, 7 px caps    */

extern uint16_t *fb_top, *fb_bot;

void gfx_init(void);
/* push what changed to the screens (once per frame, main thread) */
void gfx_present(void);
/* direct pixel access needs the width of the bitmap it touches */
static inline int gfx_w(const uint16_t *fb) { return fb == fb_top ? TOP_W : BOT_W; }
/* mark a rectangle changed after writing pixels directly */
void gfx_touch(uint16_t *fb, int x, int y, int w, int h);

void gfx_clear(uint16_t *fb, uint16_t c);
void gfx_rect(uint16_t *fb, int x, int y, int w, int h, uint16_t c);
void gfx_frame(uint16_t *fb, int x, int y, int w, int h, uint16_t c);
void gfx_rrect(uint16_t *fb, int x, int y, int w, int h, uint16_t c);    /* rounded */
void gfx_rframe(uint16_t *fb, int x, int y, int w, int h, uint16_t c);
void gfx_hline(uint16_t *fb, int x, int y, int w, uint16_t c);
void gfx_vline(uint16_t *fb, int x, int y, int h, uint16_t c);
void gfx_line(uint16_t *fb, int x0, int y0, int x1, int y1, uint16_t c);
void gfx_disc(uint16_t *fb, int cx, int cy, int r, uint16_t c);
void gfx_ring(uint16_t *fb, int cx, int cy, int r, uint16_t c);
/* arc of radius r (and r-1 when thick), from angle a0 to a1 (1024 = turn,
 * 0 = up, clockwise) */
void gfx_arc(uint16_t *fb, int cx, int cy, int r, int a0, int a1, int thick, uint16_t c);
uint16_t gfx_mix(uint16_t a, uint16_t b, int t);      /* t 0..256 */
void gfx_led(uint16_t *fb, int cx, int cy, int on, uint16_t col);
/* thin 7-segment character: height 2*s+3, width s+4 */
void gfx_seg7(uint16_t *fb, int x, int y, char ch, int s, uint16_t on, uint16_t ghost);
/* blend a rectangle toward a colour (t 0..256): dims unused controls */
void gfx_wash(uint16_t *fb, int x, int y, int w, int h, uint16_t c, int t);

/* text: returns the x after the last glyph */
int  gfx_text_w(int font, const char *s);
int  gfx_text(uint16_t *fb, int x, int y, int font, uint16_t c, const char *s);
void gfx_text_c(uint16_t *fb, int cx, int y, int font, uint16_t c, const char *s);   /* centred */
void gfx_text_r(uint16_t *fb, int rx, int y, int font, uint16_t c, const char *s);   /* right   */
int  gfx_textf(uint16_t *fb, int x, int y, int font, uint16_t c, const char *fmt, ...);
#define FONT_H(f) ((f) == F_SMALL ? 5 : 7)
/* text scaled by an integer factor (wordmarks) */
int  gfx_text_scaled(uint16_t *fb, int x, int y, int font, int scale, uint16_t c, const char *s);
/* word-wrapped paragraph in a column of width w; returns the y after it */
int  gfx_wrap(uint16_t *fb, int x, int y, int w, int font, int line_h, uint16_t c, const char *s);
#endif
