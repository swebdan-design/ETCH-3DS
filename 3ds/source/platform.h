/*
 * ETCH 3DS -- by Synth Centric
 * The platform layer the screens and the main loop see.  It keeps ETCH DS's
 * input vocabulary (KEY_A, KEY_LEFT, scanKeys, keysDown, keysDownRepeat,
 * touchRead...) so the ported screens read like the DS ones, but the values
 * are the 3DS HID bits, and the directions are the D-pad ONLY: the Circle Pad
 * and the C-stick are performance controls (PUNCH), never cursor keys.
 *
 * Screens include this header, never <3ds.h>.
 */
#ifndef ETCH_3DS_PLATFORM_H
#define ETCH_3DS_PLATFORM_H
#include <stdint.h>

/* the 3DS HID bit positions (libctru's KEY_* values).  platform3ds.c, which
 * includes <3ds.h>, defines ETCH_PLATFORM_IMPL and uses libctru's own names. */
#ifndef ETCH_PLATFORM_IMPL
#define KEY_A      (1u << 0)
#define KEY_B      (1u << 1)
#define KEY_SELECT (1u << 2)
#define KEY_START  (1u << 3)
#define KEY_RIGHT  (1u << 4)     /* D-pad */
#define KEY_LEFT   (1u << 5)
#define KEY_UP     (1u << 6)
#define KEY_DOWN   (1u << 7)
#define KEY_R      (1u << 8)
#define KEY_L      (1u << 9)
#define KEY_X      (1u << 10)
#define KEY_Y      (1u << 11)
#define KEY_ZL     (1u << 14)    /* New 3DS */
#define KEY_ZR     (1u << 15)    /* New 3DS */
#define KEY_TOUCH  (1u << 20)
#define touchPosition etch_touch_t
#define touchRead etch_touch_read
#define scanKeys etch_scan_keys
#define keysDown etch_keys_down
#define keysHeld etch_keys_held
#define keysUp etch_keys_up
#define keysDownRepeat etch_keys_down_repeat
#define keysSetRepeat etch_keys_set_repeat
#endif
#define ETCH_KEY_BUTTONS 0x0010CFFFu   /* the buttons and touch: no stick directions */

typedef struct { uint16_t px, py; } etch_touch_t;

void     etch_scan_keys(void);
uint32_t etch_keys_down(void);
uint32_t etch_keys_held(void);
uint32_t etch_keys_up(void);
uint32_t etch_keys_down_repeat(void);
void     etch_keys_set_repeat(uint8_t delay, uint8_t repeat);
void     etch_touch_read(etch_touch_t *t);
/* the Circle Pad, -156..156 each axis (right / up positive) */
void     etch_circle_read(int *dx, int *dy);

/* wall clock in DS bus clocks (33 513 982 Hz: the 3DS tick / 8), for the
 * per-stage CPU meter, which keeps ETCH DS's units */
uint32_t clk_bus(void);

/* frame pacing: present the screens and wait for the next VBlank; returns 0
 * when the system asks the app to quit */
int      frame_end(void);

/* platform facts for the screens */
uint8_t  plat_is_new3ds(void);
extern volatile uint32_t ui_frame_ticks_avg, ui_frame_ticks_max;   /* bus clocks */

#define ETCH_3DS_VERSION "0.1"
#endif
