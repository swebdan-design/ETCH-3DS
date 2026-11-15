/*
 * ETCH 3DS -- by Synth Centric
 * The audio thread and ndsp streaming.
 *
 * Decided with the project owner: one high-priority audio thread (0x18,
 * above the main thread's 0x30) on core 0, the application core, beside the
 * UI.  Old 3DS core 1 is time-sliced by APT_SetAppCpuTimeLimit, so synthesis
 * stays off it.  Because the audio thread and the UI share core 0 and the
 * audio thread has the higher priority, a block render is never interrupted
 * by the UI -- the same guarantee the DS render interrupt had over its main
 * loop, which the shared core's main-loop code (foundry_rebuild's buffer
 * swap, the edit hand-offs) relies on.
 *
 * Streaming, as on the DS: one looping ring that the DSP plays forever and
 * the renderer writes ahead of.  One stereo PCM16 ndsp channel at the DSP's
 * own rate (SYSCLOCK_SOC / 512 = 32 728.5 Hz, the DS rate, no interpolation,
 * so no resampling) plays a single looping wave buffer of RING frames.
 * ndsp calls back once per DSP frame (160 samples, 4.89 ms); the callback
 * only signals a LightEvent.  The audio thread wakes, reads the play
 * position, and renders blocks into the ring until it is LEAD samples ahead
 * of the speaker again.
 *
 * Why not a queue of wave buffers: measured in Azahar, each new wave buffer
 * only starts on the DSP frame after the one in which the previous buffer
 * ended, so every buffer boundary costs silence (128-sample buffers lost 32
 * samples a frame, 160-sample ones every other frame).  A looping buffer
 * has no boundaries, and it is how the DS streamed too.
 *
 * Dropouts: the play position is unwrapped into an absolute count.  If it
 * ever passes the write position the DSP played stale audio: that is a
 * dropout, counted in SH->dropouts, and the writer jumps ahead.
 *
 * Latency, sequencer to DSP: a block starts playing when the speaker reaches
 * the write position, so at each render it is the write position minus the
 * play position.
 */
#include <3ds.h>
#include <string.h>
#include "audio_pump.h"
#include "audio3ds.h"

#define RING (8 * AUDIO_BLOCK)                /* 1024 frames, 31 ms */
static ndspWaveBuf wb;
static int16_t *pcm;
static LightEvent wake;
static Thread th;
static volatile bool run;
static shared_t *ash;
static uint32_t played, written;             /* absolute frames */
static uint32_t last_pos;

volatile audio3ds_stats_t a3s;
uint8_t audio3ds_depth = AUDIO3DS_DEPTH_DEFAULT;

static void dsp_frame(void *unused)
{
    (void)unused;
    LightEvent_Signal(&wake);
}

static void fill(void)
{
    uint32_t pos = ndspChnGetSamplePos(0) % RING, lead = (uint32_t)audio3ds_depth * AUDIO_BLOCK;
    played += (pos - last_pos + RING) % RING;          /* unwrap the play position */
    last_pos = pos;
    if ((int32_t)(written - played) < 0) {             /* the DSP overtook the writer */
        ash->dropouts++;
        written = played;
    }
    if (a3s.started) {
        uint32_t ahead = (written - played) / AUDIO_BLOCK;
        if (ahead < a3s.queued_min) a3s.queued_min = ahead;
    }
    while (written - played < lead) {
        int16_t *dst = pcm + (written % RING) * 2;
        uint32_t lat = written - played;               /* ahead of the speaker */
        if (a3s.started) {
            a3s.lat_sum += lat;
            a3s.lat_n++;
            if (lat > a3s.lat_max) a3s.lat_max = lat;
            if (lat < a3s.lat_min) a3s.lat_min = lat;
        }
        pump_block(dst);
        DSP_FlushDataCache(dst, AUDIO_BLOCK * 4);
        written += AUDIO_BLOCK;
        a3s.blocks++;
    }
    a3s.started = 1;
}

static void audio_thread(void *arg)
{
    (void)arg;
    while (run) {
        LightEvent_Wait(&wake);
        if (!run) break;
        a3s.wakes++;
        fill();
    }
}

int audio3ds_start(shared_t *sh)
{
    float mix[12];
    ash = sh;
    memset((void *)&a3s, 0, sizeof a3s);
    a3s.queued_min = 0xFFFFFFFFu;
    a3s.lat_min = 0xFFFFFFFFu;
    if (R_FAILED(ndspInit())) { a3s.no_dsp = 1; return 0; }
    pcm = (int16_t *)linearAlloc(RING * 4);
    if (!pcm) { ndspExit(); a3s.no_dsp = 1; return 0; }
    memset(pcm, 0, RING * 4);
    DSP_FlushDataCache(pcm, RING * 4);
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    ndspChnReset(0);
    ndspChnSetInterp(0, NDSP_INTERP_NONE);
    ndspChnSetRate(0, NDSP_SAMPLE_RATE);                   /* 32 728.5 Hz: 1:1 */
    ndspChnSetFormat(0, NDSP_FORMAT_STEREO_PCM16);
    memset(mix, 0, sizeof mix);
    mix[0] = mix[1] = 1.0f;
    ndspChnSetMix(0, mix);
    /* prime: the renderer starts LEAD ahead of a silent ring */
    played = written = last_pos = 0;
    memset(&wb, 0, sizeof wb);
    wb.data_pcm16 = pcm;
    wb.nsamples = RING;
    wb.looping = true;
    LightEvent_Init(&wake, RESET_ONESHOT);
    run = true;
    /* core 0, priority 0x18 (main thread 0x30, ndsp's thread 0x2F) */
    th = threadCreate(audio_thread, NULL, 32 * 1024, 0x18, 0, false);
    if (!th) { run = false; linearFree(pcm); ndspExit(); a3s.no_dsp = 1; return 0; }
    fill();
    ndspChnWaveBufAdd(0, &wb);
    ndspSetCallback(dsp_frame, NULL);
    return 1;
}

void audio3ds_stop(void)
{
    if (!run) return;
    run = false;
    LightEvent_Signal(&wake);
    threadJoin(th, U64_MAX);
    threadFree(th);
    ndspSetCallback(NULL, NULL);
    ndspChnWaveBufClear(0);
    ndspExit();
    linearFree(pcm);
}
