#!/usr/bin/env python3
"""
ETCH 3DS -- by Synth Centric
Runs etch_3ds.3dsx in the headless Azahar harness (tools/azahar_harness) and
reports what happened, as ETCH DS's run_melonds.py does for melonDS.

    tools/run_azahar.py [--core PATH] [--model old3ds|new3ds] [--seconds N]
                        [--stress] [--punch] [--tour] [--saves] [--out DIR]

Every run boots to the title and presses START at frame 120 (pillar 1: no
touch needed for sound), then:
  default   plays; the stats mailbox is read once a second
  --punch   holds the Circle Pad left (low-pass), then right (high-pass)
  --stress  PANEL -> CPU TEST on (every step fires ACID + 6 drum lanes, every
            FX stage busy) with the PUNCH filter held: the worst case
  --tour    every tab with SELECT + RIGHT, a drag on the XY pad, screenshots
  --saves   puts an ETCH DS etch_ds.sav (a v2 and a v1 slot) and an ETCH Game
            Boy etch_gb.sav on the SD card, loads each from FILE with the
            buttons and checks the tempo the app reports
It writes OUT/audio.wav and screenshots, reads the per-stage CPU meter,
latency, dropouts and sequencer counters from emulated RAM (the stats
mailbox, 3ds/source/stats_mailbox.h), and compares the recorded audio with a
host render of the same song from the same seed (build/host/render_ref): the
emulator must play exactly the samples the shared core renders.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import wave

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FS = 16756991 / 512
AUDIO_BLOCK = 128


def mailbox_indices():
    src = open(os.path.join(ROOT, "3ds/source/stats_mailbox.h")).read()
    body = src[src.index("enum {") + 6: src.index("};")]
    body = re.sub(r"/\*.*?\*/", "", body, flags=re.S)
    idx, n = {}, 0
    for tok in body.replace("\n", " ").split(","):
        tok = tok.strip()
        if not tok:
            continue
        if "=" in tok:
            name, expr = [t.strip() for t in tok.split("=")]
            n = eval(expr, {}, idx)
        else:
            name = tok
        idx[name] = n
        n += 1
    return idx


def stats_addr(elf):
    nm = None
    for d in [os.environ.get("DEVKITARM", ""), "/opt/devkitpro/devkitARM"]:
        if d and os.path.exists(os.path.join(d, "bin/arm-none-eabi-nm")):
            nm = [os.path.join(d, "bin/arm-none-eabi-nm"), elf]
    if not nm:
        nm = ["docker", "run", "--rm", "-v", ROOT + ":/work", "-w", "/work", "devkitpro/devkitarm",
              "/opt/devkitpro/devkitARM/bin/arm-none-eabi-nm", os.path.relpath(elf, ROOT)]
    for line in subprocess.check_output(nm, text=True).splitlines():
        p = line.split()
        if len(p) == 3 and p[2] == "etch_stats":
            return int(p[0], 16)
    sys.exit("etch_stats not found in " + elf)


class Script:
    def __init__(self):
        self.lines = []
        self.t = 0

    def at(self, f, act, *args):
        self.lines.append((f, act, " ".join(str(a) for a in args)))

    def press(self, f, *keys, hold=3):
        self.at(f, "press", *keys)
        self.at(f + hold, "release", *keys)

    def text(self):
        return "\n".join("%d %s %s" % l for l in sorted(self.lines, key=lambda l: l[0])) + "\n"


def tabs_right(s, f, n):
    """SELECT + RIGHT n times from frame f; returns the frame after"""
    for i in range(n):
        s.at(f, "press", "SELECT")
        s.press(f + 2, "RIGHT")
        s.at(f + 6, "release", "SELECT")
        f += 12
    return f


def build_script(a):
    s = Script()
    start = 120
    s.at(30, "shot", "title")
    s.press(start, "START")
    end = start + int(a.seconds * 60)
    for f in range(start + 60, end + 1, 60):
        s.at(f, "stats")
    s.at(start + 300, "shot", "live")
    if a.punch:
        s.at(start + 600, "lstick", -32000, 0)
        s.at(start + 660, "shot", "punch_lp")
        s.at(start + 900, "lstick", 0, 0)
        s.at(start + 1000, "lstick", 32000, 0)
        s.at(start + 1060, "shot", "punch_hp")
        s.at(start + 1300, "lstick", 0, 0)
    if a.stress:
        f = tabs_right(s, start + 400, 2)             # PANEL
        for k in ["DOWN"] * 3 + ["RIGHT"] * 3:          # CPU TEST knob
            s.press(f, k)
            f += 8
        s.at(f, "press", "A")
        s.press(f + 3, "RIGHT")
        s.at(f + 8, "release", "A")
        s.at(f + 60, "lstick", -26000, 0)              # and the PUNCH filter on top
        s.at(f + 120, "shot", "stress")
    if a.tour:
        f = start + 400
        names = ["xy", "panel", "drum", "keys", "gen", "song", "file"]
        for i, n in enumerate(names):
            f = tabs_right(s, f, 1)
            if n == "xy":
                for k in range(20):
                    s.at(f + k * 2, "touch", 40 + k * 12, 190 - k * 7)
                s.at(f + 42, "untouch")
            if n == "keys":
                s.at(f + 10, "touch", 150, 120)
                s.at(f + 40, "untouch")
            s.at(f + 30, "shot", n)
            f += 60
    if a.saves:
        f = start + 400
        bpm_checks = []

        def to_file(f):
            # opening FILE reads all 16 slots from the SD card, which takes
            # the main loop about a second in the emulator: wait it out
            return tabs_right(s, f, 7) + 90

        f = to_file(f)                                  # slot 01 (v2): A -> LOAD, A arm, A load
        for _ in range(3):
            s.press(f, "A", hold=6)
            f += 40
        s.at(f + 30, "shot", "loaded_v2")
        s.at(f + 60, "stats"); bpm_checks.append((f + 60, 133, "ETCH DS save v2 (slot 01)"))
        f = to_file(f + 90)                             # slot 02 (v1)
        # FILE remembers its cursor: still on the button row after the load
        for k in ["UP", "DOWN", "A", "A", "A"]:          # slots, slot 02, LOAD, arm, load
            s.press(f, k, hold=6)
            f += 40
        s.at(f + 60, "stats"); bpm_checks.append((f + 60, 97, "ETCH DS save v1 (slot 02)"))
        f = to_file(f + 90)                             # GB: slot -> buttons, to GB, list
        for k in ["RIGHT", "RIGHT", "A"]:              # LOAD -> GB -> the Game Boy list
            s.press(f, k, hold=6); f += 40
        f += 60                                         # the GB list is read from the card
        s.at(f, "shot", "gb_list")
        for k in ["LEFT", "LEFT", "LEFT", "A", "A"]:    # GB -> IMPORT (slot 1), arm, import
            s.press(f, k, hold=6); f += 40
        s.at(f + 60, "stats"); bpm_checks.append((f + 60, 111, "ETCH Game Boy import (slot 1)"))
        s.at(f + 60, "shot", "imported_gb")
        a.bpm_checks = bpm_checks
        end = max(end, f + 120)
    s.at(end, "stats")
    s.at(end, "shot", "end")
    s.at(end + 1, "end")
    return s


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--core", default="/tmp/claude-0/azahar/build/bin/Release/azahar_libretro.so")
    ap.add_argument("--harness", default=os.path.join(ROOT, "build/azahar/harness"))
    ap.add_argument("--app", default=os.path.join(ROOT, "etch_3ds.3dsx"))
    ap.add_argument("--elf", default=os.path.join(ROOT, "etch_3ds.elf"))
    ap.add_argument("--model", default="old3ds", choices=["old3ds", "new3ds"])
    ap.add_argument("--seconds", type=float, default=30)
    ap.add_argument("--stress", action="store_true")
    ap.add_argument("--punch", action="store_true")
    ap.add_argument("--tour", action="store_true")
    ap.add_argument("--saves", action="store_true")
    ap.add_argument("--out", default=None)
    a = ap.parse_args()
    a.bpm_checks = []
    tag = a.model + ("_stress" if a.stress else "") + ("_punch" if a.punch else "") + \
        ("_tour" if a.tour else "") + ("_saves" if a.saves else "")
    out = a.out or os.path.join(ROOT, "build/azahar", tag)
    save = os.path.join(out, "save")
    shutil.rmtree(out, ignore_errors=True)
    sd = os.path.join(save, "Azahar", "sdmc")
    os.makedirs(os.path.join(sd, "3ds"))
    os.makedirs(os.path.join(sd, "etch_ds"))
    # ndspInit loads the DSP component from here; Azahar's HLE DSP never runs it
    with open(os.path.join(sd, "3ds", "dspfirm.cdc"), "wb") as f:
        f.write(b"\0" * 0x1000)
    if a.saves:
        subprocess.check_call([os.path.join(ROOT, "build/host/mksaves"), os.path.join(sd, "etch_ds")])
    script = build_script(a)
    with open(os.path.join(out, "script.txt"), "w") as f:
        f.write(script.text())
    addr = stats_addr(a.elf)
    cmd = [a.harness, a.core, a.app, save, out, hex(addr), os.path.join(out, "script.txt"), a.model]
    print("run:", " ".join(cmd))
    subprocess.check_call(cmd)

    # ---- stats -------------------------------------------------------------------
    ix = mailbox_indices()
    rows = []
    for line in open(os.path.join(out, "stats.txt")):
        p = line.split()
        if len(p) > 1 and p[1] != "UNMAPPED":
            rows.append([int(x) for x in p])
    if not rows:
        sys.exit("FAIL: the stats mailbox was not readable")
    last = rows[-1][1:]
    m = lambda k: last[ix[k]]
    ok = True
    print("\n== %s, %.0f s ==" % (a.model, a.seconds))
    print("mailbox magic %08x, %d frames, %d blocks rendered, playing %d" %
          (m("SM_MAGIC"), m("SM_FRAME"), m("SM_BLOCKS"), m("SM_PLAYING")))
    st = ["EVENTS", "ACID", "DRUMS", "DRIVE", "CRUSH", "DELAY", "COMB", "LIMIT", "OUT"]
    # per-stage CPU: the median over the run's 1 s windows (the last one shown)
    print("CPU per stage (%% of one 3.91 ms block, last window; worst block %.1f%%):" % (m("SM_MAX_TOTAL") / 100))
    for i, n in enumerate(st):
        print("  %-8s %5.2f%%" % (n, last[ix["SM_ST0"] + i] / 100))
    for n, k in [("SEQ", "SM_SEQ"), ("PUNCH", "SM_PUNCH"), ("TAP", "SM_TAP"), ("RETRO+OUT", "SM_OUT")]:
        print("  %-8s %5.2f%%" % (n, m(k) / 100))
    total = m("SM_TOTAL") + m("SM_SEQ") + m("SM_PUNCH") + m("SM_TAP") + m("SM_OUT")
    print("  TOTAL    %5.2f%% of the audio thread's real-time budget" % (total / 100))
    ui_ms = m("SM_UI_AVG") / 33513.982
    print("UI frame: %.2f ms avg, %.2f ms worst (of 16.7 ms)" % (ui_ms, m("SM_UI_MAX") / 33513.982))
    if m("SM_LAT_N"):
        print("latency, sequencer to DSP: %.2f ms avg (%.2f .. %.2f ms); fewest blocks queued %d" %
              (m("SM_LAT_SUM") / m("SM_LAT_N") / FS * 1000, m("SM_LAT_MIN") / FS * 1000,
               m("SM_LAT_MAX") / FS * 1000, m("SM_QUEUED_MIN")))
    print("dropouts %d, late gates %d of %d, steps %d, evolutions %d, DSP wakes %d, no_dsp %d" %
          (m("SM_DROPOUTS"), m("SM_GATES_LATE"), m("SM_GATES_TOTAL"), m("SM_STEPS_TOTAL"),
           m("SM_COMMITS"), m("SM_WAKES"), m("SM_NO_DSP")))
    if m("SM_NO_DSP"):
        print("FAIL: ndspInit failed"); ok = False
    if not m("SM_PLAYING") and not a.saves:
        print("FAIL: not playing"); ok = False
    if m("SM_DROPOUTS"):
        print("WARN: %d dropouts" % m("SM_DROPOUTS"))
    if a.punch:
        print("punches started: %d" % m("SM_PUNCH_COUNT"))
        if m("SM_PUNCH_COUNT") < 2:
            print("FAIL: the Circle Pad did not punch in"); ok = False
    totals = [r[1 + ix["SM_TOTAL"]] + r[1 + ix["SM_SEQ"]] + r[1 + ix["SM_PUNCH"]] + r[1 + ix["SM_TAP"]] +
              r[1 + ix["SM_OUT"]] for r in rows if r[1 + ix["SM_WINDOWS"]] > 1]
    if totals:
        print("audio thread over the run: median %.2f%%, max window %.2f%%" %
              (np.median(totals) / 100, max(totals) / 100))
    for fr, bpm, what in a.bpm_checks:
        row = [r for r in rows if r[0] >= fr]
        got = row[0][1 + ix["SM_BPM"]] if row else None
        print("%s: tempo %s (want %d) %s" % (what, got, bpm, "OK" if got == bpm else "FAIL"))
        ok = ok and got == bpm

    # ---- audio ----------------------------------------------------------------------
    w = wave.open(os.path.join(out, "audio.wav"))
    rate = w.getframerate()
    x = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).reshape(-1, 2)
    print("audio: %d frames at %d Hz (%.2f s), peak %d, rms %.1f dBFS" %
          (len(x), rate, len(x) / rate, np.abs(x).max(),
           10 * np.log10(np.mean(x.astype(float) ** 2) / 32768 ** 2 + 1e-12)))
    nz = np.nonzero(np.any(x != 0, axis=1))[0]
    if not len(nz):
        print("FAIL: silence"); sys.exit(1)
    first = nz[0]
    # gaps: runs of exact zeros after the music started (a dropout is a gap)
    z = np.all(x[first:] == 0, axis=1).astype(int)
    runs, cur = [], 0
    for v in z:
        if v:
            cur += 1
        elif cur:
            runs.append(cur); cur = 0
    long_gaps = [r for r in runs if r >= 64]
    print("silent gaps of 64+ samples after the start: %d" % len(long_gaps))
    if not (a.stress or a.punch or a.tour or a.saves):
        # The host renders the same song from the same seed (after a pre-roll
        # of silence, as the console renders silence under the title).  Up to
        # the first evolution it must be identical, sample for sample.  At each
        # mutation CORROSION stirs the moment's entropy (a free-running timer,
        # ETCH's ui_entropy ^ hw_noise) into its choices, on every ETCH, so
        # from there each listen evolves differently: by design.
        seed = m("SM_SEED")
        ref_path = os.path.join(out, "ref.raw")
        secs = min(a.seconds - 2, (len(x) - first) / FS - 0.5)
        subprocess.check_call([os.path.join(ROOT, "build/host/render_ref"), str(seed), "%.2f" % secs, ref_path, "500"])
        ref = np.fromfile(ref_path, dtype=np.int16).reshape(-1, 2)
        rnz = np.nonzero(np.any(ref != 0, axis=1))[0][0]
        n = min(len(ref) - rnz, len(x) - first)
        bars4 = int((4 * 4 - 0.25) * 60.0 / 124 * FS)   # the first commit: step 16's gate point, before bar 5
        diff = np.nonzero(np.any(ref[rnz:rnz + n] != x[first:first + n], axis=1))[0]
        first_diff = diff[0] if len(diff) else n
        same = first_diff >= min(bars4, n)
        print("emulator vs host render of seed %04x: identical for the first %d frames (%.2f s)%s" %
              (seed, first_diff, first_diff / FS,
               "; the first evolution lands at %.2f s (end of bar 4), where timing entropy enters by design" % (bars4 / FS)
               if same else " -- BEFORE the first evolution: FAIL"))
        if not same:
            ok = False
    print("PASS" if ok else "FAIL")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
