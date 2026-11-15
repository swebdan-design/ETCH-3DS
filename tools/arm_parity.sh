#!/bin/sh
# ETCH 3DS -- by Synth Centric
# Sound parity with the consoles' own compilers.
#
# tests/host/test_parity.c is compiled three ways and run:
#   host  x86-64, the host compiler                     (make test does this too)
#   DS    BlocksDS's arm-none-eabi-gcc with ETCH DS's flags: the sound engine,
#         CORROSION and the boot song for the ARM9 (arm946e-s; .arm.c files in
#         ARM mode with long calls, the rest Thumb), the sequencer for the ARM7
#         (arm7tdmi, Thumb), exactly as Makefile.arm9 / Makefile.arm7 build
#         them.  Run through the DS path of the test (no 3ds/ code).
#   3DS   devkitARM's arm-none-eabi-gcc with Makefile.3ds's flags (ARM11
#         MPCore, VFP hard-float), the block pump and PUNCH taps linked with
#         --wrap as in the .3dsx.  Run through the 3DS path of the test.
# The ARM programs link against devkitARM's newlib for Linux system calls
# (linux.specs) and run under qemu-arm.  All three must write the same bytes.
#
# The DS objects are built without __NDS__ (no ITCM placement, no hardware
# timer reads): those only move code and read the CPU meter's clock; every
# instruction that computes a sample is the DS compiler's.
#
#   tools/arm_parity.sh            (needs docker and qemu-arm)
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=$ROOT/build/arm_parity
mkdir -p "$OUT"
DKA=devkitpro/devkitarm
BDS=skylyrac/blocksds:slim-latest
INC="-Icore/common -Icore/corrosion -Icore/engine"
W="-w"

echo "== DS: BlocksDS gcc, ARM9 + ARM7 flags =="
docker run --rm -v "$ROOT":/work -w /work $BDS sh -c "
set -e
CC=/opt/wonderful/toolchain/gcc-arm-none-eabi/bin/arm-none-eabi-gcc
\$CC --version | head -1
A9='-mthumb -mcpu=arm946e-s+nofp -O2 -ffunction-sections -fdata-sections'
A7='-mthumb -mcpu=arm7tdmi -O2 -ffunction-sections -fdata-sections'
O=build/arm_parity/ds
mkdir -p \$O
for f in acid foundry fx render; do \$CC \$A9 -marm -mlong-calls $INC $W -c core/engine/\$f.arm.c -o \$O/\$f.o; done
for f in dsp_tables demo evolve; do \$CC \$A9 $INC $W -c core/engine/\$f.c -o \$O/\$f.o; done
for f in corrosion pattern names; do \$CC \$A9 $INC $W -c core/corrosion/\$f.c -o \$O/\$f.o; done
\$CC \$A7 $INC $W -c core/common/seq_core.c -o \$O/seq_core.o
"

echo "== 3DS: devkitARM gcc, ARM11 flags; link both =="
docker run --rm -v "$ROOT":/work -w /work $DKA sh -c "
set -e
CC=/opt/devkitpro/devkitARM/bin/arm-none-eabi-gcc
\$CC --version | head -1
A11='-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft -O2 -mword-relocations -ffunction-sections -D__3DS__'
O=build/arm_parity
mkdir -p \$O/3ds
# 3DS configuration: everything with the 3DS flags, the PUNCH taps wrapped in
\$CC \$A11 $INC -I3ds/source $W -DPARITY_3DS -specs=linux.specs -static -o \$O/parity_3ds_arm11 \
    tests/host/test_parity.c core/common/seq_core.c core/engine/acid.arm.c core/engine/foundry.arm.c \
    core/engine/fx.arm.c core/engine/render.arm.c core/engine/dsp_tables.c core/engine/demo.c \
    core/engine/evolve.c core/engine/edit.c core/engine/storage.c core/corrosion/corrosion.c \
    core/corrosion/pattern.c core/corrosion/names.c 3ds/source/audio_pump.c 3ds/source/punch.c tests/host/qemu_shim.c \
    -Wl,--wrap=foundry_render -Wl,--wrap=fx_limit -lm
# DS configuration: the DS objects, with the test driver and the edit glue
D5='-mthumb -mcpu=arm946e-s -mfloat-abi=soft -O2'
\$CC \$D5 $INC $W -c tests/host/test_parity.c -o \$O/ds/test_parity.o
\$CC \$D5 $INC $W -c core/engine/edit.c -o \$O/ds/edit.o
\$CC \$D5 $INC $W -c core/engine/storage.c -o \$O/ds/storage.o
\$CC \$D5 $W -c tests/host/qemu_shim.c -o \$O/ds/qemu_shim.o
\$CC \$D5 -specs=linux.specs -static -o \$O/parity_ds_arm9 \$O/ds/*.o -lm
"

echo "== run =="
make -s -C "$ROOT/tests/host" HOSTCC=${HOSTCC:-cc} ../../build/host/parity_ds >/dev/null
"$ROOT/build/host/parity_ds" "$OUT/host.raw"
qemu-arm "$OUT/parity_ds_arm9" "$OUT/ds_arm9.raw"
qemu-arm "$OUT/parity_3ds_arm11" "$OUT/3ds_arm11.raw"
cmp "$OUT/host.raw" "$OUT/ds_arm9.raw"
cmp "$OUT/host.raw" "$OUT/3ds_arm11.raw"
echo "OK: identical output from the host, the DS compiler (ARM9 + ARM7) and the 3DS compiler (ARM11): $(stat -c %s "$OUT/host.raw") bytes"
