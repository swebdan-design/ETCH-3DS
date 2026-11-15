/*
 * ETCH 3DS -- by Synth Centric
 * A headless test harness on Azahar (the Citra successor), through Azahar's
 * libretro core (src/citra_libretro), the way ETCH DS's harness drives
 * melonDS.  It loads the core, boots a .3dsx on the software renderer and
 * runs a script, frame by frame:
 *
 *   <frame> press   A B X Y L R ZL ZR START SELECT UP DOWN LEFT RIGHT
 *   <frame> release <same>
 *   <frame> lstick  <x> <y>      Circle Pad, -32767..32767 (right, down +)
 *   <frame> rstick  <x> <y>      C-stick
 *   <frame> touch   <x> <y>      bottom screen pixel (0..319, 0..239)
 *   <frame> untouch
 *   <frame> shot    <name>       both screens -> OUT/<name>.ppm
 *   <frame> stats                the stats mailbox -> a line of OUT/stats.txt
 *   <frame> end
 *
 * It records every sample the emulated DSP outputs to OUT/audio.wav, and
 * reads RAM through the core's memory map (Azahar's map, patched in
 * tools/azahar_harness/azahar.patch to include the app's .data/.bss), so the
 * stats come straight from the running program at the ELF address of
 * `etch_stats`.
 *
 *   harness CORE.so APP.3dsx SAVEDIR OUTDIR STATS_ADDR SCRIPT [new3ds|old3ds]
 */
#include <dlfcn.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libretro.h"

static const char *save_dir, *out_dir, *model = "Old 3DS";
static uint32_t stats_addr;

/* ---- the core ----------------------------------------------------------------- */
static void (*c_init)(void);
static void (*c_deinit)(void);
static void (*c_run)(void);
static bool (*c_load)(const struct retro_game_info *);
static void (*c_set_env)(retro_environment_t);
static void (*c_set_video)(retro_video_refresh_t);
static void (*c_set_audio)(retro_audio_sample_t);
static void (*c_set_audio_batch)(retro_audio_sample_batch_t);
static void (*c_set_poll)(retro_input_poll_t);
static void (*c_set_state)(retro_input_state_t);
static void (*c_av_info)(struct retro_system_av_info *);

/* ---- memory map --------------------------------------------------------------- */
static struct retro_memory_descriptor mdesc[64];
static unsigned n_mdesc;

static void *vaddr(uint32_t a, uint32_t n)
{
    unsigned i;
    for (i = 0; i < n_mdesc; i++)
        if (a >= mdesc[i].start && a + n <= mdesc[i].start + mdesc[i].len)
            return (uint8_t *)mdesc[i].ptr + (a - mdesc[i].start);
    return NULL;
}

/* ---- environment ----------------------------------------------------------- */
static void log_cb(enum retro_log_level level, const char *fmt, ...)
{
    va_list a;
    if (level < RETRO_LOG_WARN && !getenv("HARNESS_VERBOSE")) return;
    va_start(a, fmt);
    vfprintf(stderr, fmt, a);
    va_end(a);
}

static const char *option(const char *key)
{
    if (!strcmp(key, "citra_graphics_api")) return "Software";
    if (!strcmp(key, "citra_is_new_3ds")) return model;
    /* Azahar's New 3DS mode changes memory and resources, not the CPU clock:
     * emulate the New 3DS's 804 MHz ARM11 as 300 % of the 268 MHz clock */
    if (!strcmp(key, "citra_cpu_clock_percentage")) return !strcmp(model, "New 3DS") ? "300" : "100";
    if (!strcmp(key, "citra_use_cpu_jit")) return "enabled";
    if (!strcmp(key, "citra_audio_emulation")) return "hle";
    if (!strcmp(key, "citra_analog_function")) return "c_stick";
    if (!strcmp(key, "citra_layout_option")) return "default";
    if (!strcmp(key, "citra_use_virtual_sd")) return "enabled";
    if (!strcmp(key, "citra_use_libretro_save_path")) return "LibRetro Default";
    if (!strcmp(key, "citra_enable_touch_touchscreen")) return "enabled";
    if (!strcmp(key, "citra_enable_mouse_touchscreen")) return "disabled";
    if (!strcmp(key, "citra_enable_touch_pointer_timeout")) return "disabled";
    if (!strcmp(key, "citra_render_3d")) return "Off";
    return NULL;
}

