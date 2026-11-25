import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { join } from "node:path";
import { ABI_VERSION, CONSOLE_CAP, HOSTCALL_IMM, MAX_BREAKPOINTS, ST, STOP } from "../src/abi";

const header = readFileSync(join(process.cwd(), "core", "wasm", "silica_abi.h"), "utf8");
const def = (name: string): number => {
  const m = new RegExp(`#define\\s+${name}\\s+(0x[0-9a-fA-F]+|\\d+)u?`).exec(header);
  assert.ok(m, `missing ${name} in silica_abi.h`);
  return Number(m![1]);
};

test("TypeScript ABI constants match core/wasm/silica_abi.h", () => {
  assert.equal(ABI_VERSION, def("SILICA_ABI_VERSION"));
  const states: Record<string, number> = {
    X0: def("SILICA_ST_X0"), SP: def("SILICA_ST_SP"), PC: def("SILICA_ST_PC"), NZCV: def("SILICA_ST_NZCV"),
    EL: def("SILICA_ST_EL"), SPSEL: def("SILICA_ST_SPSEL"), STEPS: def("SILICA_ST_STEPS"), HALT: def("SILICA_ST_HALT"),
    EXITED: def("SILICA_ST_EXITED"), EXITCODE: def("SILICA_ST_EXITCODE"), SYS0: def("SILICA_ST_SYS0"),
    V0: def("SILICA_ST_V0"), WORDS: def("SILICA_STATE_WORDS"),
  };
  for (const [k, v] of Object.entries(states)) assert.equal((ST as Record<string, number>)[k], v, `ST.${k}`);
  assert.equal(STOP.BUDGET, def("SILICA_STOP_BUDGET"));
  assert.equal(STOP.HALT, def("SILICA_STOP_HALT"));
  assert.equal(STOP.BREAKPOINT, def("SILICA_STOP_BREAKPOINT"));
  assert.equal(STOP.CONSOLE, def("SILICA_STOP_CONSOLE"));
  assert.equal(STOP.EXIT, def("SILICA_STOP_EXIT"));
  assert.equal(HOSTCALL_IMM, def("SILICA_HOSTCALL_IMM"));
  assert.equal(CONSOLE_CAP, def("SILICA_CONSOLE_CAP"));
  assert.equal(MAX_BREAKPOINTS, def("SILICA_MAX_BREAKPOINTS"));
});
