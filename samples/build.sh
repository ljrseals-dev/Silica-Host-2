#!/usr/bin/env bash
# Cross-compile the bare-metal AArch64 samples with clang + lld, and generate the host-side oracle output
# for the conformance program (same C source compiled for x86-64; its output is the expected result).
set -euo pipefail
cd "$(dirname "$0")"
CC="${CC:-clang}"; LD="${LD:-ld.lld}"
mkdir -p bin expected
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
GUEST=(--target=aarch64-none-elf -ffreestanding -fno-pic -fno-stack-protector -mgeneral-regs-only
       -fno-asynchronous-unwind-tables -fno-builtin -nostdlib -w)
build() { # name source opt
  "$CC" "${GUEST[@]}" "-$3" -c "$2" -o "$TMP/$1.o"
  "$LD" -T link.ld -static -o "bin/$1.elf" "$TMP/$1.o"
}
for p in hello fib sieve crc32 flood; do build "$p" "$p.c" O1; done
for O in O0 O1 O2 Os; do
  build "conformance-$O" conformance.c "$O"
  "$CC" "-$O" -w -DHOST conformance.c -o "$TMP/host-$O"
  "$TMP/host-$O" > "expected/conformance-$O.txt"
done
echo "samples: $(ls bin | wc -l) ELFs, $(ls expected | wc -l) oracle files"
