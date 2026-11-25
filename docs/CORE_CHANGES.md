# Changes to the Silica core (vs. silica-allah-sim C++ 1.x)

Why: real compiler output (clang -O1…-O2) immediately hit instructions the original core reports as invalid
(`lsr`/`asr` immediate, `sbfx`/`ubfx`, `umulh`, register-offset `ldr/str`, …). All original behaviour and the
original 164 checks are unchanged and still pass.

## Added instructions
| Group | Instructions |
|---|---|
| Bitfield | `SBFM`, `BFM`, `UBFM` — and therefore `LSL/LSR/ASR #imm`, `UBFX/SBFX/UBFIZ/SBFIZ`, `BFI/BFXIL/BFC`, `SXTB/SXTH/SXTW`, `UXTB/UXTH` |
| Extract | `EXTR` (and `ROR #imm`) |
| Add/sub | extended-register forms (`UXTB…SXTX`, `LSL #0-4`), incl. `SP` operands |
| Multiply | `SMADDL/SMSUBL/UMADDL/UMSUBL` (`SMULL/UMULL/SMNEGL/UMNEGL`), `SMULH`, `UMULH` |
| Conditional compare | `CCMP`, `CCMN` (register and imm5) |
| One-source | `CLZ`, `CLS`, `RBIT`, `REV`, `REV16`, `REV32` |
| Loads/stores | register-offset `LDR/STR` (all sizes, sign-extending, `UXTW/LSL/SXTW/SXTX`, scaled or not), `LDPSW`, `LDNP/STNP` (treated as `LDP/STP`) |

## Other edits
* `FlatMemory::data()` accessor so the embedder can address guest RAM directly.
* `core/wasm/`: freestanding wasm32 entry point, std shims (`<span>`, `<vector>`, `<bit>` …), build script.
  `umulh` is implemented without `__int128` because wasm32 has no `__multi3`.

## Still unimplemented (the decoder reports `Invalid` and the core halts)
FP/SIMD instructions, load/store exclusive & acquire/release, atomics (LSE), `SVC`/exceptions/`ERET`,
system-register access beyond the modelled few, MTE/PAC/SVE, `STGP`, 32-bit (AArch32) state.

## How the additions are verified
1. `core/tests/test_ext.cpp` — 77 known-answer checks. Encodings come from `llvm-mc`; expected values come from the Arm ARM
   definitions; reserved encodings must stay `Invalid`. Runs under ASan/UBSan in CI.
2. `samples/conformance.c` — one C program, compiled for x86-64 *and* AArch64 at `-O0/-O1/-O2/-Os`. The x86-64 output is the
   oracle; the emulator must reproduce it byte for byte (64 checks × 4 optimisation levels). The harness was mutation-tested:
   breaking `umulh` makes it fail.
