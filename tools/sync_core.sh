#!/bin/sh
# ETCH 3DS -- by Synth Centric
# Copies the shared core from an ETCH-DS checkout into core/, unmodified, and
# records the checksums and the upstream commit.  The core is never edited in
# this repository: a fix goes to ETCH-DS first, then comes here with this
# script.  `make check` refuses to build if core/ no longer matches.
#
#   tools/sync_core.sh /path/to/ETCH-DS
set -e
SRC=${1:?usage: tools/sync_core.sh /path/to/ETCH-DS}
DST=$(cd "$(dirname "$0")/.." && pwd)/core
mkdir -p "$DST/corrosion" "$DST/common" "$DST/engine" "$DST/tests"
cp "$SRC"/corrosion/* "$DST/corrosion/"
cp "$SRC"/common/ipc.h "$SRC"/common/seq_core.c "$SRC"/common/seq_core.h "$DST/common/"
for f in acid.arm.c foundry.arm.c fx.arm.c render.arm.c dsp_tables.c dsp_tables.h audio.h \
         storage.c storage.h demo.c evolve.c edit.c app.h font_ui.c; do
    cp "$SRC/arm9/source/$f" "$DST/engine/"
done
# ETCH DS's host tests, so they keep running against this copy
cp "$SRC"/tests/host/test_generator.c "$SRC"/tests/host/test_seq.c "$SRC"/tests/host/test_dsp.c \
   "$SRC"/tests/host/test_storage.c "$SRC"/tests/host/SHA256SUMS "$DST/tests/"
if [ -d "$SRC"/tests/host/fixtures ]; then           # test data (ETCH DS: a real ETCH II save)
    mkdir -p "$DST/tests/fixtures"
    cp "$SRC"/tests/host/fixtures/* "$DST/tests/fixtures/"
fi
commit=$(git -C "$SRC" rev-parse HEAD)
branch=$(git -C "$SRC" rev-parse --abbrev-ref HEAD)
cat > "$DST/UPSTREAM" <<EOT
repository  swebdan-design/ETCH-DS
branch      $branch
commit      $commit
map         corrosion/*            <- corrosion/*
            common/*               <- common/{ipc.h,seq_core.c,seq_core.h}
            engine/*               <- arm9/source/*
            tests/*                <- tests/host/*
EOT
(cd "$DST" && find corrosion common engine tests -type f | LC_ALL=C sort | xargs sha256sum > SHA256SUMS)
echo "core/ synced from $SRC @ $commit"
