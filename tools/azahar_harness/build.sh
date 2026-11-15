#!/bin/sh
# ETCH 3DS -- by Synth Centric
# Builds the headless Azahar harness:
#   1. Azahar's libretro core, with azahar.patch (exposes the app's .data/.bss
#      in the core's memory map, so the harness can read RAM by ELF address)
#   2. harness.c, the libretro frontend that drives it
#
#   tools/azahar_harness/build.sh [AZAHAR_SRC]      (default: a fresh clone in build/azahar/src)
# Needs cmake, ninja, a C++20 compiler; the first build takes about an hour on 4 cores.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
SRC=${1:-$ROOT/build/azahar/src}
if [ ! -d "$SRC" ]; then
    git clone --depth 1 https://github.com/azahar-emu/azahar "$SRC"
fi
cd "$SRC"
git apply --check "$HERE/azahar.patch" 2>/dev/null && git apply "$HERE/azahar.patch" || echo "(patch already applied)"
git submodule update --init --depth 1 --jobs 8
mkdir -p build && cd build
cmake .. -G Ninja -DENABLE_LIBRETRO=ON -DCMAKE_BUILD_TYPE=Release -DENABLE_VULKAN=OFF \
    -DCITRA_WARNINGS_AS_ERRORS=OFF -DENABLE_LTO=OFF -DCITRA_USE_PRECOMPILED_HEADERS=OFF -DENABLE_TESTS=OFF
ninja
CORE=$(find . -name "azahar_libretro.so" | head -1)
mkdir -p "$ROOT/build/azahar"
cc -O2 -Wall -I"$SRC/externals/libretro-common/libretro-common/include" -o "$ROOT/build/azahar/harness" \
    "$HERE/harness.c" -ldl
echo "core:    $SRC/build/$CORE"
echo "harness: $ROOT/build/azahar/harness"
