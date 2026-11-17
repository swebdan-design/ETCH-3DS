# ETCH 3DS 0.2 — series update report

ETCH 3DS — by Synth Centric

ETCH 3DS 0.2 brings milestone 1 up to the ETCH series spec (`docs/SERIES_SPEC.md`, identical to the master copy in ETCH DS and checked by `make`). Everything below was built and run in this repository on the branch `claude/etch-3ds-series`. Nothing has run on a real 3DS (§3).

## 1. What changed

**Core**
* `core/` was re-synced from ETCH-DS `claude/etch-ds-series` at `0d88135` (ETCH DS 0.4) with `tools/sync_core.sh`. No core file changed from the earlier series sync; only `core/UPSTREAM` records the new commit.
* That sync brought ETCH II (Game Boy Advance) save import and its host tests, which use a real ETCH II 2.0 save (`core/tests/fixtures/etch_gba.sav`).
* `tools/check_core_upstream.sh` now compares `core/tests/fixtures/` too.

**Look (spec 4)**
* The accent **#F45423** (`C(30,10,4)`) marks what is happening now: the tab underline, the cursor, the playhead wash, lit pads, CORROSION's fresh changes (fading) and the top screen's FILL / HOLD tag.
* No text is ever set in the accent. Text on an accent fill is black.
* `gfx.c` now counts any attempt to set text in the accent, and the host screen test requires the count to be 0 after visiting every screen.

**Words (spec 3)**
* PANEL is now **SYNTH**; its second page is still DRUM.
* The SYNTH filter mode reads **FIXED / KEY**, as on ETCH DS.

**Controls (spec 2)**
* LIVE bass: A + ←/→ is a semitone and A + ↑/↓ an octave. X and Y toggle accent and slide.
* **SELECT + START** latches the fill, and no longer plays or stops.
* **B** tapped alone on LIVE toggles the fill.
* **Hold R** fills while held, and releases only the fill it started.
* **SELECT + ↑/↓** queues the next / previous pattern, and song mode turns off.
* SELECT + START is ignored while L and R are held, so the quit combo (SELECT + START + L + R) never latches a fill on the way out.

**FILE**
* The source button cycles **DS → GB → GBA → DS**. GBA imports ETCH II songs from `/etch_ds/etch_gba.sav`.

**ABOUT**
* The series' shared texts, reading as a standalone release.
* Version **0.2**, on the title screen and on ABOUT page 1.

**Test tooling**
* The stats mailbox now carries the fill, the latched fill, the queued pattern, song mode, and the first bass step's note and flags.
* New `tools/check_series_controls.py` checks the series controls on the `.3dsx` in Azahar.

**Delivery**
* The manual: `docs/ETCH_3DS_Manual.md`, with the PDF rendered by `tools/make_pdf_manual.py`.
* `THIRD_PARTY_NOTICES.md`.
* The release package in `release/`.
* Regenerated screenshots in `docs/images`, both host-drawn and from Azahar.

The console build compiled cleanly: a clean build of all 23 files with devkitARM and libctru gave no errors and no warnings.

## 2. Tests and results

| Test | Result |
|---|---|
| `make check`: series spec, core and CORROSION checksums | pass |
| `make ds-check` against ETCH-DS `0d88135` | core byte-identical (35 files and the fixtures) |
| ETCH's CORROSION tests | 44 / 44 |
| ETCH DS sequencer tests | 231 / 231 |
| ETCH DS sound-engine tests | 81 / 81 |
| ETCH DS save tests (including the real ETCH II save) | 70 / 70 |
| Sound parity, DS path vs 3DS path (64 s script) | identical, 8 388 608 bytes |
| `tools/arm_parity.sh`: host, DS compiler (ARM9 + ARM7), 3DS compiler (ARM11) under qemu-arm | identical |
| PUNCH, retro buffer, command ring (`test_punch`) | 19 / 19 |
| Screens (`test_screens`): every screen, the latched fill, no text in the accent | 9 / 9 |
| `tools/check_audio_path.py etch_3ds.elf` | no division, float, allocation or lock on the audio path |
| `tools/check_series_controls.py`, Old 3DS | **15 / 15** |
| `tools/check_series_controls.py --model new3ds` | **15 / 15** |
| `tools/run_azahar.py` (30 s each): play, `--punch`, `--stress`, `--tour`, `--saves`, on Old and New 3DS | 10 / 10 pass, **0 dropouts**, 0 late gates |

**Series controls, checked on the console build in Azahar:**
* LIVE bass A + → raised the note one semitone (9 → 10).
* X toggled accent and Y toggled slide.
* SELECT + START latched the fill on and off, and playback never stopped.
* B tapped on LIVE turned the fill on and then off.
* Hold R filled only while held.
* SELECT + ↑ queued the next pattern, with song mode off.
* FILE cycled DS → GB → GBA → DS.
* "ETCH2TEST" imported from the real ETCH II 2.0 save, and the app then played at its 118 BPM.
* The accent showed on both screens: 244 px on the bottom, 120 px for the top screen's FILL tag.
* SELECT + START + L + R quit with no fill latched.

**Emulator measurements**
* **Audio thread:** 5.8 % (Old 3DS) / 2.0 % (New 3DS) of a 3.91 ms block on the boot song; 11.6 % / 3.9 % under the CPU test with PUNCH held.
* **UI frame:** 0.8 ms / 0.3 ms average.
* **Latency, sequencer to DSP:** 8.8 ms average.
* **Exact sound:** the emulator's recording equals the host render of the same seed, sample for sample, up to the first evolution (7.7 s). From there CORROSION takes the moment's entropy, by design.
* **Saves runs:** the ETCH DS v2 and v1 saves loaded (133 and 97 BPM) and the Game Boy save imported (111 BPM). Their silent gaps are the intended stops while a project loads.

## 3. What still needs a real 3DS

Azahar is an emulator: it counts one cycle per instruction, its DSP is a high-level emulation, and it doesn't run the 3DS's own sound firmware. On real hardware, check:

* **Sound through ndsp:**
  * The app reads `/3ds/dspfirm.cdc` and plays.
  * The looping-ring streaming holds with no dropouts. The top screen's DSP line shows the latency, and any dropouts.
  * The CPU cost is within the planning figure of about 2× the emulator's (`docs/MILESTONE1_REPORT.md` §2). SYNTH → CPU TEST gives the worst case.
* **The accent on the top screen:**
  * #F45423 reads as orange on both LCDs.
  * The ACC_TINT playhead wash is visible but light.
* **Saves on the SD card:**
  * Save, load and erase in `/etch_ds/etch_ds.sav`, including after power-off.
  * The file opens in ETCH DS on a DS flash cart, and the other way round.
  * The Game Boy and ETCH II imports work from real cartridge dumps.
* **Feel:**
  * the Circle Pad's dead zone and full push for PUNCH
  * the HOME button and sleep with the lid closed (ndsp pauses and resumes)
  * the quit combo back to the Homebrew Launcher
* **Old and New 3DS both:** New 3DS was emulated as a 300 % CPU clock.
