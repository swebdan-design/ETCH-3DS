# ETCH 3DS — architecture (milestone 1)

ETCH 3DS — by Synth Centric

This document records how ETCH 3DS is built on ETCH DS, and the decisions that would be expensive to undo. Four of them were confirmed with the project owner before any code was written:

* **Two repositories.** ETCH-DS stays as it is. ETCH-3DS (this repository) carries a checked copy of ETCH DS's shared core.
* **The audio thread** runs on core 0 at priority 0x18, above the main thread.
* **Save format v3** will live in its own sidecar file, `/etch_ds/etch_3ds.sav`. Milestone 1 saves nothing 3DS-specific, so the file doesn't exist yet.
* **The emulator** is Azahar, through a headless harness on its libretro core.

## 1. Repository layout

```
core/                ETCH DS's shared core, copied unmodified (tools/sync_core.sh)
  corrosion/         CORROSION from ETCH 2.5 (its own SHA256SUMS, as in ETCH DS)
  common/            ipc.h, seq_core.c/.h: the sequencer
  engine/            ACID, FOUNDRY, FX, the renderer, DSP tables, storage, the boot
                     song, CORROSION scheduling (evolve.c), the edit hand-offs (edit.c)
  tests/             ETCH DS's host tests, unmodified
  SHA256SUMS         every file above; `make` refuses to build if one changes
  UPSTREAM           the ETCH-DS commit it was copied from
3ds/source/          the 3DS platform layer: audio thread, block pump, PUNCH,
                     graphics, input, the ported screens
tests/host/          ETCH 3DS's host tests (parity, PUNCH, screens) + helpers
tools/               sync_core.sh, check_core_upstream.sh, check_audio_path.py,
                     arm_parity.sh, run_azahar.py, azahar_harness/
```

**The core is never edited here.** A fix goes to ETCH-DS first and comes back with `tools/sync_core.sh /path/to/ETCH-DS`. That command rewrites `core/SHA256SUMS` and `core/UPSTREAM`. `make ds-check DS=/path/to/ETCH-DS` proves the copy still matches. "ETCH DS still builds, passes its tests and renders identically" therefore holds by construction: ETCH-DS itself is untouched, and its tests run here against the copy on every `make test`.

**How the 3DS layer reaches into the core without editing it.** The 3DS layer uses link-time wrapping (`-Wl,--wrap=foundry_render,--wrap=fx_limit`, section 4). The rest is plain API: `seq_command`, `seq_render_block`, `audio_render_block`, `acid_p`/`fx_p`, `host_clock`, and the app functions in `edit.c`, `evolve.c` and `demo.c`.

## 2. Threads and cores

```
 core 0 (application core; the only core the audio touches)
 ┌────────────────────────────────────────────────────────────────────────────┐
 │ audio thread, priority 0x18                                                 │
 │   sleeps on a LightEvent; ndsp's callback signals it once per DSP frame      │
 │   (160 samples, 4.89 ms); it then renders blocks into a looping ring until   │
 │   it is 3 blocks ahead of the DSP's play position:                           │
 │     pump_block: commands -> seq_render_block -> audio_render_block (+PUNCH)  │
 │                 -> retro buffer -> interleave -> the ring                     │
 │ ndsp's own thread, priority 0x2F (libctru)                                   │
 │ main thread, priority 0x30, 60 Hz                                            │
 │   input, PUNCH pad, CORROSION (evo_frame), bottom screen (ui.c), top screen, │
 │   copy to the framebuffers, file I/O                                         │
 └────────────────────────────────────────────────────────────────────────────┘
 core 1: unused in milestone 1 (Old 3DS: time-sliced by APT_SetAppCpuTimeLimit)
 cores 2, 3 (New 3DS): reserved for VAPOUR polyphony
```

* **The same guarantees as the DS.** On the DS, the ARM7 owned time and the ARM9 rendered in an interrupt, so the main loop could never delay audio or be caught mid-render by its own code. On the 3DS, the audio thread shares core 0 with the main thread and has the higher priority. A block render therefore always runs to completion before the main thread runs again. That is exactly what the shared core's main-loop code assumes:
  * `foundry_rebuild` swaps sample buffers. On the DS this happens with interrupts off; on the 3DS no lock is needed.
  * `edit.c` uses the `edit_ack` and `blk_played` hand-offs, which spin until the audio side catches up.

  **The audio thread must stay on the same core as the UI.** Moving it to another core would need a lock around the drum-buffer swap. This is noted in `docs/IDEAS.md`.
