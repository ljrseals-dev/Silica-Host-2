/**
 * Binary interface of the Silica wasm core. Mirrors core/wasm/silica_abi.h; tests/abi.test.ts parses the
 * header and fails if the two drift apart.
 */
export const ABI_VERSION = 1;

export const ST = {
  X0: 0, SP: 31, PC: 32, NZCV: 33, EL: 34, SPSEL: 35, STEPS: 36, HALT: 37,
  EXITED: 38, EXITCODE: 39, SYS0: 40, V0: 48, WORDS: 112,
} as const;

export const STOP = { BUDGET: 0, HALT: 1, BREAKPOINT: 2, CONSOLE: 3, EXIT: 4 } as const;

export const HOSTCALL_IMM = 0x5110;
export const CONSOLE_CAP = 4096;
export const MAX_BREAKPOINTS = 64;

/** Core HaltReason values (index = value of ST.HALT). */
export const HALT_REASONS = [
  "none",
  "executed an all-zero word",
  "BRK instruction",
  "invalid or unmodelled instruction",
  "memory fault (out-of-range access or misaligned PC)",
] as const;

export interface SilicaExports {
  memory: WebAssembly.Memory;
  silica_abi_version(): number;
  silica_init(ramBytes: number): number;
  silica_ram_ptr(): number;
  silica_ram_size(): number;
  silica_reset(clearRam: number): void;
  silica_set_x(idx: number, v: bigint): void;
  silica_set_sp(v: bigint): void;
  silica_set_pc(v: bigint): void;
  silica_set_nzcv(f: number): void;
  silica_bp_set(addr: bigint): number;
  silica_bp_clear(addr: bigint): void;
  silica_bp_clear_all(): void;
  silica_run(maxSteps: bigint): number;
  silica_step(): number;
  silica_state_ptr(): number;
  silica_console_ptr(): number;
  silica_console_len(): number;
  silica_console_clear(): void;
}
