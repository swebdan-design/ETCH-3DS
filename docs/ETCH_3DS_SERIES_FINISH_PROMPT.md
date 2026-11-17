# ETCH 3DS — finish the series update and deliver version 0.2

*Paste this whole document into the ETCH 3DS chat as the next task.*

## Context

The ETCH series (ETCH for Game Boy, ETCH II for GBA, ETCH DS, ETCH 3DS) now follows one shared contract, the **series spec**: `docs/SERIES_SPEC.md` (master copy in ETCH-DS; every repository carries an identical copy, checked by `make`). Read it first. It fixes the controls, the screen names, the look (colour roles, the accent **#F45423**, never used for text), and the seed-code rules.

In another session, all four repositories were updated to the spec, each on its own branch:

| Repository | Branch | State |
|---|---|---|
| swebdan-design/Etch---GB | `claude/etch-3.0` | ETCH 3.0, done: emulator 101/101, release package built |
| swebdan-design/Etch-II-GBA | `claude/etch-ii-2.0` | ETCH II 2.0, done: emulator 77/77, release package built |
| swebdan-design/ETCH-DS | `claude/etch-ds-series` | ETCH DS 0.4, done: host tests pass, melonDS 7/7 control checks, release package built |
| swebdan-design/ETCH-3DS | `claude/etch-3ds-series` | **Your task.** The code is changed and passes every host test, but it has never been built for the console: that session could not install devkitARM / libctru. |

Backups of every repository before this work are on the branches `claude/backup-pre-series-2026-11-16`.

## What is already on `claude/etch-3ds-series`

1. `core/` re-synced from ETCH-DS (`tools/sync_core.sh`), bringing the **ETCH II (Game Boy Advance) save import** (`storage_gba_status` / `storage_gba_load`, `app_import_gba`) and its host tests, which use a real ETCH II 2.0 save (`core/tests/fixtures/etch_gba.sav`). `tools/sync_core.sh` now also copies `tests/host/fixtures`.
2. The 3DS UI (`3ds/source/ui.c`, `gfx.h`, `scr_top.c`, `main.c`) has the same changes ETCH DS got:
   - **Colour:** `COL_ACCENT` and `COL_EVOLVE` are `C(30,10,4)` (#F45423); `COL_PLAY` and `COL_GLOW` are the accent tint `C(31,27,25)`. Text drawn in the accent is now `COL_TEXT`; text on an accent fill is black (lit buttons, the playing pattern and chain cells). FILE messages are black, with a small accent mark for good news.
   - **Tab:** PANEL is renamed **SYNTH** (its second page is still DRUM).
   - **LIVE bass:** A + LEFT/RIGHT is a semitone, A + UP/DOWN an octave (as on every ETCH); **X / Y** toggle accent / slide on the bass step.
   - **Fills:** SELECT + START toggles the latched fill (and no longer also plays/stops: see `main.c`); B tapped alone on LIVE toggles it; hold R fills while held and releases only the fill it started.
   - **SELECT + UP/DOWN** queues the next / previous pattern (song mode turns off).
   - **FILE:** the source button cycles DS → GB → GBA; GBA imports `/etch_ds/etch_gba.sav`.
3. `docs/SERIES_SPEC.md` copied; `make check` verifies it.
4. Host tests: 44 + 231 + 81 + 70 + PUNCH 19 + screens 6, 0 failures.
5. The ABOUT pages read as a standalone release: no "sequel to ETCH DS". Page 1 describes ETCH 3DS only; the generative music page says a seed code always makes the same groove ("share the code to share the song") without naming other consoles; the Synth Centric page says "ETCH 3DS is a Synth Centric release."; the credits keep the licences but don't say "from ETCH 2.5" or "from ETCH DS".

## Your tasks

Work on the branch `claude/etch-3ds-series`. Commit as you go and push to it. Don't merge, and don't open a pull request unless asked.

1. **Check the core.** `core/` is already synced from ETCH-DS `claude/etch-ds-series` (ETCH DS 0.4). If that branch has moved on since, re-run `tools/sync_core.sh` against its HEAD; then `make test`. `tests/host/Makefile` already passes `-DGBA_FIXTURE` for the 3DS layout. Never edit `core/` by hand.
2. **Build for the console** with devkitARM + libctru (`make`, or `docker run --rm -v "$PWD":/work -w /work devkitpro/devkitarm make`). Expect to fix console-only compile issues: the UI changes were only compiled on the host. Keep the 3DS UI's changes equivalent to ETCH DS's (compare with ETCH-DS `arm9/source/ui.c`).
3. **Run every existing check:** `make test`, `tools/arm_parity.sh`, `tools/check_audio_path.py` on `etch_3ds.elf`, and the Azahar harness (`tools/run_azahar.py`, including `--tour`, `--saves`, `--punch`, `--stress`, and `--model new3ds`). Zero dropouts, as in milestone 1.
4. **Check the series controls on the console build** (extend the Azahar runner if needed, as ETCH-DS did in `tools/check_series_controls.py`):
   - LIVE bass A + RIGHT raises the note by one semitone; X toggles accent; Y toggles slide.
   - SELECT + START latches the fill on and off and does **not** start or stop playback.
   - B tapped on LIVE toggles the fill; hold R fills only while held.
   - SELECT + UP queues the next pattern.
   - The quit combo **SELECT + START + L + R** still quits. Make sure it doesn't leave a fill toggled on the way out; if it does, ignore SELECT + START while L and R are held.
   - FILE: GB → GBA → DS cycling, and a GBA import from a real ETCH II save loads the song.
   - The accent shows where the spec says, and no text is drawn in it.
5. **Version:** bump `ETCH_3DS_VERSION` (`3ds/source/platform.h`) from 0.1 to **0.2**. The title and ABOUT show it.
6. **Screenshots:** regenerate `docs/images` (host `test_screens` + `tools/screens_png.py`, and `azahar.png` from the emulator).
7. **Manual:** write `docs/ETCH_3DS_Manual.md`, modelled on ETCH-DS `docs/ETCH_DS_Manual.md`. It must read as a standalone release: never "sequel" or "successor". Other consoles are only mentioned where a feature needs them: the save imports, and the shared save file with ETCH DS. Same sections (plain language, screenshots), plus the 3DS-only parts: PUNCH on the Circle Pad (low-pass left, high-pass right, quantised to the next 16th, snaps back), the larger screens, the quit combo, `dspfirm.cdc` (run DSP1 once if there is no sound), the shared save file with ETCH DS, and the GB and GBA imports. Render it to PDF the way ETCH-DS does (`tools/make_pdf_manual.py`: Markdown → HTML → Chromium PDF).
8. **Release package** in `release/`: `etch_3ds.3dsx`, `ETCH_3DS_Manual.pdf` and `.md`, `ETCH_3DS_source.zip` (`git archive` of the committed tree, without `release/`) and `SHA256SUMS.txt`. Add a "Release (version 0.2)" section to the README listing them, as ETCH-DS does.
9. **Report:** a short `docs/SERIES_UPDATE_REPORT.md`: what changed, every test with its result, and what still needs real hardware (a real 3DS: sound through `ndsp`, the accent on the top screen, saves on the SD card).

## Rules (unchanged from milestone 1)

- CORROSION and `core/` are never edited in this repository. A core fix goes to ETCH-DS first, then comes here with `tools/sync_core.sh`.
- The spec is the contract: the same button never means something different on another ETCH. 3DS-only additions (Circle Pad, 3D slider, ZL/ZR) may add meanings but never change existing ones.
- Text is never set in the accent colour; text on an accent fill is black.
- Seed codes: one format for the whole series (spec section 5). When OXIDE is built, it seeds from the fourth group of ETCH II's 16-digit code. The first 12 digits are always CORROSION's.
- ABOUT pages and manuals read as a standalone release (no "sequel" / "successor").
- Report results honestly: if something couldn't be run, say so.
