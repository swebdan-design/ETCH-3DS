# ETCH 3DS — milestone 1 report: "It squelches in 3D"

ETCH 3DS — by Synth Centric

Milestone 1 ports ETCH DS to the 3DS sound for sound and proves the platform layer. No new instrument was built. Everything below was measured in this repository: on the host, under qemu-arm with both consoles' compilers, and in the Azahar emulator through the headless harness. Nothing has run on real hardware yet (§4).

## 1. What works

| # | Deliverable | Status | Evidence |
|---|---|---|---|
| 1 | One `make` builds a `.3dsx`; the shared core is restructured; ETCH DS still builds and passes its tests | **Done** | Details below. |
| 2 | CORROSION byte-identical; ETCH's 44 tests; every DS host test against the shared core | **Done** | Details below. |
| 3 | Audio thread + ndsp streaming, `seq_core` in render time, step rules, fixed swing clock, edit epoch | **Done** | Details below. |
| 4 | Sound parity: host test, DS vs 3DS configuration; the emulator plays the same song | **Done** | Details below. |
| 5 | Title screen, then CORRODE from `0F1C-9009-5870` in DRIFT | **Done** | Details below. |
| 6 | The ETCH DS UI ported to 400 × 240 and 320 × 240, with ABOUT's four pages | **Done** | Details below. |
| 7 | Circle Pad as the PUNCH filter axis (momentary, quantised), with the retro buffer | **Done** | Details below. |
| 8 | ETCH DS v1/v2 saves load from `/etch_ds/etch_ds.sav`; ETCH Game Boy import | **Done** | Details below. |
| 9 | CPU per stage on Old and New 3DS configurations; headroom for OXIDE + VAPOUR | **Done** (emulator) | §2 |

**1. Build.**
* `make` checks `core/` and CORROSION against their checksums, then builds `etch_3ds.3dsx` with devkitARM and libctru, natively or through `make docker`.
* ETCH-DS itself is untouched: the two-repository layout was chosen by the owner. Its ROM rebuilds byte-identical to the released `etch_ds.nds` (sha256 `a798a333…`).
* `make ds-check DS=…` proves `core/` matches ETCH-DS byte for byte (34 files).

**2. Tests against the shared core.**
* `core/corrosion/SHA256SUMS` and `core/SHA256SUMS` are checked by every `make`.
* ETCH's 44 CORROSION tests pass, 0 failures.
* ETCH DS's sequencer (231), DSP (81) and save (56) tests pass against `core/`, 0 failures.

**3. Audio thread.**
* The thread runs at priority 0x18 on core 0 and streams to ndsp through a looping ring at 32 728.5 Hz, 1:1.
* `seq_core` runs unchanged inside the block pump, with a UI → audio command ring.
* In the emulator: 0 dropouts and 0 late gates in every run, and 8.8 ms average lead (§2).

**4. Sound parity.**
* **Host:** 64 s of the boot song plus a script exercising every command (`test_parity`) is byte-identical through the DS path and the 3DS path, with the PUNCH taps linked in: 8 388 608 bytes.
* **Console compilers:** the same holds with BlocksDS's GCC (DS ARM9 and ARM7 flags) and devkitARM's GCC (ARM11), both run under qemu-arm (`tools/arm_parity.sh`).
* **Emulator:** Azahar's recording of the `.3dsx` equals the host render, sample for sample, up to the first evolution (§2).

**5. Title.**
* Pressing START with no touch starts playback, as the emulator runs show.
* The boot pattern is CORROSION's GEN ALL on that code, an ETCH DS host check.
* DRIFT evolved 3–5 times in every 30–40 s emulator run.

**6. UI port.**
* Tabs: LIVE, XY, PANEL/DRUM, KEYS, GEN, SONG/FILE and ABOUT, with the same grammar. The D-pad is the cursor; the Circle Pad is never a cursor.
* Every screen is drawn on the host (`test_screens`, `docs/images/screens.png`) and in the emulator (`docs/images/azahar.png`).

