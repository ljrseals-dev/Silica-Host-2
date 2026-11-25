import { SilicaWasmCore } from "../src/cores/silica-wasm";

export const words = (...w: number[]): Uint8Array => {
  const b = new Uint8Array(w.length * 4);
  const dv = new DataView(b.buffer);
  w.forEach((x, i) => dv.setUint32(i * 4, x >>> 0, true));
  return b;
};

// Hand-assembled A64 (verified against llvm-mc): see core/tests/test_ext.cpp for the method.
export const MOVZ_X0_0 = 0xd2800000, BRK_HOSTCALL = 0xd42a2200, BRK_0 = 0xd4200000;
export const MOVZ = (rd: number, imm: number) => (0xd2800000 | (imm << 5) | rd) >>> 0;

/** sum 1..10 into x2, then exit(0). */
export const SUM_LOOP = words(0xd2800141, 0x8b010042, 0xf1000421, 0x54ffffc1, MOVZ_X0_0, BRK_HOSTCALL);

export async function newCore(ramBytes?: number): Promise<SilicaWasmCore> {
  const c = new SilicaWasmCore();
  await c.load(ramBytes ? { ramBytes } : {});
  return c;
}

/** Run a core to completion in slices; returns the console text. */
export function runToEnd(c: SilicaWasmCore, max = 1e9): { text: string; stop: string } {
  let text = "";
  c.on("console", (t) => (text += t));
  let left = max, stop = "budget";
  while (left > 0 && (stop = c.run(5_000_000)) === "budget") left -= 5_000_000;
  return { text, stop };
}
