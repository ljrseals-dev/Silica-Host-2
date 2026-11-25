import { test } from "node:test";
import assert from "node:assert/strict";
import { ElfError, isElf, parseElf } from "../src/elf";

/** Build a minimal valid AArch64 ET_EXEC ELF with the given PT_LOAD segments. */
function makeElf(segs: { vaddr: number; filesz: number; memsz: number }[], opts: { entry?: number; type?: number; machine?: number; cls?: number; data?: number } = {}): Uint8Array {
  const phnum = segs.length;
  const dataStart = 64 + phnum * 56;
  const total = dataStart + segs.reduce((a, s) => a + s.filesz, 0);
  const b = new Uint8Array(total);
  const dv = new DataView(b.buffer);
  b.set([0x7f, 0x45, 0x4c, 0x46, opts.cls ?? 2, opts.data ?? 1, 1]);
  dv.setUint16(16, opts.type ?? 2, true);
  dv.setUint16(18, opts.machine ?? 183, true);
  dv.setBigUint64(24, BigInt(opts.entry ?? segs[0]?.vaddr ?? 0), true);
  dv.setBigUint64(32, 64n, true);
  dv.setUint16(54, 56, true);
  dv.setUint16(56, phnum, true);
  let off = dataStart;
  segs.forEach((s, i) => {
    const o = 64 + i * 56;
    dv.setUint32(o, 1, true);
    dv.setBigUint64(o + 8, BigInt(off), true);
    dv.setBigUint64(o + 16, BigInt(s.vaddr), true);
    dv.setBigUint64(o + 32, BigInt(s.filesz), true);
    dv.setBigUint64(o + 40, BigInt(s.memsz), true);
    off += s.filesz;
  });
  return b;
}
const RAM = 1 << 20;
const bad = (b: Uint8Array, re: RegExp) => assert.throws(() => parseElf(b, RAM), (e: unknown) => e instanceof ElfError && re.test(e.message));

test("accepts a valid executable and reports segments + entry", () => {
  const img = parseElf(makeElf([{ vaddr: 0x1000, filesz: 16, memsz: 64 }, { vaddr: 0x2000, filesz: 0, memsz: 4096 }]), RAM);
  assert.equal(img.entry, 0x1000);
  assert.equal(img.segments.length, 2);
  assert.equal(img.segments[0].memsz, 64);
});
test("isElf", () => { assert.ok(isElf(makeElf([{ vaddr: 0x1000, filesz: 4, memsz: 4 }]))); assert.ok(!isElf(new Uint8Array([1, 2, 3, 4]))); assert.ok(!isElf(new Uint8Array(2))); });
test("rejects wrong class / endianness / type / machine", () => {
  const seg = [{ vaddr: 0x1000, filesz: 4, memsz: 4 }];
  bad(makeElf(seg, { cls: 1 }), /64-bit/);
  bad(makeElf(seg, { data: 2 }), /little-endian/);
  bad(makeElf(seg, { type: 3 }), /ET_EXEC/);
  bad(makeElf(seg, { machine: 62 }), /AArch64/);
});
test("rejects truncated, segment-out-of-file, and filesz > memsz", () => {
  bad(makeElf([{ vaddr: 0x1000, filesz: 4, memsz: 4 }]).subarray(0, 40), /truncated/);
  const e = makeElf([{ vaddr: 0x1000, filesz: 64, memsz: 64 }]);
  bad(e.subarray(0, e.length - 8), /outside file/);
  bad(makeElf([{ vaddr: 0x1000, filesz: 8, memsz: 4 }]), /exceeds memory size/);
});
test("rejects segments that do not fit in RAM, overlap, or have a bad entry", () => {
  bad(makeElf([{ vaddr: RAM - 8, filesz: 4, memsz: 64 }]), /does not fit/);
  bad(makeElf([{ vaddr: 0x1000, filesz: 4, memsz: 0x100 }, { vaddr: 0x1080, filesz: 4, memsz: 4 }]), /overlapping/);
  bad(makeElf([{ vaddr: 0x1000, filesz: 4, memsz: 4 }], { entry: 0x5000 }), /entry point/);
  bad(makeElf([{ vaddr: 0x1000, filesz: 8, memsz: 8 }], { entry: 0x1002 }), /entry point/);
});
test("rejects PT_INTERP, no program headers, and absurd header counts", () => {
  const e = makeElf([{ vaddr: 0x1000, filesz: 4, memsz: 4 }]);
  const dyn = e.slice(); new DataView(dyn.buffer).setUint32(64, 3, true);
  bad(dyn, /PT_INTERP/);
  const none = e.slice(); new DataView(none.buffer).setUint16(56, 0, true);
  bad(none, /no program headers/);
  const many = e.slice(); new DataView(many.buffer).setUint16(56, 5000, true);
  bad(many, /too many/);
});
test("rejects 64-bit values that cannot be represented", () => {
  const e = makeElf([{ vaddr: 0x1000, filesz: 4, memsz: 4 }]);
  new DataView(e.buffer).setBigUint64(64 + 16, 0xffffffffffffffffn, true);
  bad(e, /too large/);
});
