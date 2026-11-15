# ETCH 3DS — ideas parked for later milestones

Ideas that came up while building milestone 1. They're out of scope for now (pillar: don't build beyond the milestone you're on). The full design is in ETCH-DS's `docs/ETCH_3DS_CONCEPT.md`.

## PUNCH

* **Latch.** ZR (R on Old 3DS) held while releasing should latch a punch. R is the latched fill today, so on Old 3DS that needs a new home, for example L + R, or Y as the PUNCH shift.
* **The rest of the pad:**
  * up: ECHO THROW, which freezes into a looping, corroding echo at full push
  * down: CRUSH
  * flick one-shots: TAPE STOP, VINYL BRAKE, REVERSE, GATE

  Each would be one more stage in `__wrap_fx_limit` (the bus before the limiter), with the same idle-is-exact rule.
* **BEAT REPEAT** plays from the retro buffer, which already holds the drum bus on its own, so "drums only by default" costs nothing. A punch can snap back to "where the song would be" by reading the live position from `retro_frames`.
* **PUNCH lanes.** Record the punch state at each 16th into a 16-step lane per pattern (save v3).
* **The retro buffer could grow.** 4 s is one bar at 60 BPM. Two bars would allow two-bar repeats, at 1 MB more.

## Audio

* **The audio thread and the UI must share a core.** The shared core's main-loop code relies on renders being atomic relative to the main thread. That's how `foundry_rebuild` swaps buffers without a lock. If VAPOUR polyphony moves to New 3DS cores 2–3, put only VAPOUR there, fed by its own SPSC ring. Moving the core render off core 0 would need a lock in `foundry_rebuild`, which is an ETCH-DS change.
* **Lead.** The renderer stays 3 blocks ahead of the DSP's play position (8.8 ms average), with no dropouts in the emulator. Real hardware may allow 2 blocks. Make it a setting and measure on hardware.
* **A deterministic `hw_noise` test build** would let the emulator comparison run past the first evolution.
* **VFP for the 3DS-only engines.** The PUNCH filter turned out cheap enough in fixed point. VAPOUR and RUST may use floats, as planned. `check_audio_path.py` would then need a per-root allow-list: floats permitted under the VAPOUR/RUST roots, still forbidden in the shared core.
* **A selectable ACID mode** with 4× oversampling or a zero-delay-feedback ladder, as a new mode and never a silent change. CPU headroom allows it (see the milestone report).

## Screens

* **The 3D landscape** on the top screen, with citro3d: generations in depth, evolved steps rising forward, the 3D slider for depth and its macro. The top screen is a RAM bitmap today, so it could become a texture under a citro3d scene.
* **A HARMONY tab** (milestone 2, OXIDE) fits the 320-px tab bar at 46 px per tab, or as a page of GEN.
* **Show PUNCH on the bottom screen too**, for example as a strip on LIVE showing what the Circle Pad is doing. Today only the top screen shows it.

## Tooling

* **The emulator harness** could record the per-block render timestamps (ticks) to measure scheduling jitter as well as the averages.
* **Fold `tools/arm_parity.sh` into `make test`** when Docker is available, so the console-compiler parity runs on every test.
