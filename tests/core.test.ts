import { test } from "node:test";
import assert from "node:assert/strict";
import { SilicaWasmCore } from "../src/cores/silica-wasm";
import { MOVZ, SUM_LOOP, BRK_HOSTCALL, MOVZ_X0_0, newCore, runToEnd, words } from "./util";

test("runs a hand-assembled loop to a clean guest exit", async () => {
  const c = await newCore();
  const r = c.loadBinary(SUM_LOOP);
  assert.deepEqual([r.format, r.entry, r.bytes], ["raw", 0x1000, 24]);
  assert.equal(c.run(1000), "exited");
  const regs = c.getRegisters();
  assert.equal(regs.X2, 55n);
  assert.equal(regs.X1, 0n);
  const st = c.getStatus();
  assert.equal(st.state, "exited");
  assert.equal(st.exitCode, 0n);
  assert.equal(st.instructions, 33n);
  assert.equal(c.run(10), "exited", "running an exited core stays exited");
});

test("initial state: SP at top of RAM, PC at entry, flags clear", async () => {
  const c = await newCore(1 << 20);
  c.loadBinary(SUM_LOOP, { base: 0x2000 });
  const r = c.getRegisters();
  assert.equal(r.PC, 0x2000n);
  assert.equal(r.SP, BigInt((1 << 20) - 16));
  assert.equal(r.NZCV, 0n);
  c.loadBinary(SUM_LOOP, { stackPointer: 0x8000, entry: 0x1004 });
  assert.equal(c.getRegisters().SP, 0x8000n);
  assert.equal(c.getPC(), 0x1004);
});

test("hostcall putc/write deliver console text, including multi-byte UTF-8 split across drains", async () => {
  const c = await newCore();
  const enc = new TextEncoder().encode("héllo ✓\n");
  const code = [MOVZ(0, 2), MOVZ(1, 0x4000), MOVZ(2, enc.length), BRK_HOSTCALL, MOVZ(0, 0), MOVZ(1, 3), BRK_HOSTCALL];
  c.loadBinary(words(...code));
  c.setMemory(0x4000, enc);
  const { text, stop } = runToEnd(c);
  assert.equal(text, "héllo ✓\n");
  assert.equal(stop, "exited");
  assert.equal(c.getStatus().exitCode, 3n);
});

test("hostcall: unknown op returns -1; bad guest pointer returns -1 and does not crash", async () => {
  const c = await newCore();
  c.loadBinary(words(MOVZ(0, 99), BRK_HOSTCALL, 0xaa0003f3 /* mov x19, x0 */, MOVZ(0, 2), MOVZ(1, 0), 0xf2a00001 /* movk x1,#0,lsl16 */, 0xd2a80001 /* movz x1,#0x4000,lsl16 -> 0x40000000 */, MOVZ(2, 16), BRK_HOSTCALL, 0xaa0003f4 /* mov x20,x0 */, MOVZ_X0_0, MOVZ(1, 0), BRK_HOSTCALL));
  assert.equal(c.run(100), "exited");
  const r = c.getRegisters();
  assert.equal(r.X19, 0xffffffffffffffffn);
  assert.equal(r.X20, 0xffffffffffffffffn);
});

test("halt reasons: zero word, invalid instruction, memory fault", async () => {
  let c = await newCore();
  c.loadBinary(words(0x00000000));
  assert.equal(c.run(10), "halted");
  assert.match(c.getStatus().reason!, /all-zero/);

  c = await newCore();
  c.loadBinary(words(0xffffffff));
  assert.equal(c.run(10), "halted");
  assert.match(c.getStatus().reason!, /invalid/);
  assert.equal(c.getPC(), 0x1000, "PC stays on the faulting instruction");

  c = await newCore();
  c.loadBinary(words(0xf9400020 /* ldr x0,[x1] */));
  c.setRegister("X1", 0xffffff00n);
  assert.equal(c.run(10), "halted");
  assert.match(c.getStatus().reason!, /memory fault/);

  c = await newCore();
  c.loadBinary(words(0xd4200000 /* brk #0 : a normal BRK is a halt, not a hostcall */));
  assert.equal(c.run(10), "halted");
  assert.match(c.getStatus().reason!, /BRK/);
});

