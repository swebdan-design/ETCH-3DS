#!/usr/bin/env python3
"""
ETCH 3DS -- by Synth Centric
Proves the audio path is free of division and floating point, and that it
never allocates, frees, locks or sleeps.  Adapted from ETCH DS's checker
(tools/check_audio_path.py there), which walks arm9.elf from the render
interrupt; run that one on the DS build.

Walks the call graph of etch_3ds.elf from the block pump (pump_block: the
command ring, seq_core, the whole shared engine, the PUNCH taps, the retro
buffer) through direct calls, veneers and register-indirect calls
(conservatively: every function address in a reachable function's literal
pool counts as a callee).  Fails if a reachable function is a division or
modulo helper, a soft-float or VFP-using routine, libm, an allocator, or a
lock / sleep / wait, or if a reachable instruction is a hardware divide or a
VFP instruction.

It then reports what the audio THREAD reaches beyond the pump (ndsp and
the kernel), where waiting is expected: the thread sleeps on a LightEvent
between DSP frames, and ndsp's wave-buffer queue takes ndsp's own short lock,
which the UI never touches.  Allocation and division are still forbidden.

    tools/check_audio_path.py [etch_3ds.elf]
"""
import os
import re
import shutil
import subprocess
import sys

ROOT = "pump_block"
THREAD = "audio_thread"
DIV = r"__aeabi_(i|ui|l|ul)div(mod)?|__(u)?(div|mod)[sd]i3|__divdi3|__udivdi3"
FLOAT = r"__aeabi_[fd]\w+|^(sqrt|exp|log|pow|sin|cos|tan|floor|ceil|fmod|tanh)f?$|__(add|sub|mul|div)[sd]f3"
ALLOC = r"^_?(malloc|free|realloc|calloc|memalign|linearAlloc|linearFree)(_r)?$|^_malloc_r$|^_free_r$"
BLOCK = r"LightLock_|RecursiveLock_|LightEvent_Wait|svcSleepThread|svcWaitSynchronization|CondVar_|LightSemaphore_"
BAD_PUMP = re.compile("|".join([DIV, FLOAT, ALLOC, BLOCK]))
BAD_THREAD = re.compile("|".join([DIV, FLOAT, ALLOC]))
VFP = re.compile(r"\t(v[a-z]+(\.f(32|64))?|f[a-z]+[sd])\s")
HWDIV = re.compile(r"\t[su]div\s")


def tool(name):
    for d in [os.environ.get("DEVKITARM", ""), "/opt/devkitpro/devkitARM"]:
        p = os.path.join(d, "bin", "arm-none-eabi-" + name)
        if d and os.path.exists(p):
            return p
    p = shutil.which("arm-none-eabi-" + name)
    if p:
        return p
    sys.exit("arm-none-eabi-%s not found (set DEVKITARM)" % name)


def main():
    elf = sys.argv[1] if len(sys.argv) > 1 else "etch_3ds.elf"
    dis = subprocess.check_output([tool("objdump"), "-d", elf], text=True).splitlines()
    syms = {}
    for line in subprocess.check_output([tool("nm"), elf], text=True).splitlines():
        p = line.split()
        if len(p) == 3 and p[1] in "tTwW":
            syms[int(p[0], 16) & ~1] = p[2]
    funcs, words = {}, {}
    cur = None
    for line in dis:
        m = re.match(r"^([0-9a-f]+) <(.+)>:$", line)
        if m:
            cur = m.group(2)
            funcs[cur] = []
            continue
        m = re.match(r"^\s*([0-9a-f]+):\s+([0-9a-f]{8})\s+\.word\s+0x([0-9a-f]+)", line)
        if m:
            words[int(m.group(1), 16)] = int(m.group(3), 16)
        if cur:
            funcs[cur].append(line)

    def callees(f):
        out = set()
        for line in funcs.get(f, []):
            m = re.search(r"\t(bl|blx|b|b\.w|bx|beq|bne|bgt|blt|bge|ble|bhi|bls|bcc|bcs)\s+[0-9a-f]+ <([^>+]+)", line)
            if m and m.group(2) != f:
                out.add(m.group(2))
            if HWDIV.search(line):
                out.add("<hardware divide>")
            if VFP.search(line) and ".word" not in line:
                out.add("<VFP instruction in %s>" % f)
            m = re.search(r"\.word\s+0x([0-9a-f]+)", line)
            if m and int(m.group(1), 16) & ~1 in syms:
                out.add(syms[int(m.group(1), 16) & ~1])
        return out

    def walk(root, bad, stop=()):
        seen, stack, found = set(), [root], []
        while stack:
            f = stack.pop()
            if f in seen:
                continue
            seen.add(f)
            if bad.search(f) or f.startswith("<"):
                found.append(f)
                continue
            if f in stop:
                continue
            stack.extend(callees(f))
        return seen, found

    pump, bad = walk(ROOT, BAD_PUMP)
    print("block pump (reachable from %s): %d functions" % (ROOT, len(pump)))
    for f in sorted(x for x in pump if not x.startswith("<")):
        print("  " + f)
    if bad:
        print("FAIL: reachable from the block pump:", ", ".join(sorted(bad)))
        sys.exit(1)
    thread, bad = walk(THREAD, BAD_THREAD, stop={ROOT})
    extra = sorted(x for x in thread - pump if not x.startswith("<") and x != ROOT)
    print("audio thread, beyond the pump: %d functions (ndsp, kernel)" % len(extra))
    for f in extra:
        print("  " + f)
    if bad:
        print("FAIL: reachable from the audio thread:", ", ".join(sorted(bad)))
        sys.exit(1)
    print("OK: no division, modulo, floating point, allocation or lock is reachable from the block pump, "
          "and no division, floating point or allocation from the audio thread")


if __name__ == "__main__":
    main()