* **Commands.** On the DS, the ARM9 sent commands to the ARM7 over a FIFO, and they ran between two block interrupts. On the 3DS, `app_send` writes to a single-producer, single-consumer ring of 256 words. The audio thread drains it before it sequences each block, so a command lands at the same point in the block stream as on the DS. A memory barrier orders `SH->edit_pat` (and the other shared fields) before the command that refers to it. The ring never filled in any test.
* **Shared state.** `shared_t` (core/common/ipc.h) is the same structure the DS shares between its two CPUs. On the 3DS it's an ordinary static, with the same writer and reader rules (ETCH DS ARCHITECTURE.md §3). The DS's "ARM7" fields are written by the audio thread, and its "ARM9" fields by the main thread.
* **The audio thread never waits on the UI.** It never allocates, and it never takes a lock. `tools/check_audio_path.py` proves this from the ELF (§6). Beyond the block pump, it reaches only its own `LightEvent` sleep, the play-position read and the cache flush.

## 3. Streaming and latency

* **One stereo PCM16 ndsp channel** at `NDSP_SAMPLE_RATE` = `SYSCLOCK_SOC / 512` = 32 728.5 Hz. That is the DS output rate, and the rate the core is designed for (`FS_HZ`). Interpolation is off, so the DSP plays the samples 1:1 with no resampling.
* **A looping ring, as on the DS.** The channel plays one looping wave buffer of 1 024 frames (8 blocks, 31 ms) forever.
  * On each DSP frame, the audio thread reads the play position (`ndspChnGetSamplePos`) and unwraps it into an absolute count.
  * It then renders blocks into the ring until it is **3 blocks** (`AUDIO3DS_DEPTH_DEFAULT`, 384 samples) ahead of the speaker.
  * This is the DS design: there, the sound hardware looped a ring that the ARM9 wrote two blocks ahead of.
* **Why not a queue of wave buffers.** That was the first design. Measured in Azahar, a new wave buffer only started on the DSP frame *after* the one in which the previous buffer ended, so every buffer boundary cost silence:
  * 128-sample buffers lost 32 samples in every 160-sample frame.
  * 256-sample buffers lost 64 samples in every second frame.
  * 160-sample buffers lost every other frame.

  A looping buffer has no boundaries, and Azahar's DSP re-reads a looping PCM16 buffer every frame, which is exactly the use it's written for. Whether the console's DSP has the same boundary behaviour is unverified. The ring avoids the question either way.