test("halt event fires exactly once; setting PC resumes a halted core", async () => {
  const c = await newCore();
  const halts: string[] = [];
  c.on("halt", (r) => halts.push(r));
  c.loadBinary(words(0xffffffff));
  c.run(5); c.run(5);
  assert.equal(halts.length, 1);
  c.setRegister("PC", 0x1000n);
  assert.equal(c.getStatus().state, "ready");
});

test("breakpoints stop before the instruction; resuming and stepping make progress", async () => {
  const c = await newCore();
  c.loadBinary(SUM_LOOP);
  c.addBreakpoint(0x1008);              // subs x1,x1,#1 in the loop body
  const hits: number[] = [];
  c.on("breakpoint", (pc) => hits.push(pc));
  assert.equal(c.run(1000), "breakpoint");
  assert.equal(c.getPC(), 0x1008);
  assert.equal(c.getRegisters().X2, 10n, "first add executed, subs not yet");
  assert.equal(c.step(1), "budget");     // steps over the breakpoint it started on
  assert.equal(c.getPC(), 0x100c);
  assert.equal(c.run(1000), "breakpoint");
  assert.equal(c.getRegisters().X2, 19n);
  c.removeBreakpoint(0x1008);
  assert.deepEqual(c.getBreakpoints(), []);
  assert.equal(c.run(1000), "exited");
  assert.equal(c.getRegisters().X2, 55n);
  assert.equal(hits.length, 2);
});

test("a breakpoint on the entry point fires before anything executes; then resume steps over it", async () => {
  const c = await newCore();
  c.loadBinary(SUM_LOOP);
  c.addBreakpoint(0x1000);
  assert.equal(c.run(1000), "breakpoint");
  assert.equal(c.getPC(), 0x1000);
  assert.equal(c.getStatus().instructions, 0n);
  assert.equal(c.run(1000), "exited", "second run steps over the breakpoint it stopped on");
  c.loadBinary(SUM_LOOP);                // reload: breakpoint fires again from a fresh start
  assert.equal(c.run(1000), "breakpoint");
  assert.throws(() => c.step(0), RangeError);
});

test("breakpoint API validation and limits", async () => {
  const c = await newCore();
  assert.throws(() => c.addBreakpoint(0x1002), RangeError);
  assert.throws(() => c.addBreakpoint(-4), RangeError);
  for (let i = 0; i < 64; i++) c.addBreakpoint(0x1000 + i * 4);
  assert.throws(() => c.addBreakpoint(0x2000), RangeError);
  c.addBreakpoint(0x1000);               // duplicate is fine
  c.clearBreakpoints();
  assert.deepEqual(c.getBreakpoints(), []);
});

test("memory access is range-checked and copies data", async () => {
  const c = await newCore(64 * 1024);
  c.setMemory(0x100, new Uint8Array([1, 2, 3, 4]));
  const m = c.getMemory(0x100, 4);
  assert.deepEqual([...m], [1, 2, 3, 4]);
  m[0] = 9;
  assert.equal(c.getMemory(0x100, 1)[0], 1, "getMemory returns a copy");
  assert.deepEqual(c.getMemory(65536, 0), new Uint8Array(0));
  assert.throws(() => c.getMemory(65535, 2), RangeError);
  assert.throws(() => c.getMemory(-1, 1), RangeError);
  assert.throws(() => c.setMemory(65534, new Uint8Array(4)), RangeError);
  assert.throws(() => c.getMemory(1.5, 1), RangeError);
  assert.deepEqual(c.getMemoryMap(), [{ name: "RAM", base: 0, size: 65536, writable: true }]);
});