static bool env(unsigned cmd, void *data)
{
    switch (cmd) {
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        struct retro_variable *v = data;
        v->value = option(v->key);
        return v->value != NULL;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE: *(bool *)data = false; return true;
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY: *(const char **)data = save_dir; return true;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: return true;
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: ((struct retro_log_callback *)data)->log = log_cb; return true;
    case RETRO_ENVIRONMENT_SET_MEMORY_MAPS: {
        const struct retro_memory_map *m = data;
        unsigned i;
        n_mdesc = m->num_descriptors > 64 ? 64 : m->num_descriptors;
        for (i = 0; i < n_mdesc; i++) mdesc[i] = m->descriptors[i];
        return true;
    }
    case RETRO_ENVIRONMENT_GET_CAN_DUPE: *(bool *)data = true; return true;
    case RETRO_ENVIRONMENT_SET_HW_RENDER: return false;      /* software renderer */
    case RETRO_ENVIRONMENT_GET_PREFERRED_HW_RENDER: *(unsigned *)data = RETRO_HW_CONTEXT_NONE; return true;
    default: return false;
    }
}

/* ---- video ------------------------------------------------------------------ */
static uint32_t *frame;
static unsigned fw, fh;
static void video(const void *data, unsigned w, unsigned h, size_t pitch)
{
    unsigned y;
    if (!data) return;                            /* dupe */
    if (w != fw || h != fh) { free(frame); frame = malloc((size_t)w * h * 4); fw = w; fh = h; }
    for (y = 0; y < h; y++) memcpy(frame + (size_t)y * w, (const uint8_t *)data + y * pitch, w * 4);
}

static void shot(const char *name)
{
    char p[512];
    FILE *f;
    unsigned i;
    if (!frame) return;
    snprintf(p, sizeof p, "%s/%s.ppm", out_dir, name);
    f = fopen(p, "wb");
    if (!f) return;
    fprintf(f, "P6\n%u %u\n255\n", fw, fh);
    for (i = 0; i < fw * fh; i++) {
        uint8_t rgb[3] = { (uint8_t)(frame[i] >> 16), (uint8_t)(frame[i] >> 8), (uint8_t)frame[i] };
        fwrite(rgb, 3, 1, f);
    }
    fclose(f);
}

/* ---- audio ------------------------------------------------------------------ */
static int16_t *pcm;
static size_t n_pcm, cap_pcm;
static size_t audio_batch(const int16_t *d, size_t frames)
{
    if (n_pcm + frames > cap_pcm) { cap_pcm = (n_pcm + frames) * 2; pcm = realloc(pcm, cap_pcm * 4); }
    memcpy(pcm + n_pcm * 2, d, frames * 4);
    n_pcm += frames;
    return frames;
}
static void audio_one(int16_t l, int16_t r) { int16_t s[2] = { l, r }; audio_batch(s, 1); }

static void write_wav(double rate)
{
    char p[512];
    FILE *f;
    uint32_t sr = (uint32_t)(rate + 0.5), data = (uint32_t)(n_pcm * 4), riff = 36 + data, fl = 16, br = sr * 4;
    uint16_t fmt = 1, ch = 2, ba = 4, bits = 16;
    snprintf(p, sizeof p, "%s/audio.wav", out_dir);
    f = fopen(p, "wb");
    if (!f) return;
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
    fwrite(&fl, 4, 1, f); fwrite(&fmt, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&sr, 4, 1, f);
    fwrite(&br, 4, 1, f); fwrite(&ba, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f); fwrite(pcm, 4, n_pcm, f);
    fclose(f);
}