* **Latency** is measured at each render as the write position minus the play position: how far ahead of the speaker the new block is. In the emulator it averaged **8.8 ms** (6.9–10.8 ms) on Old and New 3DS. On top of that comes the DSP's own output stage of about one frame (4.9 ms), so about **11–16 ms** from sequencer to speaker.
* **A dropout** means the play position passed the write position, so the DSP played stale audio. The writer then jumps ahead, and the event counts in `SH->dropouts`, as on the DS. In every emulator run, including the CPU stress test, there were none.
* **The sequencer runs in render time**, exactly as on the DS. `seq_core` advances its phase accumulator per 32-sample sub-block inside `seq_render_block`, so tempo, swing (with ETCH II's fix) and the edit epoch are sample-accurate, whatever the thread scheduling does.
* **The CPU meter keeps ETCH DS's units.** `render.arm.c`'s `host_clock` hook is set to the 3DS tick divided by 8: 268 111 856 / 8 = 33 513 982 Hz, exactly the DS bus clock. Every stage's percentage of a block period is therefore directly comparable with ETCH DS's.

## 4. PUNCH (milestone 1: the architecture and the filter axis)

```
render.arm.c (core, unchanged)
   ACID -> bus
   foundry_render ──(wrap)──> __wrap_foundry_render: drums into a zeroed chunk,
                              copy to the drum-bus retro buffer, add to the bus
   fx_drive, fx_crush, fx_delay, fx_comb
   fx_limit ──(wrap)──> __wrap_fx_limit: PUNCH filter on the bus (if punched in),
                        then the real limiter
pump_block: the limited mix -> the mix retro buffer -> ndsp
```

* **The retro buffer** holds 131 072 frames (4.0 s) of the mix and of the drum bus on its own, as stereo int16. That is one bar at the slowest tempo (60 BPM), and more at every faster one. It's static memory, 1 MB, allocated at boot.
* **The taps are exact.** The drums only ever add into the bus, so rendering them into zeros and adding them gives the same integer sums. While nothing is punched in, `__wrap_fx_limit` touches no sample. The sound-parity test proves both, with the taps linked in.
* **The DJ filter** is a zero-delay-feedback state-variable filter (Simper's trapezoidal SVF) in fixed point: Q30 coefficients, int64 products, int32 states. Its tables (128 cutoffs × 8 resonances) are built at boot in floating point, off the audio path. The audio path has no division and no floating point.
  * Circle Pad left gives a low-pass from 15 kHz down to 90 Hz, with resonance rising with the push. Right gives a high-pass from 30 Hz up to 3.5 kHz.
  * The cutoff glides one eighth of the way to the target every 32 samples.
* **Start.** A punch starts on the sample of the next 16th: the block's `EV_STEP` event offset. It starts at once when playback is stopped or `punch_quantise` is 0. It fades in over 64 samples.
* **Release.** Letting go fades out over 64 samples, and then the filter is bypassed exactly. The sequencer never stopped, so the song is where it would have been.
* **Measured on the host** (`tests/host/test_punch.c`):
  * The output is identical, bit for bit, up to the exact sample of the next 16th.
  * Full low-pass takes the treble down by 20.6 dB, and full high-pass takes the bass down by 41.2 dB.
  * A sweep over every setting never pins the output.

## 5. Screens

* **Two RAM bitmaps.** The top is 400 × 240 and the bottom 320 × 240, in ETCH DS's 16-bit colour format, so `gfx.c` keeps every DS primitive, colour token and both proportional fonts. Each bitmap keeps a dirty rectangle. Once per frame, `gfx_present` copies only what changed into the framebuffers, converting the colour (BGR555 to RGB565 through a 32 K lookup table) and rotating. The framebuffers are single-buffered, because only changed pixels are rewritten.
* **The bottom screen** (`ui.c`) is ETCH DS's `ui.c` with its layout scaled 1.25× (256 × 192 to 320 × 240) and the type kept at pixel size. Every hit test still uses the same constants as the drawing. The tabs are LIVE, XY, PANEL/DRUM, KEYS, GEN and SONG/FILE/ABOUT, with the same grammar. ABOUT has the four pages, updated for the 3DS.
* **The top screen** (`scr_top.c`) holds:
  * the pattern
  * the step strip
  * MODE / PATTERN / GENERATION / NEXT EVO / TEMPO / **PUNCH**
  * the scope, which reads the retro buffer
  * the DSP meter, with ACID, DRUMS, FX, SEQ, PUNCH and UI frame time, latency and dropouts

  It's 2D in milestone 1. citro2d/citro3d come in with the 3D landscape.
* **Input.** `platform.h` keeps ETCH DS's vocabulary (`KEY_A`, `keysDown`, `keysDownRepeat`, `touchRead`), but the directions are **the D-pad only**. The Circle Pad and C-stick never move a cursor: they are PUNCH.
* `tests/host/test_screens.c` draws every screen on the host with the real engine underneath, and writes the screenshots in `docs/images`.

## 6. Proofs (what `make test` and the tools check)

| Claim | How |
|---|---|
| CORROSION is byte-identical | `core/corrosion/SHA256SUMS`, checked by every `make` |
| The shared core is ETCH DS's, unmodified | `core/SHA256SUMS` by every `make`; `make ds-check DS=…` against a checkout |
| ETCH's 44 CORROSION tests pass against the copy | `core/tests/test_generator.c` (unmodified) |
| Every ETCH DS host test passes against the copy | `test_seq` (231), `test_dsp` (81), `test_storage` (56) |
| The DS path and the 3DS path render identically | `tests/host/test_parity.c`: 64 s script, byte-compare |
| …with the consoles' own compilers | `tools/arm_parity.sh`: BlocksDS GCC (ARM9 + ARM7 flags), devkitARM GCC (ARM11), run under qemu-arm, byte-compare |
| No division, float, allocation or lock on the audio path | `tools/check_audio_path.py etch_3ds.elf` |
| The .3dsx plays the same song | `tools/run_azahar.py`: the emulator's recording equals the host render of the same seed, sample for sample, up to the first evolution (where CORROSION takes the moment's entropy, by design) |
| PUNCH, the retro buffer, the command ring | `tests/host/test_punch.c` |

## 7. Memory (milestone 1)

| | Size | Notes |
|---|---|---|
| `.text` + `.rodata` | about 290 KB | wavetables (64 KB) and the other DSP tables, fonts |
| `.bss` | about 1.6 MB | retro buffer (1 MB), screen bitmaps (432 KB), colour LUT (64 KB), delay line (64 KB), shared block |
| heap | under 1 MB | FOUNDRY's drum samples (at most 2 s each) |
| linear heap | 4 KB | the ndsp ring (1 024 stereo frames) |

That's well under the Old 3DS's 64 MB application budget, leaving room for VAPOUR, RUST, mic samples and a longer retro buffer.
