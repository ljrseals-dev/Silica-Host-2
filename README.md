# SilicaHost 2

A browser host for emulator cores, shipping one real core: **Silica, a 64-bit Arm (AArch64) interpreter compiled to a
17 KB WebAssembly binary**. Load a bare-metal ELF built by clang, run it in a tab, watch registers/memory, set
breakpoints, run several isolated instances side by side. `dist/index.html` is a single file that works from `file://`.

```
npm ci
npm run build        # wasm core + sample guests + dist/index.html   (needs clang, lld; see below)
npm run serve        # optional; dist/index.html also opens straight from disk
npm test             # everything: native+sanitizer core tests, TS tests, browser e2e
```

Toolchain: `clang` ≥ 18 with the `wasm32` and `aarch64` targets, `ld.lld`, `g++` (sanitizer builds), Node ≥ 22.
No Emscripten.

## What is real here
| Piece | What it is |
|---|---|
| `core/` | The Silica C++20 core, **built to wasm32 with clang + wasm-ld** (`core/wasm/build-wasm.sh`). Freestanding, zero imports, 17 KB. One wasm instance per guest = private linear memory. |
| `src/cores/silica-wasm.ts` | Plugin that drives the wasm via a small C ABI (`core/wasm/silica_abi.h`, mirrored and drift-checked in `src/abi.ts`). |
| `src/elf.ts` | Defensive ELF64/AArch64 loader (rejects wrong machine/class/type, PT_INTERP, overlaps, segments outside RAM, bad entry…). Raw images load at `0x1000`. |
| `src/host.ts` | Plugin registry + instances with a time-sliced run loop (8 ms slices, self-calibrating, injectable scheduler). The UI never freezes on an infinite loop. |
| `samples/` | Guests compiled by real clang: hello, fib, sieve, crc32, a 100 KB console-flood test, and a **conformance program** (below). |

Throughput: ~85 MIPS on a tight ALU/branch loop (Node 22, one core). Treat as a ballpark, not a benchmark.

## How it was verified
* **Original core suite (164 checks) unchanged and passing**, plus **77 new known-answer checks** for added instructions
  (encodings from `llvm-mc`, results from the Arm ARM), native and under ASan/UBSan.
* **Differential conformance**: one C program, 64 checks, compiled for x86-64 *and* AArch64 at `-O0/-O1/-O2/-Os`.
  The emulated output must equal the native output byte for byte — all four pass. The harness was mutation-tested
  (a deliberately broken `umulh` is caught).
* 45 TypeScript tests (ABI drift, ELF validation, core API, breakpoints, console back-pressure, host scheduling, isolation).
* Browser e2e from `file://`: boots, runs ELFs, breakpoints, memory view, multi-instance, bad-file handling; fails on any
  console error. The conformance sample also self-checks against its oracle *inside the page*.

## Limits — read before relying on it
* **Functional interpreter, not a cycle-accurate or full-system model.** No MMU, interrupts, timers, exceptions/`SVC`,
  FP/SIMD *instructions*, atomics/exclusives, or AArch32. Unimplemented instructions halt the core with a clear reason
  (`docs/CORE_CHANGES.md` lists the gaps). This runs freestanding bare-metal code; it does **not** run Linux binaries.
* Guest I/O is a tiny hostcall ABI (`BRK #0x5110`: exit, putc, write — `docs/GUEST_ABI.md`); there is no keyboard input yet.
* No disassembler in the UI (the memory view shows raw bytes with PC highlighted).
* 64 breakpoints per instance; RAM 64 KiB – 256 MiB; each instance holds its RAM in wasm memory.
* **No ESP adapter is shipped yet.** Espressif's `esp-emulator` does exist (Beta; ESP32-C3/C6/H2/P4/S3, WASM release asset
  `esp-emu-<version>-wasm.tar.gz`, JS API documented in its `docs/guides/BROWSER.md`), but its source is not open and I have
  not been able to read that API doc or fetch the binary, so any adapter written now would be guessing. The adapter that was
  supplied targets an assumed API and the pre-1.0 plugin contract; see `docs/PLUGINS.md` for what is needed to finish it.

## Layout
```
core/        C++ core + wasm entry + native tests (make -C core test san)
samples/     guest sources, linker script, build.sh, expected oracle output
src/         abi.ts plugin.ts elf.ts host.ts cores/silica-wasm.ts app/ (UI)
tests/       node:test suites + e2e/ (Playwright)
scripts/     build.mjs (esbuild bundle), test.mjs, serve.mjs
docs/        GUEST_ABI.md CORE_CHANGES.md PLUGINS.md
```
Licence: MIT (`LICENSE`); core derived from MIT-licensed silica-allah-sim (`NOTICE`, `core/LICENSE`).
