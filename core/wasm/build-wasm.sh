#!/usr/bin/env bash
# Build the Silica core to a freestanding wasm32 module (no Emscripten needed: clang + wasm-ld).
set -euo pipefail
HERE="$(cd "$(dirname "$0")/.." && pwd)"
# Output path: absolute, or relative to the caller's working directory (default: <project>/build/silica-core.wasm).
OUT="${1:-$HERE/../build/silica-core.wasm}"
case "$OUT" in /*) ;; *) OUT="$PWD/$OUT" ;; esac
cd "$HERE"
mkdir -p "$(dirname "$OUT")"
CXX="${CXX:-clang++}"
"$CXX" --target=wasm32 -std=c++20 -O3 -DNDEBUG -flto \
  -ffreestanding -fno-exceptions -fno-rtti -fno-threadsafe-statics \
  -mbulk-memory -mnontrapping-fptoint -msign-ext -mmutable-globals \
  -Wall -Wextra -Wpedantic -Werror \
  -nostdlib -isystem wasm/shim -I include -I wasm \
  -Wl,--no-entry -Wl,--lto-O3 -Wl,--gc-sections -Wl,--strip-all \
  -Wl,-z,stack-size=65536 -Wl,--initial-memory=1048576 -Wl,--max-memory=1342177280 \
  wasm/silica_wasm.cpp -o "$OUT"
echo "built $OUT ($(wc -c < "$OUT") bytes)"