**7. PUNCH.**
* **Host:** the output is bit-identical to the unpunched song up to the exact sample of the next 16th. Full low-pass is −20.6 dB of treble; full high-pass is −41.2 dB of bass. A sweep over every setting is stable. Release returns to exact bypass within a block, and the sequencer never stops underneath.
* **Retro buffer:** 4.0 s (one bar at 60 BPM) of the mix and of the drum bus, checked equal to the output.
* **Emulator:** Circle Pad left then right started 2 punches, and the top screen showed "LP 100%".

**8. Saves.**
* **Emulator, with the buttons only:** an ETCH DS save v2 (slot 01, 133 BPM), a save v1 (slot 02, 97 BPM) and an ETCH Game Boy save (slot 1, 111 BPM) were written to the emulated SD card by ETCH DS's own storage code. Each loaded or imported, and the app then reported the saved tempo.
* Saving from the 3DS uses the same code (`test_screens` saves a slot).

## 2. Measurements

All CPU figures are percentages of one block period (128 samples, 3.91 ms) on core 0, in ETCH DS's units (DS bus clocks = 3DS tick / 8). Each is the 256-block window average read from the running program's RAM (the stats mailbox).

* **Old 3DS:** 268 MHz.
* **New 3DS:** Azahar's New 3DS mode with its CPU clock at 300 %, standing in for the New 3DS's 804 MHz. Azahar's own New 3DS mode changes only memory and resources, not the clock.
* **Song:** the boot song in DRIFT.
* **Stress:** PANEL → CPU TEST, which fires ACID and all six drum lanes on every 16th with every FX stage working, with the PUNCH filter held on top.

| Stage | Old 3DS, song | Old 3DS, stress | New 3DS, song | New 3DS, stress |
|---|---|---|---|---|
| events + bus clear | 0.10 % | 0.10 % | 0.03 % | 0.03 % |
| ACID | 1.29 % | 2.03 % | 0.43 % | 0.67 % |
| FOUNDRY, 6 lanes | 1.60 % | 3.06 % | 0.55 % | 1.03 % |
| FX: drive | 0.64 % | 0.64 % | 0.21 % | 0.21 % |
| FX: crush | 0.03 % | 0.27 % | 0.01 % | 0.09 % |
| FX: delay | 0.56 % | 0.56 % | 0.18 % | 0.18 % |
| FX: comb | 0.39 % | 0.39 % | 0.13 % | 0.13 % |
| FX: limiter | 0.56 % | 2.15 % | 0.18 % | 0.71 % |
| out | 0.03 % | 0.03 % | 0.01 % | 0.01 % |
| sequencer + command ring | 0.05 % | 0.05 % | 0.01 % | 0.01 % |
| **PUNCH filter** | 0.00 % (not punched) | **1.55 %** | 0.00 % | 0.51 % |
| drum-bus tap | 0.50 % | 0.49 % | 0.16 % | 0.16 % |
| retro buffer + interleave | 0.24 % | 0.24 % | 0.08 % | 0.08 % |
| **audio thread total** | **6.0 %** | **11.6 %** (worst block 9.5 %, worst window 11.6 %) | **2.0 %** | **3.9 %** |
| **UI frame** (input, CORROSION, both screens, copy) | 1.1 ms avg, 3.8 ms worst of 16.7 ms | 1.1 ms, 4.1 ms worst | 0.4 ms, 1.3 ms worst | 0.4 ms, 1.3 ms worst |