/* ---- input ------------------------------------------------------------------ */
static uint32_t buttons;
static int lx, ly, rx, ry, tx = -1, ty;
static const struct { const char *n; unsigned id; } bmap[] = {
    { "A", RETRO_DEVICE_ID_JOYPAD_A }, { "B", RETRO_DEVICE_ID_JOYPAD_B },
    { "X", RETRO_DEVICE_ID_JOYPAD_X }, { "Y", RETRO_DEVICE_ID_JOYPAD_Y },
    { "L", RETRO_DEVICE_ID_JOYPAD_L }, { "R", RETRO_DEVICE_ID_JOYPAD_R },
    { "ZL", RETRO_DEVICE_ID_JOYPAD_L2 }, { "ZR", RETRO_DEVICE_ID_JOYPAD_R2 },
    { "START", RETRO_DEVICE_ID_JOYPAD_START }, { "SELECT", RETRO_DEVICE_ID_JOYPAD_SELECT },
    { "UP", RETRO_DEVICE_ID_JOYPAD_UP }, { "DOWN", RETRO_DEVICE_ID_JOYPAD_DOWN },
    { "LEFT", RETRO_DEVICE_ID_JOYPAD_LEFT }, { "RIGHT", RETRO_DEVICE_ID_JOYPAD_RIGHT },
};

static void poll(void) { }
static int16_t state(unsigned port, unsigned device, unsigned index, unsigned id)
{
    if (port) return 0;
    if (device == RETRO_DEVICE_JOYPAD) {
        if (id == RETRO_DEVICE_ID_JOYPAD_MASK) return (int16_t)buttons;
        return (buttons >> id) & 1;
    }
    if (device == RETRO_DEVICE_ANALOG) {
        if (index == RETRO_DEVICE_INDEX_ANALOG_LEFT) return (int16_t)(id == RETRO_DEVICE_ID_ANALOG_X ? lx : ly);
        if (index == RETRO_DEVICE_INDEX_ANALOG_RIGHT) return (int16_t)(id == RETRO_DEVICE_ID_ANALOG_X ? rx : ry);
    }
    if (device == RETRO_DEVICE_POINTER) {
        /* the default layout: top 400x240 over bottom 320x240 centred, in a
         * buffer of fw x fh; pointer coordinates span -0x7fff..0x7fff */
        int bx = (int)(fw - 320) / 2, by = (int)fh / 2;
        if (tx < 0 || !fw) return 0;
        if (id == RETRO_DEVICE_ID_POINTER_PRESSED) return 1;
        if (id == RETRO_DEVICE_ID_POINTER_X) return (int16_t)(((bx + tx + 0.5) * 2.0 * 0x7fff) / fw - 0x7fff);
        if (id == RETRO_DEVICE_ID_POINTER_Y) return (int16_t)(((by + ty * (fh / 2) / 240.0 + 0.5) * 2.0 * 0x7fff) / fh - 0x7fff);
    }
    return 0;
}

static unsigned button_bits(char *list)
{
    unsigned b = 0, i;
    char *t;
    for (t = strtok(list, " \t\n"); t; t = strtok(NULL, " \t\n"))
        for (i = 0; i < sizeof bmap / sizeof bmap[0]; i++)
            if (!strcmp(t, bmap[i].n)) b |= 1u << bmap[i].id;
    return b;
}

/* ---- stats -------------------------------------------------------------------- */
static FILE *stats_f;
static void stats_line(unsigned fr)
{
    const uint32_t *m = stats_addr ? vaddr(stats_addr, 64 * 4) : NULL;
    int i;
    if (!stats_f) {
        char p[512];
        snprintf(p, sizeof p, "%s/stats.txt", out_dir);
        stats_f = fopen(p, "w");
    }
    if (!stats_f) return;
    fprintf(stats_f, "%u", fr);
    if (!m) fprintf(stats_f, " UNMAPPED");
    else for (i = 0; i < 64; i++) fprintf(stats_f, " %u", m[i]);
    fprintf(stats_f, "\n");
    fflush(stats_f);
}

