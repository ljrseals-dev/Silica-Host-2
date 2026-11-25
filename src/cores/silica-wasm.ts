/**
 * Silica AArch64 core, running as a real WebAssembly binary (built from core/ with clang + wasm-ld).
 * Each plugin instance owns its own WebAssembly.Instance (and therefore its own linear memory),
 * so multiple instances are fully isolated from each other.
 */
import {
  ABI_VERSION, CONSOLE_CAP, HALT_REASONS, MAX_BREAKPOINTS, ST, STOP, type SilicaExports,
} from "../abi";
import { ElfError, isElf, parseElf } from "../elf";
import type {
  Capability, CoreStatus, EmulatorPlugin, InitOptions, LoadOptions, LoadResult, MemoryRegion,
  PluginEventName, PluginEvents, PluginFactory, RegisterFile, StopReason,
} from "../plugin";

// Embedded by the bundler (esbuild --loader:.wasm=binary), so the app also works from file://.
import wasmBytes from "../../build/silica-core.wasm";

export const DEFAULT_RAM = 4 * 1024 * 1024;
export const MIN_RAM = 64 * 1024;
export const MAX_RAM = 256 * 1024 * 1024;
const DEFAULT_BASE = 0x1000;

let compiled: Promise<WebAssembly.Module> | null = null;
function compileOnce(): Promise<WebAssembly.Module> {
  return (compiled ??= WebAssembly.compile(wasmBytes as unknown as BufferSource));
}

const GPR_NAMES = Array.from({ length: 31 }, (_, i) => `X${i}`);

export class SilicaWasmCore implements EmulatorPlugin {
  readonly id = "silica-aarch64-wasm";
  readonly name = "Silica AArch64 (WebAssembly)";
  readonly version = "1.0.0";
  readonly supportedArchs = ["aarch64"] as const;
  readonly capabilities: readonly Capability[] = ["functional-only", "breakpoints", "console", "elf"];

  private x: SilicaExports | null = null;
  private ramBase = 0;
  private ramSize = 0;
  private bps = new Set<number>();
  private decoder = new TextDecoder("utf-8");
  private listeners: Partial<Record<PluginEventName, Set<(...a: never[]) => void>>> = {};
  private destroyed = false;
  private lastStatus: CoreStatus["state"] = "ready";

  async load(options: InitOptions = {}): Promise<void> {
    if (this.x) throw new Error("core already loaded");
    const ram = options.ramBytes ?? DEFAULT_RAM;
    if (!Number.isInteger(ram) || ram < MIN_RAM || ram > MAX_RAM || ram % 16 !== 0) {
      throw new RangeError(`ramBytes must be a multiple of 16 between ${MIN_RAM} and ${MAX_RAM}`);
    }
    const module = await compileOnce();
    const x = new WebAssembly.Instance(module, {}).exports as unknown as SilicaExports;
    if (x.silica_abi_version() !== ABI_VERSION) throw new Error("wasm core ABI version mismatch");
    const rc = x.silica_init(ram);
    if (rc !== 0) throw new Error(`silica_init failed (${rc}): could not allocate ${ram} bytes of guest RAM`);
    this.x = x;
    this.ramBase = x.silica_ram_ptr();
    this.ramSize = x.silica_ram_size();
  }

  private core(): SilicaExports {
    if (this.destroyed) throw new Error("core was destroyed");
    if (!this.x) throw new Error("core not loaded: call load() first");
    return this.x;
  }
  private mem8(): Uint8Array { return new Uint8Array(this.core().memory.buffer); }