**Latency**, from the sequencer to the DSP (the rendered block's lead over the play position):
* 8.8 ms average, 6.9–10.8 ms, in every run.
* Add the DSP's output stage of about one 4.9 ms frame: about **11–16 ms** to the speaker, against the 10–15 ms target.
* The lead is one setting (`audio3ds_depth`, in blocks). 2 blocks would be about 7–12 ms if hardware allows.

**Dropouts:** **0** in every emulator run.
* That's 10 runs of 30–40 s: song, PUNCH, stress, tour of every tab, and save loading, on Old and New 3DS.
* There were 0 late gates (gate points that fired late) out of 246–331 per run.
* No silent gaps appeared in any recording.
* The command ring never filled.

**Sound parity:**

| Comparison | Result |
|---|---|
| Host: DS path vs 3DS path (pump, ring, PUNCH taps), 64 s script | identical, 8 388 608 bytes, hash `de61240b50378482` |
| BlocksDS GCC 16.2 (ARM9 arm946e-s + ARM7 arm7tdmi flags) under qemu-arm | identical to the host |
| devkitARM GCC 16.1 (ARM11 MPCore, hard-float) under qemu-arm, PUNCH taps linked | identical to the host |
| Azahar: the `.3dsx`'s audio vs the host render of the same seed | identical, sample for sample, for the first 7.70–7.74 s |

**Why the emulator comparison stops at 7.7 s.** That is the first DRIFT evolution, at step 16's gate point of bar 4. At each mutation, CORROSION's random stream stirs in the moment's entropy (`evo_random()` uses `ui_entropy ^ hw_noise()`, a free-running timer). That's true on ETCH, ETCH II and ETCH DS too, so from the first evolution each listen differs, as the living song intends. Making the emulator check run longer would need a deterministic `hw_noise` in a test build.

**Two harness findings, both fixed:**
* The host reference needs a pre-roll of silence before START. On the console, ACID's smoothed controls settle during the title screen. Without the pre-roll, 2 % of the samples differ slightly in the first second.
* Separate wave buffers lost audio at every boundary in Azahar, which is why streaming became a looping ring (ARCHITECTURE.md §3).

**Memory:** about 290 KB of code and tables, 1.6 MB of `.bss` (of which 1 MB is the retro buffer and 432 KB the screen bitmaps), under 1 MB of heap for drum samples, and a 4 KB DSP ring. That's well under the Old 3DS's 64 MB.

### Headroom for OXIDE + VAPOUR on Old 3DS

**What the emulator shows.** It counts one cycle per instruction, and that agrees with the DS port: ACID's 1.29 % at 268 MHz is 5.2 % at the DS's 67 MHz, against 5.5 % measured in melonDS. Real ARM11 code needs more than one cycle per instruction: multiplies, the 64 KB of wavetables in a 16 KB data cache, branch mispredictions. **Plan on about 2× the emulator's figures** until hardware says otherwise.

| Old 3DS, core 0 | Emulator | Planning figure (2×) |
|---|---|---|
| audio thread, worst case (stress + PUNCH) | 11.6 % | about 23 % |
| UI, average / worst frame | 6.6 % / 25 % | about 13 % / 50 % |
| **free for new engines, on average** | **about 80 %** | **about 60 %** |

**What the new engines need.**
* **OXIDE** is a control-rate composer that runs on the main thread when the pattern changes, like CORROSION. Its audio cost is zero.
* **VAPOUR, paraphonic on Old 3DS:** 4 voices × 2–3 band-limited oscillators through one filter, plus an echo and a small plate. By ACID's measured costs, that's about 4–6 % of a block in the emulator, or 8–12 % planned.
* **The reverb and sidechain ducking** are about 2–3 % more.

**Verdict.** OXIDE + VAPOUR + reverb fit on Old 3DS core 0 with roughly half the core still free at the planning figure. Full 8-voice VAPOUR with a filter per voice belongs on the New 3DS (≈3× the clock and two more cores), as the design says. If the UI's worst frames matter later, the cheapest win is drawing less per frame (the scope and stats alternate already), not the audio path.

## 3. Decisions recorded

* **Repository layout:** two repositories, as the owner decided. ETCH-3DS carries `core/`, a checksummed, unmodified copy of ETCH DS's shared core. `tools/sync_core.sh` brings changes over, and `make ds-check` proves they match.
* **Audio thread:** priority 0x18 on core 0, beside the UI, as the owner decided. Because both share a core and audio has the higher priority, renders are atomic relative to the UI, which the shared core's main-loop code relies on (`foundry_rebuild`'s buffer swap).
* **Save format v3:** a sidecar `/etch_ds/etch_3ds.sav`, as the owner decided. Nothing 3DS-specific is saved in milestone 1, so it isn't written yet. `etch_ds.sav` stays exactly ETCH DS's format.
* **Emulator:** Azahar, as the owner decided, driven through its libretro core by a headless C frontend (`tools/azahar_harness`) with a one-hunk patch that exposes the app's data in the core's memory map.
* **No citro2d in milestone 1.** The screens are ETCH DS's bitmap renderer on RAM bitmaps, copied to the framebuffers by dirty rectangle. That keeps every primitive, font and colour of the DS design system, and costs about 1 ms a frame. citro3d comes in with the 3D landscape.
* **The PUNCH filter is fixed point.** It's cheap enough that VFP wasn't needed, so the whole audio path stays division- and float-free, checked by `tools/check_audio_path.py`.