#define SYM(v, n) do { *(void **)&v = dlsym(h, n); if (!v) { fprintf(stderr, "missing %s\n", n); return 1; } } while (0)

int main(int argc, char **argv)
{
    void *h;
    struct retro_game_info gi;
    struct retro_system_av_info av;
    FILE *sc;
    char line[512];
    unsigned fr = 0, next_fr = 0, last = 0;
    if (argc < 7) { fprintf(stderr, "usage: harness CORE.so APP.3dsx SAVEDIR OUTDIR STATS_ADDR SCRIPT [new3ds|old3ds]\n"); return 2; }
    save_dir = argv[3];
    out_dir = argv[4];
    stats_addr = (uint32_t)strtoul(argv[5], NULL, 0);
    if (argc > 7 && !strcmp(argv[7], "new3ds")) model = "New 3DS";
    else model = "Old 3DS";
    h = dlopen(argv[1], RTLD_NOW);
    if (!h) { fprintf(stderr, "%s\n", dlerror()); return 1; }
    SYM(c_init, "retro_init"); SYM(c_deinit, "retro_deinit"); SYM(c_run, "retro_run");
    SYM(c_load, "retro_load_game"); SYM(c_set_env, "retro_set_environment");
    SYM(c_set_video, "retro_set_video_refresh"); SYM(c_set_audio, "retro_set_audio_sample");
    SYM(c_set_audio_batch, "retro_set_audio_sample_batch"); SYM(c_set_poll, "retro_set_input_poll");
    SYM(c_set_state, "retro_set_input_state"); SYM(c_av_info, "retro_get_system_av_info");
    c_set_env(env);
    c_set_video(video);
    c_set_audio(audio_one);
    c_set_audio_batch(audio_batch);
    c_set_poll(poll);
    c_set_state(state);
    c_init();
    memset(&gi, 0, sizeof gi);
    gi.path = argv[2];
    if (!c_load(&gi)) { fprintf(stderr, "load failed\n"); return 1; }
    c_av_info(&av);
    fprintf(stderr, "harness: %s, %ux%u, %.3f fps, audio %.1f Hz, %u memory regions\n",
            model, av.geometry.base_width, av.geometry.base_height, av.timing.fps, av.timing.sample_rate, n_mdesc);
    sc = fopen(argv[6], "r");
    if (!sc) { fprintf(stderr, "no script\n"); return 1; }
    for (;;) {
        char act[32] = "", rest[400] = "";
        int got = 0;
        if (fgets(line, sizeof line, sc)) {
            if (line[0] == '#' || line[0] == '\n') continue;
            got = sscanf(line, "%u %31s %399[^\n]", &next_fr, act, rest);
        } else { next_fr = last; strcpy(act, "end"); got = 2; }
        if (got < 2) continue;
        while (fr < next_fr) { c_run(); fr++; }
        if (!strcmp(act, "press")) buttons |= button_bits(rest);
        else if (!strcmp(act, "release")) buttons &= ~button_bits(rest);
        else if (!strcmp(act, "lstick")) sscanf(rest, "%d %d", &lx, &ly);
        else if (!strcmp(act, "rstick")) sscanf(rest, "%d %d", &rx, &ry);
        else if (!strcmp(act, "touch")) sscanf(rest, "%d %d", &tx, &ty);
        else if (!strcmp(act, "untouch")) tx = -1;
        else if (!strcmp(act, "shot")) shot(rest);
        else if (!strcmp(act, "stats")) stats_line(fr);
        else if (!strcmp(act, "end")) break;
        last = next_fr;
    }
    write_wav(av.timing.sample_rate);
    fprintf(stderr, "harness: %u frames, %zu audio frames (%.2f s)\n", fr, n_pcm, n_pcm / av.timing.sample_rate);
    if (stats_f) fclose(stats_f);
    c_deinit();
    return 0;
}
