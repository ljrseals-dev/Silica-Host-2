import { SilicaHost, type Instance, type RunState } from "../host";
import { silicaWasmFactory } from "../cores/silica-wasm";
import hello from "../../samples/bin/hello.elf";
import fib from "../../samples/bin/fib.elf";
import sieve from "../../samples/bin/sieve.elf";
import crc32 from "../../samples/bin/crc32.elf";
import flood from "../../samples/bin/flood.elf";
import conf from "../../samples/bin/conformance-O2.elf";
import confExpected from "../../samples/expected/conformance-O2.txt";

interface Sample { id: string; label: string; data: Uint8Array; expected?: string }
const SAMPLES: Sample[] = [
  { id: "hello", label: "hello — print + exit", data: hello },
  { id: "fib", label: "fib — recursion", data: fib },
  { id: "sieve", label: "sieve — primes ≤ 10000", data: sieve },
  { id: "crc32", label: "crc32 — known vector", data: crc32 },
  { id: "conf", label: "conformance — 64 checks vs x86-64 oracle", data: conf, expected: confExpected },
  { id: "flood", label: "flood — 100 KB console output", data: flood },
];

const $ = <T extends HTMLElement>(id: string): T => {
  const el = document.getElementById(id);
  if (!el) throw new Error(`missing #${id}`);
  return el as T;
};
const hex = (v: bigint | number, pad = 0) => "0x" + v.toString(16).padStart(pad, "0");
const parseHex = (s: string): number | null => {
  const t = s.trim().replace(/^0x/i, "");
  return /^[0-9a-f]{1,9}$/i.test(t) ? parseInt(t, 16) : null;
};

const host = new SilicaHost();
host.registerPlugin(silicaWasmFactory);
let active: Instance | null = null;
const prevRegs = new Map<number, Record<string, bigint>>();
const expectedFor = new Map<number, string>();
let memAddr = 0x1000;
let prevMem: { addr: number; bytes: Uint8Array } | null = null;

// ── console ────────────────────────────────────────────────────────────────
function logHost(msg: string, cls = "host"): void {
  const span = document.createElement("span");
  span.className = cls;
  span.textContent = msg + "\n";
  $("console").appendChild(span);
  scrollConsole();
}
function scrollConsole(): void { const p = $("pane-console"); p.scrollTop = p.scrollHeight; }
function renderConsole(inst: Instance): void {
  const c = $("console");
  c.textContent = "";
  const t = document.createElement("span");
  t.textContent = inst.consoleText;
  c.appendChild(t);
}

// ── rendering ──────────────────────────────────────────────────────────────
let raf = 0;
function scheduleRender(): void { if (!raf) raf = requestAnimationFrame(() => { raf = 0; render(); }); }

function render(): void {
  renderInstances();
  const inst = active;
  const dis = !inst;
  for (const id of ["btn-run", "btn-pause", "btn-step", "btn-step100", "btn-restart", "btn-load-sample", "mem-go", "mem-pc", "mem-sp", "mem-prev", "mem-next", "bp-add"]) {
    ($(id) as HTMLButtonElement).disabled = dis;
  }
  if (!inst) { $("st-state").textContent = "no instance"; return; }
  const st = inst.plugin.getStatus();
  const state: RunState = inst.state;
  ($("btn-run") as HTMLButtonElement).disabled = !(state === "paused");
  ($("btn-pause") as HTMLButtonElement).disabled = state !== "running";
  ($("btn-step") as HTMLButtonElement).disabled = !(state === "paused");
  ($("btn-step100") as HTMLButtonElement).disabled = !(state === "paused");
  ($("btn-restart") as HTMLButtonElement).disabled = !inst.lastLoad;

  const regs = inst.plugin.getRegisters();
  $("st-state").textContent = state + (inst.lastStop === "breakpoint" ? " (breakpoint)" : "");
  $("st-pc").textContent = hex(regs.PC);
  $("st-n").textContent = st.instructions.toLocaleString();
  $("st-ips").textContent = inst.ips > 0 ? (inst.ips / 1e6).toFixed(1) + " MIPS" : "—";
  $("st-detail").textContent =
    st.state === "halted" ? `halted: ${st.reason}` : st.state === "exited" ? `guest exited with code ${st.exitCode}` : "";

  const nz = Number(regs.NZCV);
  const flags = $("flags");
  flags.textContent = "";
  [["N", 8], ["Z", 4], ["C", 2], ["V", 1]].forEach(([n, m]) => {
    const s = document.createElement("span");
    s.className = "flag" + (nz & (m as number) ? " on" : "");
    s.textContent = n as string;
    flags.appendChild(s);
  });

  const prev = prevRegs.get(inst.id) ?? {};
  const grid = $("regs");
  grid.textContent = "";
  const names = ["PC", "SP", ...Array.from({ length: 31 }, (_, i) => `X${i}`)];
  for (const n of names) {
    const a = document.createElement("span"); a.className = "n"; a.textContent = n;
    const b = document.createElement("span"); b.className = "v" + (prev[n] !== undefined && prev[n] !== regs[n] ? " chg" : "");
    b.textContent = hex(regs[n], 16); b.title = regs[n].toString(10);
    grid.append(a, b);
  }
  prevRegs.set(inst.id, regs);

  const bps = $("bps");
  bps.textContent = "";
  for (const a of inst.plugin.getBreakpoints()) {
    const li = document.createElement("li");
    const t = document.createElement("span"); t.textContent = hex(a);
    const x = document.createElement("button"); x.textContent = "✕"; x.setAttribute("aria-label", `Remove breakpoint ${hex(a)}`);
    x.onclick = () => { inst.plugin.removeBreakpoint(a); scheduleRender(); };
    li.append(t, x); bps.appendChild(li);
  }
  if (!$("pane-memory").hidden) renderMemory(inst, Number(regs.PC));
}