## 4. What could not be verified

* **Real hardware.** Nothing has run on an Old or New 3DS. Still to confirm there:
  * the DSP's own behaviour with a looping buffer that's rewritten ahead of the play position. This is the standard streaming technique, but unverified here.
  * true CPU cost (see the 2× planning factor)
  * ndsp callback jitter
  * SD-card write timing
* **Flash carts, CFW setups and the Homebrew Launcher variants.** Untested. The DSP firmware dump (`sdmc:/3ds/dspfirm.cdc`) must exist for sound. The emulator runs used a placeholder, because Azahar's HLE DSP ignores it. Without it the app says so on the title and runs silently.
* **Emulator timing fidelity.** Azahar counts one tick per instruction and doesn't model the New 3DS clock, so New 3DS was emulated as 300 % CPU clock. Its HLE DSP isn't the real firmware.
* **The 3D slider, tilt, the C-stick and ZL/ZR** are unused in milestone 1.
* **A `.cia`** wasn't built.

## 5. Open questions, with recommendations

1. **Repository layout: monorepo, or a separate repository with a submodule?**
   * **Decided: two repositories**, with ETCH-3DS carrying a checked copy of the core.
   * Recommendation: keep it. When the core next changes in ETCH-DS, sync it here and let the checksums prove it.
   * Consider a submodule only if both repositories start changing the core often.
2. **Save format v3: extend the slot file, or a sidecar?**
   * **Decided: the sidecar** `/etch_ds/etch_3ds.sav`, one slot per DS slot, with ETCH's checksum-then-header-last scheme.
   * Recommendation: write it from milestone 2, when OXIDE settings first need saving.
   * A DS save never sees 3DS data, so ETCH DS needs no change.
3. **OXIDE on ETCH's demo songs: on or off by default?**
   * Recommendation: **off by default.**
   * ETCH's seed codes have no OXIDE suffix, so "harmony only when the suffix is present" already means off. A demo of OXIDE can be a new 3DS demo song with a suffixed code.
4. **PROGRESSION mode: may chords transpose the bass?**
   * Recommendation: **yes, opt-in per project and always shown on the top screen.**
   * Implement it as a per-bar transpose applied to sequencer events (render time), never by rewriting pattern memory. Then turning it off gives the exact bass back, and seed-code parity with the GB/GBA/DS holds whenever it's off.
5. **BEAT REPEAT target: drums only by default?**
   * Recommendation: **yes.** The retro buffer already keeps the drum bus on its own, so it costs nothing extra. The 303 keeps flowing (pillar 2), and L for the whole mix is a natural modifier.
6. **May CORROSION add punches when evolving (an opt-in "performer" layer)?**
   * Recommendation: **yes, but not in CORROSION.** Make it a separate generator with its own PRNG and salt, like OXIDE, reading CORROSION's output, so seed codes stay identical across machines. Off by default.
7. **Should the Old 3DS get everything except full VAPOUR polyphony?**
   * Recommendation: **yes.** §2 shows OXIDE, paraphonic VAPOUR and the reverb fit with about half of core 0 still free at the pessimistic figure.
   * Re-check on hardware before RUST's paraphonic mode lands.
8. **Distribution: `.3dsx` only, or `.cia` too?**
   * Recommendation: **`.3dsx` first.** It runs from the Homebrew Launcher on every setup and needs no installation.
   * Add a `.cia` (makerom + bannertool) at a release milestone if people ask for a HOME Menu icon.
9. **Name: ETCH 3DS or ETCH III?**
   * Recommendation: **ETCH 3DS.** It matches ETCH DS, says which machine it's for, and the series already numbers by machine more than by sequel (ETCH II was the GBA).
10. **Should a reduced PUNCH (Y/X + D-pad) be backported to ETCH DS?**
    * Recommendation: **yes, the filter only**, as a later ETCH DS release. The PUNCH filter is fixed point and division-free, so it would run on the ARM9 as is.
    * Do it in ETCH-DS's core first, so it comes back here through the sync and parity stays provable.
