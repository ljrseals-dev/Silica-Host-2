import { test } from "node:test";
import assert from "node:assert/strict";
import { SilicaHost, type Scheduler } from "../src/host";
import { silicaWasmFactory } from "../src/cores/silica-wasm";
import { SUM_LOOP, words } from "./util";

/** Deterministic scheduler: manual clock that advances 1ms per call to now(), callbacks run on demand. */
function fakeScheduler() {
  let t = 0; const queue: (() => void)[] = [];
  const s: Scheduler = { now: () => (t += 1), schedule: (cb) => { queue.push(cb); return cb; }, cancel: (h) => { const i = queue.indexOf(h as () => void); if (i >= 0) queue.splice(i, 1); } };
  return { s, pending: () => queue.length, tick: () => queue.shift()?.() };
}
// An infinite loop: b .
const SPIN = words(0x14000000);

async function setup(opts = {}) {
  const fs = fakeScheduler();
  const host = new SilicaHost({ scheduler: fs.s, ...opts });
  host.registerPlugin(silicaWasmFactory);
  const inst = await host.createInstance("silica-aarch64-wasm", { name: "t" });
  return { host, inst, fs };
}

test("registry: duplicate/unknown plugin ids are rejected", async () => {
  const host = new SilicaHost();
  host.registerPlugin(silicaWasmFactory);
  assert.throws(() => host.registerPlugin(silicaWasmFactory), /already registered/);
  await assert.rejects(host.createInstance("nope"), /unknown plugin/);
  assert.equal(host.plugins().length, 1);
});

test("play runs slices until the guest exits, then reports state", async () => {
  const { inst, fs } = await setup();
  const states: string[] = [];
  inst.on("state", (s) => states.push(s));
  inst.loadBinary(SUM_LOOP);
  assert.equal(inst.state, "paused");
  inst.play();
  assert.equal(inst.state, "running");
  fs.tick();
  assert.equal(inst.state, "exited");
  assert.equal(fs.pending(), 0);
  assert.deepEqual(states, ["paused", "running", "exited"]);
  inst.play(); // no-op on finished instance
  assert.equal(inst.state, "exited");
});

test("a spinning guest yields between slices and can be paused", async () => {
  const { inst, fs } = await setup();
  inst.loadBinary(SPIN);
  inst.play();
  for (let i = 0; i < 5; i++) { fs.tick(); assert.equal(inst.state, "running"); assert.equal(fs.pending(), 1, "re-scheduled, never blocks"); }
  assert.ok(inst.plugin.getStatus().instructions > 0n);
  inst.pause();
  assert.equal(inst.state, "paused");
  assert.equal(fs.pending(), 0);
});

test("play(limit) stops after the requested number of instructions", async () => {
  const { inst, fs } = await setup();
  inst.loadBinary(SPIN);
  inst.play(1000);
  while (fs.pending()) fs.tick();
  assert.equal(inst.state, "paused");
  const n = inst.plugin.getStatus().instructions;
  assert.ok(n >= 1000n && n < 1_000_000n, `ran ${n}`);
});

test("breakpoint during play pauses with lastStop=breakpoint; play resumes past it", async () => {
  const { inst, fs } = await setup();
  inst.loadBinary(SUM_LOOP);
  inst.plugin.addBreakpoint(0x1008);
  inst.play(); fs.tick();
  assert.equal(inst.state, "paused");
  assert.equal(inst.lastStop, "breakpoint");
  assert.equal(inst.plugin.getPC(), 0x1008);
  inst.plugin.clearBreakpoints();
  inst.play(); fs.tick();
  assert.equal(inst.state, "exited");
});

test("step on halted/exited/idle instance is inert; step otherwise advances", async () => {
  const { inst } = await setup();
  assert.equal(inst.state, "idle");
  inst.step();
  assert.equal(inst.plugin.getStatus().instructions, 0n);
  inst.loadBinary(SUM_LOOP);
  assert.equal(inst.step(3), "budget");
  assert.equal(inst.plugin.getStatus().instructions, 3n);
  inst.loadBinary(words(0xffffffff));
  assert.equal(inst.step(), "halted");
  assert.equal(inst.state, "halted");
});

test("restart reloads the last image from a clean state", async () => {
  const { inst, fs } = await setup();
  inst.loadBinary(SUM_LOOP);
  inst.play(); fs.tick();
  assert.equal(inst.state, "exited");
  inst.restart();
  assert.equal(inst.state, "paused");
  assert.equal(inst.plugin.getRegisters().X2, 0n);
  assert.equal(inst.plugin.getPC(), 0x1000);
  inst.play(); fs.tick();
  assert.equal(inst.plugin.getRegisters().X2, 55n);
});

test("console history is capped per instance and cleared on load", async () => {
  const { inst, fs } = await setup({ consoleLimit: 64 });
  // Guest: write 200 bytes from 0x4000 then exit.
  const code = words(0xd2800040 /* movz x0,#2 */, 0xd2880001 /* movz x1,#0x4000 */, 0xd2801902 /* movz x2,#200 */, 0xd42a2200, 0xd2800000, 0xd42a2200);
  inst.loadBinary(code);
  inst.plugin.setMemory(0x4000, new Uint8Array(200).fill(65));
  const seen: string[] = [];
  inst.on("console", (t) => seen.push(t));
  inst.play(); fs.tick();
  assert.equal(seen.join("").length, 200, "listeners get everything");
  assert.equal(inst.consoleText.length, 64, "history is bounded");
  inst.loadBinary(code);
  assert.equal(inst.consoleText, "");
});

test("destroyInstance stops and frees; ids are never reused", async () => {
  const { host, inst, fs } = await setup();
  inst.loadBinary(SPIN); inst.play();
  host.destroyInstance(inst.id);
  assert.equal(host.getInstance(inst.id), undefined);
  assert.equal(fs.pending(), 0);
  assert.throws(() => inst.plugin.getPC(), /destroyed/);
  const next = await host.createInstance("silica-aarch64-wasm");
  assert.notEqual(next.id, inst.id);
  host.destroyInstance(12345); // unknown id: no-op
});

test("many instances run independently", async () => {
  const { host } = await setup();
  const all = await Promise.all(Array.from({ length: 8 }, () => host.createInstance("silica-aarch64-wasm", { init: { ramBytes: 1 << 20 } })));
  all.forEach((i, k) => { i.loadBinary(SUM_LOOP); i.plugin.setRegister("X2", BigInt(k * 100)); i.step(1000); });
  all.forEach((i, k) => assert.equal(i.plugin.getRegisters().X2, BigInt(k * 100 + 55)));
  assert.equal(host.instances().length, 9);
});
