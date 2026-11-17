#!/bin/sh
# ETCH 3DS -- by Synth Centric
# Proves core/ is still byte-identical to an ETCH-DS checkout (the files and
# the commit recorded in core/UPSTREAM).  Run after pulling either repository:
#
#   tools/check_core_upstream.sh /path/to/ETCH-DS
#
# Exit 0: identical.  Otherwise it lists what differs; bring the change over
# with tools/sync_core.sh (core/ is never edited in this repository).
set -e
SRC=${1:?usage: tools/check_core_upstream.sh /path/to/ETCH-DS}
HERE=$(cd "$(dirname "$0")/.." && pwd)/core
want=$(sed -n 's/^commit *//p' "$HERE/UPSTREAM")
have=$(git -C "$SRC" rev-parse HEAD 2>/dev/null || echo unknown)
[ "$want" = "$have" ] || echo "note: core/ was copied from ETCH-DS $want; the checkout is at $have"
fail=0
check() {   # $1 = file in core/, $2 = file in ETCH-DS
    if ! cmp -s "$HERE/$1" "$SRC/$2"; then echo "DIFFERS: core/$1 vs ETCH-DS/$2"; fail=1; fi
}
for f in "$HERE"/corrosion/*; do b=$(basename "$f"); check "corrosion/$b" "corrosion/$b"; done
for b in ipc.h seq_core.c seq_core.h; do check "common/$b" "common/$b"; done
for f in "$HERE"/engine/*; do b=$(basename "$f"); check "engine/$b" "arm9/source/$b"; done
for f in "$HERE"/tests/*; do
    b=$(basename "$f")
    if [ -d "$f" ]; then
        diff -rq "$f" "$SRC/tests/host/$b" >/dev/null || { echo "DIFFERS: core/tests/$b/ vs ETCH-DS/tests/host/$b/"; fail=1; }
    else check "tests/$b" "tests/host/$b"; fi
done
if [ $fail = 0 ]; then echo "OK: core/ is byte-identical to ETCH-DS ($(ls "$HERE"/corrosion "$HERE"/common "$HERE"/engine "$HERE"/tests | grep -c .) files)"; fi
exit $fail
