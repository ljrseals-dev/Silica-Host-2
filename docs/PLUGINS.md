# Writing a plugin

Implement `EmulatorPlugin` (`src/plugin.ts`) and register a `PluginFactory` with `SilicaHost.registerPlugin`.
`src/cores/silica-wasm.ts` is the reference implementation (≈250 lines). Rules of the contract:

* `load()` is async and called once; everything else is synchronous.
* `run(n)` / `step(n)` return a `StopReason` (`budget | halted | breakpoint | exited`). Guest faults are *status*
  (`getStatus()`), never exceptions. Exceptions are for host misuse (bad ranges, use after `destroy`).
* A breakpoint fires before the instruction at that address executes. Resuming from the PC a breakpoint last fired at,
  and `step()`, always make progress.
* Memory getters return copies and are range-checked (`RangeError`).
* Emit `console`, `halt`, `breakpoint`, `exit` events; the host never polls for them.
* The host time-slices `run()` (default 8 ms per slice), so a core must honour its instruction budget.

## ESP32 / Espressif `esp-emulator` — status
`github.com/espressif/esp-emulator` exists (Beta; Rust-based; ESP32-C3/C6/H2/P4, S3 per Espressif's announcement, S31 in
bring-up; the classic ESP32 is not supported). Releases include a WASM tarball and `docs/guides/BROWSER.md` describes its
JavaScript API. The repository carries Apache-2.0 but currently holds binaries, docs and tools, not the emulator source.

An earlier version of this file wrongly said the repository could not be found; that was a search miss on my side.

**Not done, and why:** the supplied `src/plugins/esp-adapter.ts` wraps an `EspWasmModule` interface its own comments call
"simplified" (`loadFirmware/start/pause/step?/readMem?…`), not the documented API, and it fakes execution in a "mock" mode
(PC += 4). It also predates contract v1 (`load(LoadOptions)`, async `loadBinary`, `runFor`, `LoadOptions.entryPoint`).
Finishing it for real needs (1) `docs/guides/BROWSER.md` and (2) the wasm release asset, then:
* Map the real API onto `EmulatorPlugin`; declare only capabilities it truly has (the esp-emu browser build is likely
  *free-running*, with no `step`/breakpoints — plugins like that need a `free-running` capability so the host stops
  busy-slicing `run()` and instead calls start/stop; that host change is small but should be designed against the real API).
* Drop the fake "mock" execution; test with the real wasm, as `tests/samples.test.ts` does for Silica.
