/** Minimal, defensive ELF64 (little-endian, AArch64) loader. Parses and validates; does not touch RAM. */

export class ElfError extends Error {
  constructor(message: string) { super(message); this.name = "ElfError"; }
}

export interface ElfSegment { vaddr: number; offset: number; filesz: number; memsz: number }
export interface ElfImage { entry: number; segments: ElfSegment[] }

const EM_AARCH64 = 183;
const PT_LOAD = 1;
const MAX_PHNUM = 64;

export function isElf(b: Uint8Array): boolean {
  return b.length >= 4 && b[0] === 0x7f && b[1] === 0x45 && b[2] === 0x4c && b[3] === 0x46;
}

function safeNumber(v: bigint, what: string): number {
  if (v > BigInt(Number.MAX_SAFE_INTEGER)) throw new ElfError(`${what} too large`);
  return Number(v);
}

/** Parse and validate an ELF image against a RAM size. */
export function parseElf(bytes: Uint8Array, ramSize: number): ElfImage {
  if (!isElf(bytes)) throw new ElfError("not an ELF file");
  if (bytes.length < 64) throw new ElfError("truncated ELF header");
  const dv = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  if (bytes[4] !== 2) throw new ElfError("only 64-bit ELF is supported");
  if (bytes[5] !== 1) throw new ElfError("only little-endian ELF is supported");
  const type = dv.getUint16(16, true);
  if (type !== 2) throw new ElfError(`unsupported ELF type ${type} (need a static ET_EXEC executable)`);
  const machine = dv.getUint16(18, true);
  if (machine !== EM_AARCH64) throw new ElfError(`not an AArch64 binary (e_machine=${machine})`);

  const entry = safeNumber(dv.getBigUint64(24, true), "entry point");
  const phoff = safeNumber(dv.getBigUint64(32, true), "program header offset");
  const phentsize = dv.getUint16(54, true);
  const phnum = dv.getUint16(56, true);
  if (phentsize < 56) throw new ElfError("bad program header entry size");
  if (phnum === 0) throw new ElfError("ELF has no program headers");
  if (phnum > MAX_PHNUM) throw new ElfError(`too many program headers (${phnum})`);
  if (phoff + phnum * phentsize > bytes.length) throw new ElfError("program headers outside file");

  const segments: ElfSegment[] = [];
  for (let i = 0; i < phnum; i++) {
    const o = phoff + i * phentsize;
    const ptype = dv.getUint32(o, true);
    if (ptype === 3) throw new ElfError("dynamically linked executables are not supported (PT_INTERP)");
    if (ptype !== PT_LOAD) continue;
    const offset = safeNumber(dv.getBigUint64(o + 8, true), "segment offset");
    const vaddr = safeNumber(dv.getBigUint64(o + 16, true), "segment address");
    const filesz = safeNumber(dv.getBigUint64(o + 32, true), "segment file size");
    const memsz = safeNumber(dv.getBigUint64(o + 40, true), "segment memory size");
    if (filesz > memsz) throw new ElfError("segment file size exceeds memory size");
    if (offset + filesz > bytes.length) throw new ElfError("segment data outside file");
    if (vaddr + memsz > ramSize) {
      throw new ElfError(
        `segment 0x${vaddr.toString(16)}..0x${(vaddr + memsz).toString(16)} does not fit in ${ramSize} bytes of RAM`,
      );
    }
    segments.push({ vaddr, offset, filesz, memsz });
  }
  if (segments.length === 0) throw new ElfError("ELF has no loadable segments");
  const sorted = [...segments].sort((a, b) => a.vaddr - b.vaddr);
  for (let i = 1; i < sorted.length; i++) {
    if (sorted[i].vaddr < sorted[i - 1].vaddr + sorted[i - 1].memsz) throw new ElfError("overlapping segments");
  }
  if (entry % 4 !== 0 || !segments.some((s) => entry >= s.vaddr && entry < s.vaddr + s.memsz)) {
    throw new ElfError(`entry point 0x${entry.toString(16)} is not inside a loadable segment`);
  }
  return { entry, segments };
}
