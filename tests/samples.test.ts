import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { join } from "node:path";
import { newCore, runToEnd } from "./util";

const sample = (n: string) => new Uint8Array(readFileSync(join(process.cwd(), "samples", "bin", `${n}.elf`)));
const expected = (n: string) => readFileSync(join(process.cwd(), "samples", "expected", `${n}.txt`), "utf8");

async function runSample(name: string) {
  const c = await newCore();
  const r = c.loadBinary(sample(name));
  assert.equal(r.format, "elf");
  const out = runToEnd(c);
  return { c, ...out };
}

test("hello: real clang-built ELF prints and exits 0", async () => {
  const { text, stop, c } = await runSample("hello");
  assert.equal(text, "Hello from a real AArch64 binary on SilicaHost!\n");
  assert.equal(stop, "exited");
  assert.equal(c.getStatus().exitCode, 0n);
});

test("fib (recursion, stack frames, bl/ret)", async () => {
  const { text, c } = await runSample("fib");
  assert.equal(text, "fib(0) = 0\nfib(5) = 5\nfib(10) = 55\nfib(15) = 610\nfib(20) = 6765\n");
  assert.equal(c.getStatus().exitCode, 0n);
});

test("sieve (bss zero-init, byte loads/stores, register-offset addressing)", async () => {
  const { text, c } = await runSample("sieve");
  assert.equal(text, "primes <= 10000: 1229\n");
  assert.equal(c.getStatus().exitCode, 0n);
});

test("crc32 (known vector 0xCBF43926)", async () => {
  const { text, c } = await runSample("crc32");
  assert.equal(text, 'crc32("123456789") = 3421780262\n');
  assert.equal(c.getStatus().exitCode, 0n);
});

test("console back-pressure: 100000 bytes through the 4 KiB buffer arrive intact, exit code propagates", async () => {
  const { text, c } = await runSample("flood");
  assert.equal(text.length, 100000);
  let want = "";
  for (let i = 0; i < 100000; i++) want += String.fromCharCode(97 + (i % 26));
  assert.equal(text, want);
  assert.equal(c.getStatus().exitCode, 7n);
});

for (const opt of ["O0", "O1", "O2", "Os"]) {
  test(`conformance-${opt}: AArch64 output equals the x86-64 oracle byte for byte`, async () => {
    const { text, stop } = await runSample(`conformance-${opt}`);
    assert.equal(stop, "exited");
    const want = expected(`conformance-${opt}`);
    assert.equal(want.split("\n").length - 1, 64, "oracle has 64 checks");
    assert.equal(text, want);
  });
}

test("conformance outputs agree across optimisation levels (guards the oracle itself)", () => {
  const base = expected("conformance-O0");
  for (const o of ["O1", "O2", "Os"]) assert.equal(expected(`conformance-${o}`), base);
});
