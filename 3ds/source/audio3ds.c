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
 * Streaming: one stereo PCM16 ndsp channel at the DSP's own rate
 * (SYSCLOCK_SOC / 512 = 32 728.5 Hz, the DS rate, no interpolation, so no
 * resampling), fed a ring of NWB wave buffers of one block each.  ndsp calls
 * back once per DSP frame (160 samples, 4.89 ms); the callback only signals
 * a LightEvent.  The audio thread wakes, counts what is still queued, and
 * renders blocks until QUEUE_DEPTH blocks are queued again.  A wake that
 * finds nothing queued is a dropout.
 *
 * Latency, sequencer to DSP: the block being rendered starts playing after
 * everything already queued, so it is measured at each render as the queued
 * samples minus the play position in the current buffer.
 */
#include <3ds.h>
#include <string.h>
#include "audio_pump.h"
#include "audio3ds.h"

#define NWB 8
static ndspWaveBuf wb[NWB];
static int16_t *pcm;
static LightEvent wake;
static Thread th;
static volatile bool run;
static uint32_t next_wb;
static shared_t *ash;

volatile audio3ds_stats_t a3s;
uint8_t audio3ds_depth = AUDIO3DS_DEPTH_DEFAULT;

static void dsp_frame(void *unused)
{
    (void)unused;
    LightEvent_Signal(&wake);
}

static void fill(void)
{
    uint32_t i, queued = 0, qsamples = 0, pos;
    for (i = 0; i < NWB; i++)
        if (wb[i].status == NDSP_WBUF_QUEUED || wb[i].status == NDSP_WBUF_PLAYING) { queued++; qsamples += AUDIO_BLOCK; }
    if (a3s.started && queued == 0) ash->dropouts++;
    if (a3s.started && queued < a3s.queued_min) a3s.queued_min = queued;
    pos = ndspChnGetSamplePos(0);
    while (queued < audio3ds_depth) {
        ndspWaveBuf *w = &wb[next_wb];
        uint32_t lat;
        if (w->status == NDSP_WBUF_QUEUED || w->status == NDSP_WBUF_PLAYING) break;
        lat = qsamples > pos ? qsamples - pos : 0;           /* ahead of the speaker */
        if (a3s.started) {
            a3s.lat_sum += lat;
            a3s.lat_n++;
            if (lat > a3s.lat_max) a3s.lat_max = lat;
            if (lat < a3s.lat_min) a3s.lat_min = lat;
        }
        pump_block(w->data_pcm16);
        DSP_FlushDataCache(w->data_pcm16, AUDIO_BLOCK * 4);
        ndspChnWaveBufAdd(0, w);
        next_wb = (next_wb + 1) % NWB;
        queued++;
        qsamples += AUDIO_BLOCK;
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
    uint32_t i;
    ash = sh;
    memset((void *)&a3s, 0, sizeof a3s);
    a3s.queued_min = 0xFFFFFFFFu;
    a3s.lat_min = 0xFFFFFFFFu;
    if (R_FAILED(ndspInit())) { a3s.no_dsp = 1; return 0; }
    pcm = (int16_t *)linearAlloc(NWB * AUDIO_BLOCK * 4);
    if (!pcm) { ndspExit(); a3s.no_dsp = 1; return 0; }
    memset(pcm, 0, NWB * AUDIO_BLOCK * 4);
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    ndspChnReset(0);
    ndspChnSetInterp(0, NDSP_INTERP_NONE);
    ndspChnSetRate(0, NDSP_SAMPLE_RATE);                   /* 32 728.5 Hz: 1:1 */
    ndspChnSetFormat(0, NDSP_FORMAT_STEREO_PCM16);
    memset(mix, 0, sizeof mix);
    mix[0] = mix[1] = 1.0f;
    ndspChnSetMix(0, mix);
    memset(wb, 0, sizeof wb);
    for (i = 0; i < NWB; i++) {
        wb[i].data_pcm16 = pcm + i * AUDIO_BLOCK * 2;
        wb[i].nsamples = AUDIO_BLOCK;
        wb[i].status = NDSP_WBUF_DONE;
    }
    next_wb = 0;
    LightEvent_Init(&wake, RESET_ONESHOT);
    run = true;
    /* core 0, priority 0x18 (main thread 0x30, ndsp's thread 0x2F) */
    th = threadCreate(audio_thread, NULL, 32 * 1024, 0x18, 0, false);
    if (!th) { run = false; linearFree(pcm); ndspExit(); a3s.no_dsp = 1; return 0; }
    ndspSetCallback(dsp_frame, NULL);
    LightEvent_Signal(&wake);                               /* prime the queue */
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