function renderInstances(): void {
  const ul = $("instances");
  ul.textContent = "";
  for (const i of host.instances()) {
    const li = document.createElement("li");
    li.className = "inst" + (i === active ? " active" : "");
    li.tabIndex = 0;
    const dot = document.createElement("span"); dot.className = "dot " + i.state;
    const nm = document.createElement("span"); nm.textContent = i.name;
    const x = document.createElement("button"); x.className = "x"; x.textContent = "×"; x.setAttribute("aria-label", `Close ${i.name}`);
    x.onclick = (e) => { e.stopPropagation(); closeInstance(i); };
    li.append(dot, nm, x);
    const select = () => { setActive(i); };
    li.onclick = select;
    li.onkeydown = (e) => { if (e.key === "Enter" || e.key === " ") { e.preventDefault(); select(); } };
    ul.appendChild(li);
  }
}

function renderMemory(inst: Instance, pc: number): void {
  const size = inst.plugin.getMemoryMap()[0].size;
  memAddr = Math.max(0, Math.min(memAddr, size - 256)) & ~0xf;
  ($("memaddr") as HTMLInputElement).value = memAddr.toString(16);
  const bytes = inst.plugin.getMemory(memAddr, 256);
  const prev = prevMem && prevMem.addr === memAddr ? prevMem.bytes : null;
  const dump = $("memdump");
  dump.textContent = "";
  for (let r = 0; r < 16; r++) {
    const rowAddr = memAddr + r * 16;
    const line = document.createElement("div");
    const a = document.createElement("span"); a.className = "addr"; a.textContent = hex(rowAddr, 8) + "  ";
    line.appendChild(a);
    let ascii = "";
    for (let c = 0; c < 16; c++) {
      const i = r * 16 + c;
      const s = document.createElement("span");
      const here = rowAddr + c;
      s.className = (prev && prev[i] !== bytes[i] ? "chg " : "") + (here >= pc && here < pc + 4 ? "pc" : "");
      s.textContent = bytes[i].toString(16).padStart(2, "0") + (c === 7 ? "  " : " ");
      line.appendChild(s);
      ascii += bytes[i] >= 32 && bytes[i] < 127 ? String.fromCharCode(bytes[i]) : ".";
    }
    line.appendChild(document.createTextNode(" |" + ascii + "|"));
    dump.appendChild(line);
  }
  prevMem = { addr: memAddr, bytes };
}

// ── instance management ───────────────────────────────────────────────────
function setActive(i: Instance | null): void {
  active = i;
  prevMem = null;
  if (i) renderConsole(i); else $("console").textContent = "";
  scheduleRender();
}

async function newInstance(): Promise<Instance> {
  const inst = await host.createInstance("silica-aarch64-wasm");
  inst.on("console", (t) => {
    if (active !== inst) return;
    const c = $("console");
    const last = c.lastElementChild;
    if (last && !last.className) last.appendChild(document.createTextNode(t));
    else { const s = document.createElement("span"); s.textContent = t; c.appendChild(s); }
    scrollConsole();
  });
  inst.on("tick", scheduleRender);
  inst.on("state", (s) => {
    if (s === "exited" || s === "halted") onFinished(inst);
    scheduleRender();
  });
  setActive(inst);
  logHost(`[host] created ${inst.name} (4 MiB RAM, isolated wasm instance)`);
  return inst;
}

function onFinished(inst: Instance): void {
  if (active !== inst) return;
  const st = inst.plugin.getStatus();
  if (st.state === "exited") {
    logHost(`[host] guest exited with code ${st.exitCode} after ${st.instructions.toLocaleString()} instructions`, st.exitCode === 0n ? "ok" : "err");
    const want = expectedFor.get(inst.id);
    if (want !== undefined) {
      const got = inst.consoleText.split("\n"), exp = want.split("\n");
      const bad = exp.filter((l, i) => l !== got[i]).length;
      logHost(bad === 0 ? `[host] conformance: ${exp.length - 1}/${exp.length - 1} lines match the x86-64 oracle ✓` : `[host] conformance: ${bad} line(s) DIFFER from the oracle`, bad === 0 ? "ok" : "err");
    }
  } else if (st.state === "halted") {
    logHost(`[host] core halted: ${st.reason} at PC ${hex(inst.plugin.getPC())}`, "err");
  }
}

