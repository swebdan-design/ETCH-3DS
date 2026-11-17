#!/usr/bin/env python3
"""
ETCH 3DS -- by Synth Centric
The series controls (docs/SERIES_SPEC.md 2) on the console build, in the
Azahar harness, as ETCH DS checks them in melonDS
(ETCH-DS tools/check_series_controls.py).  It drives etch_3ds.3dsx with
buttons only, reads the app's state from emulated RAM (the stats mailbox) and
checks:

  * LIVE bass: A + RIGHT is one semitone up, X toggles accent, Y toggles slide
  * SELECT + START latches the fill on and off, and does not start or stop
    playback
  * B tapped alone on LIVE toggles the fill; hold R fills only while held
  * SELECT + UP queues the next pattern (song mode off)
  * FILE: the source button cycles DS -> GB -> GBA -> DS, and an ETCH II save
    (a real ETCH II 2.0 save, core/tests/fixtures/etch_gba.sav) imports
  * the accent (#F45423 on BGR555: C(30,10,4)) shows on both screens
  * SELECT + START + L + R quits, without leaving a fill latched on the way

    tools/check_series_controls.py [--model old3ds|new3ds] [--out DIR]
"""
import argparse
import os
import shutil
import subprocess
import sys

import numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import run_azahar as R  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--core", default="/tmp/claude-0/azahar/build/bin/Release/azahar_libretro.so")
    ap.add_argument("--harness", default=os.path.join(ROOT, "build/azahar/harness"))
    ap.add_argument("--model", default="old3ds", choices=["old3ds", "new3ds"])
    ap.add_argument("--out", default=None)
    a = ap.parse_args()
    out = a.out or os.path.join(ROOT, "build/azahar", a.model + "_series")
    shutil.rmtree(out, ignore_errors=True)
    save = os.path.join(out, "save")
    sd = os.path.join(save, "Azahar", "sdmc")
    os.makedirs(os.path.join(sd, "3ds"))
    os.makedirs(os.path.join(sd, "etch_ds"))
    with open(os.path.join(sd, "3ds", "dspfirm.cdc"), "wb") as f:
        f.write(b"\0" * 0x1000)
    shutil.copy(os.path.join(ROOT, "core/tests/fixtures/etch_gba.sav"), os.path.join(sd, "etch_ds", "etch_gba.sav"))

    s = R.Script()
    marks = {}

    def mark(f, name):
        s.at(f, "stats")
        marks[name] = f

    s.press(120, "START")
    mark(400, "before")
    s.at(410, "press", "A")                  # A held + RIGHT: a semitone
    s.press(420, "RIGHT")
    s.at(440, "release", "A")
    mark(470, "semitone")
    s.press(490, "X")
    mark(520, "x")
    s.press(540, "Y")
    mark(570, "y")
    s.at(590, "press", "SELECT")             # SELECT + START: latched fill on
    s.press(593, "START")
    s.at(600, "release", "SELECT")
    mark(640, "selstart_on")
    s.at(650, "shot", "fill_on")
    s.at(660, "press", "SELECT")             # ... and off
    s.press(663, "START")
    s.at(670, "release", "SELECT")
    mark(710, "selstart_off")
    s.press(730, "B")                        # B alone on LIVE: fill on
    mark(770, "b_on")
    s.press(790, "B")                        # ... and off
    mark(830, "b_off")
    s.press(850, "R", hold=100)              # hold R: fill while held
    mark(900, "r_held")
    mark(1000, "r_up")
    mark(1010, "before_queue")
    s.at(1020, "press", "SELECT")            # SELECT + UP: queue the next pattern
    s.press(1023, "UP")
    s.at(1030, "release", "SELECT")
    mark(1050, "queued")
    s.at(1060, "shot", "live")

    f = R.tabs_right(s, 1100, 7) + 90        # FILE
    s.at(f, "shot", "file_ds")
    for k in ["A", "RIGHT", "RIGHT", "A"]:   # slot -> buttons -> GB: the Game Boy list
        s.press(f, k, hold=6)
        f += 40
    f += 60
    s.at(f, "shot", "file_gb")
    s.press(f, "A", hold=6)                  # GBA: the ETCH II list
    f += 100
    s.at(f, "shot", "file_gba")
    s.press(f, "A", hold=6)                  # DS again
    f += 100
    s.at(f, "shot", "file_ds2")
    s.press(f, "A", hold=6)                  # GB
    f += 100
    s.press(f, "A", hold=6)                  # GBA
    f += 100
    for k in ["UP", "DOWN", "A", "LEFT", "A", "A"]:   # slot 2, buttons, IMPORT, arm, import
        s.press(f, k, hold=6)
        f += 40
    mark(f + 60, "imported")
    s.at(f + 60, "shot", "imported_gba")

    q = f + 120                              # the quit combo: SELECT, L, R held, then START
    mark(q - 5, "before_quit")
    s.at(q, "press", "SELECT", "L", "R")
    s.at(q + 10, "press", "START")
    mark(q + 60, "after_quit_1")
    mark(q + 180, "after_quit_2")
    s.at(q + 190, "end")

    with open(os.path.join(out, "script.txt"), "w") as fh:
        fh.write(s.text())
    addr = R.stats_addr(os.path.join(ROOT, "etch_3ds.elf"))
    cmd = [a.harness, a.core, os.path.join(ROOT, "etch_3ds.3dsx"), save, out, hex(addr),
           os.path.join(out, "script.txt"), a.model]
    subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=1800)

    ix = R.mailbox_indices()
    rows = {}
    for line in open(os.path.join(out, "stats.txt")):
        p = line.split()
        if len(p) > 1 and p[1] != "UNMAPPED":
            rows[int(p[0])] = [int(x) for x in p[1:]]
    M = lambda name, k: rows[marks[name]][ix[k]]
    ok = []

    def chk(name, c, detail=""):
        ok.append(bool(c))
        print("PASS" if c else "FAIL", name, detail)

    n0, f0 = M("before", "SM_NOTE0"), M("before", "SM_FLAGS0")
    n1, f1 = M("semitone", "SM_NOTE0"), M("semitone", "SM_FLAGS0")
    chk("LIVE bass A + RIGHT: one semitone up", (f0 & 3) == 1 and n1 == n0 + 1,
        "note %d -> %d (flags %02x)" % (n0, n1, f0))
    chk("LIVE bass X: accent toggles", (M("x", "SM_FLAGS0") ^ f1) & 0x04 == 0x04 and (M("x", "SM_FLAGS0") ^ f1) & 0x08 == 0)
    chk("LIVE bass Y: slide toggles", (M("y", "SM_FLAGS0") ^ M("x", "SM_FLAGS0")) & 0x0C == 0x08)
    chk("SELECT + START latches the fill on", M("selstart_on", "SM_FILL_HOLD") == 1)
    chk("SELECT + START lets it go", M("selstart_off", "SM_FILL_HOLD") == 0)
    chk("SELECT + START never stops playback",
        M("selstart_on", "SM_PLAYING") == 1 and M("selstart_off", "SM_PLAYING") == 1)
    chk("B tapped on LIVE: fill on, then off", M("b_on", "SM_FILL_HOLD") == 1 and M("b_off", "SM_FILL_HOLD") == 0)
    chk("hold R: fills while held, stops on release",
        M("r_held", "SM_FILL_HOLD") == 1 and M("r_up", "SM_FILL_HOLD") == 0 and M("r_up", "SM_PLAYING") == 1)
    cur = M("before_queue", "SM_CUR")
    nxt = (cur + 1) % 32                      # queued, or already playing if the bar line came first
    chk("SELECT + UP queues the next pattern",
        (M("queued", "SM_QUEUED") == nxt or M("queued", "SM_CUR") == nxt) and M("queued", "SM_SONG") == 0,
        "was %02d; then playing %02d, queued %d" % (cur, M("queued", "SM_CUR"), M("queued", "SM_QUEUED")))
    chk("ETCH II (GBA) import loads the song (118 BPM, from a real ETCH II 2.0 save)",
        M("imported", "SM_BPM") == 118 and M("imported", "SM_PLAYING") == 1, "tempo %d" % M("imported", "SM_BPM"))
    chk("the quit combo leaves no fill latched", M("before_quit", "SM_FILL_HOLD") == 0)
    chk("SELECT + START + L + R quits (the main loop stops)",
        M("after_quit_1", "SM_FRAME") == M("after_quit_2", "SM_FRAME"),
        "frames %d, %d" % (M("after_quit_1", "SM_FRAME"), M("after_quit_2", "SM_FRAME")))

    # FILE source cycling, read off the bottom screen: the path at the top right
    def text_region(name):
        im = np.asarray(Image.open(os.path.join(out, name + ".ppm")).convert("RGB")).astype(int)
        return im
    # the accent on both screens: #F45423 as the 3DS shows C(30,10,4) (RGB565)
    def accent_px(im, top):
        reg = im[:240] if top else im[240:]
        d = np.abs(reg - np.array([247, 81, 33])).sum(axis=2)
        return int((d < 24).sum())
    live = text_region("live")
    fill = text_region("fill_on")
    chk("the accent shows on the bottom screen (tab underline, cursor, playhead wash)", accent_px(live, False) > 20,
        "%d px" % accent_px(live, False))
    chk("the accent shows on the top screen while the fill is on (the FILL tag)", accent_px(fill, True) > 20,
        "%d px" % accent_px(fill, True))
    # the list header ("SAVE SLOTS ... /ETCH_DS/ETCH_DS.SAV"): bottom screen y 26..34
    sigs = [int(text_region(n)[240 + 26:240 + 34, 40:360].sum()) for n in ["file_ds", "file_gb", "file_gba", "file_ds2"]]
    chk("FILE: the source cycles DS -> GB -> GBA -> DS (the list header changes, and returns)",
        len(set(sigs[:3])) == 3 and sigs[3] == sigs[0])
    print("%d/%d series control checks passed (screenshots in %s)" % (sum(ok), len(ok), out))
    sys.exit(0 if all(ok) else 1)


if __name__ == "__main__":
    main()