  // ── loading ──────────────────────────────────────────────────────────────
  loadBinary(data: ArrayBuffer | Uint8Array, options: LoadOptions = {}): LoadResult {
    const x = this.core();
    const bytes = data instanceof Uint8Array ? data : new Uint8Array(data);
    const mem = this.mem8();
    let result: LoadResult;
    if (isElf(bytes)) {
      const img = parseElf(bytes, this.ramSize);
      x.silica_reset(1);
      for (const s of img.segments) {
        mem.set(bytes.subarray(s.offset, s.offset + s.filesz), this.ramBase + s.vaddr);
      }
      result = {
        format: "elf", entry: options.entry ?? img.entry, bytes: bytes.length,
        segments: img.segments.map((s) => ({ addr: s.vaddr, size: s.memsz })),
      };
    } else {
      const base = options.base ?? DEFAULT_BASE;
      if (!Number.isInteger(base) || base < 0 || base % 4 !== 0) throw new RangeError("base must be a non-negative multiple of 4");
      if (bytes.length === 0) throw new ElfError("empty binary");
      if (base + bytes.length > this.ramSize) {
        throw new RangeError(`binary of ${bytes.length} bytes at 0x${base.toString(16)} does not fit in ${this.ramSize} bytes of RAM`);
      }
      x.silica_reset(1);
      mem.set(bytes, this.ramBase + base);
      result = { format: "raw", entry: options.entry ?? base, bytes: bytes.length, segments: [{ addr: base, size: bytes.length }] };
    }
    if (result.entry % 4 !== 0 || result.entry >= this.ramSize) throw new RangeError("entry point outside RAM or misaligned");
    const sp = options.stackPointer ?? (this.ramSize - 16);
    if (sp < 0 || sp > this.ramSize) throw new RangeError("stack pointer outside RAM");
    x.silica_set_sp(BigInt(sp));
    x.silica_set_pc(BigInt(result.entry));
    this.lastStatus = "ready";
    return result;
  }

  reset(): void {
    this.core().silica_reset(1);
    this.lastStatus = "ready";
  }

  destroy(): void {
    this.destroyed = true;
    this.x = null;
    this.listeners = {};
    this.bps.clear();
  }

  // ── execution ────────────────────────────────────────────────────────────
  step(n = 1): StopReason {
    if (!Number.isInteger(n) || n < 1) throw new RangeError("n must be a positive integer");
    const x = this.core();
    // The first instruction always executes (even on a breakpoint); the remainder is a normal run.
    const stop = x.silica_step();
    this.drainConsole();
    if (stop === STOP.EXIT) return this.onExit();
    if (stop === STOP.HALT) return this.onHalt();
    return n === 1 ? "budget" : this.run(n - 1);
  }

  run(maxInstructions: number): StopReason {
    const x = this.core();
    if (!Number.isFinite(maxInstructions) || maxInstructions < 0) throw new RangeError("maxInstructions must be >= 0");
    let remaining = Math.floor(maxInstructions);
    while (remaining > 0) {
      const chunk = Math.min(remaining, 0x3fffffff);
      const before = this.stepsNow();
      const stop = x.silica_run(BigInt(chunk));
      const executed = Number(this.stepsNow() - before);
      remaining -= executed;
      this.drainConsole();
      if (stop === STOP.BREAKPOINT) { this.emit("breakpoint", this.getPC()); return "breakpoint"; }
      if (stop === STOP.EXIT) return this.onExit();
      if (stop === STOP.HALT) return this.onHalt();
      if (executed === 0) break; // defensive: BUDGET/CONSOLE stops always make progress
    }
    return "budget";
  }

  private stepsNow(): bigint { return this.snapshot()[ST.STEPS]; }

  private onExit(): StopReason {
    if (this.lastStatus !== "exited") {
      this.lastStatus = "exited";
      this.emit("exit", this.snapshot()[ST.EXITCODE]);
    }
    return "exited";
  }
  private onHalt(): StopReason {
    if (this.lastStatus !== "halted") {
      this.lastStatus = "halted";
      this.emit("halt", this.getStatus().reason ?? "halted");
    }
    return "halted";
  }

  private drainConsole(): void {
    const x = this.core();
    const n = x.silica_console_len();
    if (n === 0) return;
    if (n > CONSOLE_CAP) throw new Error("console buffer corrupted");
    const chunk = new Uint8Array(x.memory.buffer, x.silica_console_ptr(), n).slice();
    x.silica_console_clear();
    const text = this.decoder.decode(chunk, { stream: true });
    if (text) this.emit("console", text);
  }

  // ── state ────────────────────────────────────────────────────────────────
  private snapshot(): BigUint64Array {
    const x = this.core();
    return new BigUint64Array(x.memory.buffer, x.silica_state_ptr(), ST.WORDS);
  }