function closeInstance(i: Instance): void {
  host.destroyInstance(i.id);
  expectedFor.delete(i.id); prevRegs.delete(i.id);
  if (active === i) setActive(host.instances()[0] ?? null); else scheduleRender();
}

function load(inst: Instance, data: Uint8Array, label: string, expected?: string): void {
  try {
    const r = inst.loadBinary(data);
    if (expected !== undefined) expectedFor.set(inst.id, expected); else expectedFor.delete(inst.id);
    prevRegs.delete(inst.id); prevMem = null;
    renderConsole(inst);
    logHost(`[host] loaded ${label}: ${r.format.toUpperCase()}, ${r.bytes.toLocaleString()} bytes, entry ${hex(r.entry)}`);
    memAddr = r.entry & ~0xf;
  } catch (e) {
    logHost(`[host] cannot load ${label}: ${(e as Error).message}`, "err");
  }
  scheduleRender();
}

async function ensureActive(): Promise<Instance> { return active ?? (await newInstance()); }

// ── wiring ─────────────────────────────────────────────────────────────────
const sel = $("sample") as HTMLSelectElement;
for (const s of SAMPLES) { const o = document.createElement("option"); o.value = s.id; o.textContent = s.label; sel.appendChild(o); }

$("btn-new").onclick = () => { void newInstance(); };
$("btn-load-sample").onclick = async () => {
  const s = SAMPLES.find((x) => x.id === sel.value)!;
  load(await ensureActive(), s.data, s.id, s.expected);
};
$("btn-run").onclick = () => active?.play();
$("btn-pause").onclick = () => active?.pause();
$("btn-step").onclick = () => active?.step(1);
$("btn-step100").onclick = () => active?.step(100);
$("btn-restart").onclick = () => { if (active) { active.restart(); renderConsole(active); logHost("[host] restarted"); } };

async function loadFile(file: File): Promise<void> {
  if (file.size > 64 * 1024 * 1024) { logHost(`[host] ${file.name} is too large (max 64 MiB)`, "err"); return; }
  const inst = await ensureActive();
  load(inst, new Uint8Array(await file.arrayBuffer()), file.name);
}
($("file") as HTMLInputElement).onchange = async (e) => {
  const f = (e.target as HTMLInputElement).files?.[0];
  if (f) await loadFile(f);
  (e.target as HTMLInputElement).value = "";
};
window.addEventListener("dragover", (e) => { e.preventDefault(); document.body.classList.add("dragover"); });
window.addEventListener("dragleave", (e) => { if (e.relatedTarget === null) document.body.classList.remove("dragover"); });
window.addEventListener("drop", async (e) => {
  e.preventDefault(); document.body.classList.remove("dragover");
  const f = e.dataTransfer?.files[0];
  if (f) await loadFile(f);
});

function showTab(which: "console" | "memory"): void {
  $("pane-console").hidden = which !== "console";
  $("pane-memory").hidden = which !== "memory";
  $("tab-console").setAttribute("aria-selected", String(which === "console"));
  $("tab-memory").setAttribute("aria-selected", String(which === "memory"));
  scheduleRender();
}
$("tab-console").onclick = () => showTab("console");
$("tab-memory").onclick = () => showTab("memory");

const gotoMem = (a: number | null) => { if (a !== null) { memAddr = a; prevMem = null; scheduleRender(); } };
$("mem-go").onclick = () => gotoMem(parseHex(($("memaddr") as HTMLInputElement).value));
($("memaddr") as HTMLInputElement).onkeydown = (e) => { if (e.key === "Enter") gotoMem(parseHex((e.target as HTMLInputElement).value)); };
$("mem-pc").onclick = () => active && gotoMem(active.plugin.getPC());
$("mem-sp").onclick = () => active && gotoMem(Number(active.plugin.getRegisters().SP) - 128);
$("mem-prev").onclick = () => gotoMem(memAddr - 256);
$("mem-next").onclick = () => gotoMem(memAddr + 256);

function addBp(): void {
  const inp = $("bp-addr") as HTMLInputElement;
  const a = parseHex(inp.value);
  if (!active) return;
  if (a === null || a % 4 !== 0) { logHost("[host] breakpoint must be a 4-byte-aligned hex address", "err"); return; }
  try { active.plugin.addBreakpoint(a); inp.value = ""; } catch (e) { logHost(`[host] ${(e as Error).message}`, "err"); }
  scheduleRender();
}
$("bp-add").onclick = addBp;
($("bp-addr") as HTMLInputElement).onkeydown = (e) => { if (e.key === "Enter") addBp(); };

// Boot: one instance with the hello sample loaded (not running).
(async () => {
  try {
    const inst = await newInstance();
    load(inst, hello, "hello");
    logHost("[host] press ▶ Run, or pick another sample / drop your own AArch64 ELF.");
  } catch (e) {
    logHost(`[host] failed to start: ${(e as Error).message}`, "err");
  }
})();

// Test hook (used by the Playwright e2e): read-only access to host state.
(window as unknown as { __silica: unknown }).__silica = { host, get active() { return active; } };
