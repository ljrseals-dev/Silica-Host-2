/**
 * SilicaHost plugin contract (v1). Any emulator core that implements `EmulatorPlugin` can be hosted.
 *
 * Design rules:
 *  - Synchronous execution (`step`/`run`): the host owns scheduling, so a core never blocks the UI
 *    longer than the budget it is given.
 *  - Failures that are the guest's fault (bad instruction, bad access) are *status*, not exceptions.
 *    Exceptions are for host misuse (bad address range, oversize image, use after destroy).
 *  - Addresses and sizes are JS numbers (guest RAM is capped well below 2^53); register values are bigint.
 */

export type Capability =
  | "functional-only"
  | "breakpoints"
  | "console"
  | "elf"
  | "network"
  | "gpio"
  | "gdb"
  | "trace"
  | "multi-core";

export type RegisterFile = Record<string, bigint>;

export interface MemoryRegion {
  name: string;
  base: number;
  size: number;
  writable: boolean;
}

export interface InitOptions {
  /** Guest RAM in bytes (core-specific limits apply). */
  ramBytes?: number;
}

export interface LoadOptions {
  /** Load address for raw binaries (ignored for ELF). Default 0x1000. */
  base?: number;
  /** Override the entry point. */
  entry?: number;
  /** Initial stack pointer. Default: top of RAM, 16-byte aligned. */
  stackPointer?: number;
}

export interface LoadResult {
  format: "elf" | "raw";
  entry: number;
  bytes: number;
  segments: { addr: number; size: number }[];
}

export type StopReason = "budget" | "halted" | "breakpoint" | "exited";

export interface CoreStatus {
  state: "ready" | "halted" | "exited";
  /** Human-readable halt reason when state === "halted". */
  reason?: string;
  /** Guest exit code when state === "exited". */
  exitCode?: bigint;
  /** Instructions retired since the last reset. */
  instructions: bigint;
}

export interface PluginEvents {
  console: (text: string) => void;
  halt: (reason: string) => void;
  breakpoint: (pc: number) => void;
  exit: (code: bigint) => void;
}
export type PluginEventName = keyof PluginEvents;

export interface EmulatorPlugin {
  readonly id: string;
  readonly name: string;
  readonly version: string;
  readonly supportedArchs: readonly string[];
  readonly capabilities: readonly Capability[];

  /** Allocate the core. Must be called once before anything else. */
  load(options?: InitOptions): Promise<void>;
  /** Load an ELF or raw image, point PC/SP at it. Throws on malformed or oversize images. */
  loadBinary(data: ArrayBuffer | Uint8Array, options?: LoadOptions): LoadResult;
  /** Clear registers and RAM. */
  reset(): void;
  destroy(): void;

  /** Execute exactly `n` instructions (or until the core stops). A step never stops on a breakpoint it starts on. */
  step(n?: number): StopReason;
  /** Execute up to `maxInstructions`. */
  run(maxInstructions: number): StopReason;
  getStatus(): CoreStatus;

  getRegisters(): RegisterFile;
  setRegister(name: string, value: bigint): void;
  getPC(): number;

  getMemoryMap(): MemoryRegion[];
  /** Copy of guest memory. Throws RangeError when [addr, addr+len) is outside RAM. */
  getMemory(addr: number, len: number): Uint8Array;
  setMemory(addr: number, data: Uint8Array): void;

  addBreakpoint(addr: number): void;
  removeBreakpoint(addr: number): void;
  clearBreakpoints(): void;
  getBreakpoints(): number[];

  on<E extends PluginEventName>(event: E, cb: PluginEvents[E]): void;
  off<E extends PluginEventName>(event: E, cb: PluginEvents[E]): void;
}

export interface PluginFactory {
  readonly id: string;
  readonly name: string;
  create(): EmulatorPlugin;
}