  getStatus(): CoreStatus {
    const s = this.snapshot();
    const instructions = s[ST.STEPS];
    if (s[ST.EXITED] === 1n) return { state: "exited", exitCode: s[ST.EXITCODE], instructions };
    const h = Number(s[ST.HALT]);
    if (h !== 0) return { state: "halted", reason: HALT_REASONS[h] ?? `halt ${h}`, instructions };
    return { state: "ready", instructions };
  }

  getRegisters(): RegisterFile {
    const s = this.snapshot();
    const out: RegisterFile = {};
    for (let i = 0; i < 31; i++) out[GPR_NAMES[i]] = s[ST.X0 + i];
    out.SP = s[ST.SP];
    out.PC = s[ST.PC];
    out.NZCV = s[ST.NZCV];
    return out;
  }

  setRegister(name: string, value: bigint): void {
    const x = this.core();
    const v = BigInt.asUintN(64, value);
    const key = name.toUpperCase();
    const m = /^X(\d+)$/.exec(key);
    if (m && Number(m[1]) < 31) x.silica_set_x(Number(m[1]), v);
    else if (key === "SP") x.silica_set_sp(v);
    else if (key === "PC") { x.silica_set_pc(v); this.lastStatus = "ready"; }
    else if (key === "NZCV") x.silica_set_nzcv(Number(v & 0xfn));
    else throw new RangeError(`unknown register ${name}`);
  }

  getPC(): number { return Number(this.snapshot()[ST.PC]); }

  // ── memory ───────────────────────────────────────────────────────────────
  getMemoryMap(): MemoryRegion[] { return [{ name: "RAM", base: 0, size: this.ramSize, writable: true }]; }

  private check(addr: number, len: number): void {
    if (!Number.isInteger(addr) || !Number.isInteger(len) || addr < 0 || len < 0 || addr + len > this.ramSize) {
      throw new RangeError(`memory range 0x${addr.toString(16)}+${len} is outside RAM (${this.ramSize} bytes)`);
    }
  }
  getMemory(addr: number, len: number): Uint8Array {
    this.check(addr, len);
    return this.mem8().slice(this.ramBase + addr, this.ramBase + addr + len);
  }
  setMemory(addr: number, data: Uint8Array): void {
    this.check(addr, data.length);
    this.mem8().set(data, this.ramBase + addr);
  }

  // ── breakpoints ──────────────────────────────────────────────────────────
  addBreakpoint(addr: number): void {
    const x = this.core();
    if (!Number.isInteger(addr) || addr < 0 || addr % 4 !== 0) throw new RangeError("breakpoint address must be a non-negative multiple of 4");
    if (this.bps.has(addr)) return;
    if (this.bps.size >= MAX_BREAKPOINTS) throw new RangeError(`at most ${MAX_BREAKPOINTS} breakpoints`);
    x.silica_bp_set(BigInt(addr));
    this.bps.add(addr);
  }
  removeBreakpoint(addr: number): void {
    if (this.bps.delete(addr)) this.core().silica_bp_clear(BigInt(addr));
  }
  clearBreakpoints(): void { this.core().silica_bp_clear_all(); this.bps.clear(); }
  getBreakpoints(): number[] { return [...this.bps].sort((a, b) => a - b); }

  // ── events ───────────────────────────────────────────────────────────────
  on<E extends PluginEventName>(event: E, cb: PluginEvents[E]): void {
    (this.listeners[event] ??= new Set()).add(cb as (...a: never[]) => void);
  }
  off<E extends PluginEventName>(event: E, cb: PluginEvents[E]): void {
    this.listeners[event]?.delete(cb as (...a: never[]) => void);
  }
  private emit<E extends PluginEventName>(event: E, ...args: Parameters<PluginEvents[E]>): void {
    for (const cb of this.listeners[event] ?? []) (cb as (...a: unknown[]) => void)(...args);
  }
}

export const silicaWasmFactory: PluginFactory = {
  id: "silica-aarch64-wasm",
  name: "Silica AArch64 (WebAssembly)",
  create: () => new SilicaWasmCore(),
};