test("loadBinary rejects oversize / empty / malformed images without corrupting state", async () => {
  const c = await newCore(64 * 1024);
  assert.throws(() => c.loadBinary(new Uint8Array(70000)), RangeError);
  assert.throws(() => c.loadBinary(new Uint8Array(0)));
  assert.throws(() => c.loadBinary(SUM_LOOP, { base: 3 }), RangeError);
  assert.throws(() => c.loadBinary(SUM_LOOP, { entry: 0x100000 }), RangeError);
  assert.throws(() => c.loadBinary(SUM_LOOP, { stackPointer: 1 << 30 }), RangeError);
  assert.throws(() => c.loadBinary(new Uint8Array([0x7f, 0x45, 0x4c, 0x46, 2, 1, 1, 0])), /truncated/);
  c.loadBinary(SUM_LOOP);                // still usable
  assert.equal(c.run(1000), "exited");
});

test("reset clears registers, RAM and status", async () => {
  const c = await newCore();
  c.loadBinary(SUM_LOOP);
  c.run(1000);
  c.reset();
  assert.equal(c.getRegisters().X2, 0n);
  assert.equal(c.getStatus().state, "ready");
  assert.ok(c.getMemory(0x1000, 24).every((b) => b === 0));
  assert.equal(c.getStatus().instructions, 0n);
});

test("setRegister: X0-X30, SP, PC, NZCV; XZR/invalid names rejected; values wrap to 64 bits", async () => {
  const c = await newCore();
  c.setRegister("x5", 0x1234n);
  c.setRegister("X30", -1n);
  c.setRegister("SP", 0x8000n);
  c.setRegister("NZCV", 0b1010n);
  const r = c.getRegisters();
  assert.equal(r.X5, 0x1234n);
  assert.equal(r.X30, 0xffffffffffffffffn);
  assert.equal(r.SP, 0x8000n);
  assert.equal(r.NZCV, 0b1010n);
  assert.throws(() => c.setRegister("X31", 1n), RangeError);
  assert.throws(() => c.setRegister("FOO", 1n), RangeError);
});

test("independent instances do not share state", async () => {
  const a = await newCore(), b = await newCore();
  a.loadBinary(SUM_LOOP);
  b.loadBinary(words(MOVZ(7, 42), MOVZ_X0_0, MOVZ(1, 1), BRK_HOSTCALL));
  a.run(1000); b.run(1000);
  assert.equal(a.getRegisters().X2, 55n);
  assert.equal(a.getRegisters().X7, 0n);
  assert.equal(b.getRegisters().X7, 42n);
  assert.equal(b.getRegisters().X2, 0n);
  assert.equal(b.getStatus().exitCode, 1n);
});

test("RAM size validation and large allocation", async () => {
  for (const bad of [0, 1000, 64 * 1024 + 8, 512 * 1024 * 1024, -16, 65536.5]) {
    await assert.rejects(new SilicaWasmCore().load({ ramBytes: bad }), RangeError, `ramBytes=${bad}`);
  }
  const big = await newCore(64 * 1024 * 1024);
  big.setMemory(64 * 1024 * 1024 - 4, new Uint8Array([1, 2, 3, 4]));
  assert.deepEqual([...big.getMemory(64 * 1024 * 1024 - 4, 4)], [1, 2, 3, 4]);
});

test("lifecycle misuse throws clearly", async () => {
  const c = new SilicaWasmCore();
  assert.throws(() => c.run(1), /not loaded/);
  await c.load();
  await assert.rejects(c.load(), /already loaded/);
  c.destroy();
  assert.throws(() => c.getRegisters(), /destroyed/);
  const d = await newCore();
  assert.throws(() => d.run(-1), RangeError);
  assert.throws(() => d.run(NaN), RangeError);
  assert.equal(d.run(0), "budget");
});

test("off() removes listeners", async () => {
  const c = await newCore();
  let n = 0;
  const f = () => { n++; };
  c.on("halt", f); c.off("halt", f);
  c.loadBinary(words(0xffffffff)); c.run(5);
  assert.equal(n, 0);
});
