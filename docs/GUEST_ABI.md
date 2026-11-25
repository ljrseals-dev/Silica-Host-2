# Guest ABI

SilicaHost runs **bare-metal AArch64 (EL0 semantics, little-endian) programs**. There is no OS, libc, MMU or
interrupt controller. Programs are static `ET_EXEC` ELF files (or raw images loaded at `0x1000`).

## Memory and start state
* Flat RAM at address `0`, size chosen when the instance is created (default 4 MiB, 64 KiB – 256 MiB).
* PC = ELF entry (raw: load base). SP = top of RAM − 16 unless overridden. All other registers 0.
* Accesses outside RAM, and unaligned PC, halt the core with a *memory fault*.

## Hostcalls (`BRK #0x5110`)
A SilicaHost convention (not Arm semihosting). `x0` = operation, arguments in `x1`, `x2`; result in `x0`.

| op | name | args | result |
|----|------|------|--------|
| 0 | exit | `x1` = exit code | stops the core (`exited`) |
| 1 | putc | `x1` = byte | `1`, or `0` if the console buffer is full |
| 2 | write | `x1` = guest pointer, `x2` = length | bytes accepted (may be < length — loop on it); `-1` on a bad pointer |
| other | | | `-1` |

Any other `BRK` immediate halts the core (useful as a breakpoint instruction).
The console buffer is 4 KiB; the host drains it between slices, so `write` loops never lose data
(`samples/flood.c` pushes 100 KB through it and the test asserts every byte arrives).

## Building a guest
`samples/silica.h` has the inline-asm wrappers; `samples/build.sh` shows the exact flags:

```
clang --target=aarch64-none-elf -O2 -ffreestanding -fno-pic -fno-stack-protector -mgeneral-regs-only \
      -fno-builtin -nostdlib -c prog.c -o prog.o
ld.lld -T samples/link.ld -static -o prog.elf prog.o
```
`-mgeneral-regs-only` matters: the core has no FP/SIMD *instruction* support (registers exist, instructions don't).
