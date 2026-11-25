var __toBinary = /* @__PURE__ */ (() => {
  var table = new Uint8Array(128);
  for (var i = 0; i < 64; i++) table[i < 26 ? i + 65 : i < 52 ? i + 71 : i < 62 ? i - 4 : i * 4 - 205] = i;
  return (base64) => {
    var n = base64.length, bytes = new Uint8Array((n - (base64[n - 1] == "=") - (base64[n - 2] == "=")) * 3 / 4 | 0);
    for (var i2 = 0, j = 0; i2 < n; ) {
      var c0 = table[base64.charCodeAt(i2++)], c1 = table[base64.charCodeAt(i2++)];
      var c2 = table[base64.charCodeAt(i2++)], c3 = table[base64.charCodeAt(i2++)];
      bytes[j++] = c0 << 2 | c1 >> 4;
      bytes[j++] = c1 << 4 | c2 >> 2;
      bytes[j++] = c2 << 6 | c3;
    }
    return bytes;
  };
})();

// src/host.ts
var defaultScheduler = {
  schedule: (cb) => setTimeout(cb, 0),
  cancel: (h) => clearTimeout(h),
  now: () => typeof performance !== "undefined" ? performance.now() : Date.now()
};
var CONSOLE_LIMIT = 256 * 1024;
var Instance = class {
  constructor(id, name, plugin, sched, sliceMs, consoleLimit) {
    this.id = id;
    this.name = name;
    this.plugin = plugin;
    this.sched = sched;
    this.sliceMs = sliceMs;
    this.consoleLimit = consoleLimit;
    plugin.on("console", (t) => {
      this.consoleText += t;
      if (this.consoleText.length > this.consoleLimit) this.consoleText = this.consoleText.slice(-this.consoleLimit);
      this.listeners.console.forEach((cb) => cb(t));
    });
  }
  state = "idle";
  lastStop = null;
  consoleText = "";
  /** Instructions per second measured over the most recent run. */
  ips = 0;
  lastLoad = null;
  handle = null;
  chunk = 2e4;
  limit = Infinity;
  runStart = 0;
  runInstr = 0;
  listeners = {
    state: /* @__PURE__ */ new Set(),
    console: /* @__PURE__ */ new Set(),
    tick: /* @__PURE__ */ new Set()
  };
  on(e, cb) {
    this.listeners[e].add(cb);
    return () => this.listeners[e].delete(cb);
  }
  setState(s, reason) {
    this.state = s;
    this.lastStop = reason;
    this.listeners.state.forEach((cb) => cb(s, reason));
  }
  loadBinary(data, options) {
    this.pause();
    const bytes = (data instanceof Uint8Array ? data : new Uint8Array(data)).slice();
    const r = this.plugin.loadBinary(bytes, options);
    this.lastLoad = { data: bytes, options };
    this.consoleText = "";
    this.setState("paused", null);
    this.listeners.tick.forEach((cb) => cb());
    return r;
  }
  /** Reset the core and reload the last image. */
  restart() {
    this.pause();
    this.plugin.reset();
    this.consoleText = "";
    const r = this.lastLoad ? this.plugin.loadBinary(this.lastLoad.data, this.lastLoad.options) : null;
    this.setState(r ? "paused" : "idle", null);
    this.listeners.tick.forEach((cb) => cb());
    return r;
  }
  /** Start/continue running. `limit` caps the instructions executed by this call. */
  play(limit = Infinity) {
    if (this.state === "running") return;
    if (this.state === "halted" || this.state === "exited" || this.state === "idle") return;
    this.limit = limit;
    this.runStart = this.sched.now();
    this.runInstr = 0;
    this.setState("running", null);
    this.handle = this.sched.schedule(() => this.slice());
  }
  pause() {
    if (this.handle !== null) {
      this.sched.cancel(this.handle);
      this.handle = null;
    }
    if (this.state === "running") this.setState("paused", null);
  }
  step(n = 1) {
    if (this.state === "halted" || this.state === "exited" || this.state === "idle") return this.lastStop ?? "halted";
    this.pause();
    const r = this.plugin.step(n);
    this.afterStop(r);
    this.listeners.tick.forEach((cb) => cb());
    return r;
  }
  afterStop(r) {
    if (r === "halted") this.setState("halted", r);
    else if (r === "exited") this.setState("exited", r);
    else this.setState("paused", r);
  }
  slice() {
    this.handle = null;
    if (this.state !== "running") return;
    const t0 = this.sched.now();
    let stop = "budget";
    while (stop === "budget" && this.sched.now() - t0 < this.sliceMs && this.runInstr < this.limit) {
      const n = Math.min(this.chunk, this.limit - this.runInstr);
      const c0 = this.sched.now();
      stop = this.plugin.run(n);
      const dt = this.sched.now() - c0;
      this.runInstr += n;
      if (dt > 0) this.chunk = Math.max(1e3, Math.min(5e7, Math.floor(n * (this.sliceMs / 4) / dt)));
    }
    const elapsed = (this.sched.now() - this.runStart) / 1e3;
    if (elapsed > 0) this.ips = this.runInstr / elapsed;
    this.listeners.tick.forEach((cb) => cb());
    if (stop !== "budget") {
      this.afterStop(stop);
      return;
    }
    if (this.runInstr >= this.limit) {
      this.setState("paused", "budget");
      return;
    }
    this.handle = this.sched.schedule(() => this.slice());
  }
};
var SilicaHost = class {
  factories = /* @__PURE__ */ new Map();
  insts = /* @__PURE__ */ new Map();
  nextId = 0;
  sched;
  sliceMs;
  consoleLimit;
  constructor(opts = {}) {
    this.sched = opts.scheduler ?? defaultScheduler;
    this.sliceMs = opts.sliceMs ?? 8;
    this.consoleLimit = opts.consoleLimit ?? CONSOLE_LIMIT;
  }
  registerPlugin(factory) {
    if (this.factories.has(factory.id)) throw new Error(`plugin "${factory.id}" already registered`);
    this.factories.set(factory.id, factory);
  }
  plugins() {
    return [...this.factories.values()];
  }
  async createInstance(pluginId, opts = {}) {
    const f = this.factories.get(pluginId);
    if (!f) throw new Error(`unknown plugin "${pluginId}"`);
    const plugin = f.create();
    await plugin.load(opts.init);
    const id = this.nextId++;
    const inst = new Instance(id, opts.name ?? `${f.name} #${id}`, plugin, this.sched, this.sliceMs, this.consoleLimit);
    this.insts.set(id, inst);
    return inst;
  }
  getInstance(id) {
    return this.insts.get(id);
  }
  instances() {
    return [...this.insts.values()];
  }
  destroyInstance(id) {
    const i = this.insts.get(id);
    if (!i) return;
    i.pause();
    i.plugin.destroy();
    this.insts.delete(id);
  }
};

// src/elf.ts
var ElfError = class extends Error {
  constructor(message) {
    super(message);
    this.name = "ElfError";
  }
};
var EM_AARCH64 = 183;
var PT_LOAD = 1;
var MAX_PHNUM = 64;
function isElf(b) {
  return b.length >= 4 && b[0] === 127 && b[1] === 69 && b[2] === 76 && b[3] === 70;
}
function safeNumber(v, what) {
  if (v > BigInt(Number.MAX_SAFE_INTEGER)) throw new ElfError(`${what} too large`);
  return Number(v);
}
function parseElf(bytes, ramSize) {
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
  const segments = [];
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
        `segment 0x${vaddr.toString(16)}..0x${(vaddr + memsz).toString(16)} does not fit in ${ramSize} bytes of RAM`
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

// src/abi.ts
var ABI_VERSION = 1;
var ST = {
  X0: 0,
  SP: 31,
  PC: 32,
  NZCV: 33,
  EL: 34,
  SPSEL: 35,
  STEPS: 36,
  HALT: 37,
  EXITED: 38,
  EXITCODE: 39,
  SYS0: 40,
  V0: 48,
  WORDS: 112
};
var STOP = { BUDGET: 0, HALT: 1, BREAKPOINT: 2, CONSOLE: 3, EXIT: 4 };
var CONSOLE_CAP = 4096;
var MAX_BREAKPOINTS = 64;
var HALT_REASONS = [
  "none",
  "executed an all-zero word",
  "BRK instruction",
  "invalid or unmodelled instruction",
  "memory fault (out-of-range access or misaligned PC)"
];

// build/silica-core.wasm
var silica_core_default = __toBinary("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4JOAgABBAEEAKQPoiICAADcD6JOAgABBAEEAKQPwiICAADcD8JOAgABBAEEAKQP4iICAADcD+JOAgABBAEEAKQOAiYCAADcDgJSAgABBAEEAKQOIiYCAADcDiJSAgABBAEEAKQOQiYCAADcDkJSAgABBAEEAKQOYiYCAADcDmJSAgABBAEEAKQOgiYCAADcDoJSAgABBAEEAKQOoiYCAADcDqJSAgABBAEEAKQOwiYCAADcDsJSAgABBAEEAKQO4iYCAADcDuJSAgABBAEEAKQPAiYCAADcDwJSAgABBAEEAKQPIiYCAADcDyJSAgABBAEEAKQPQiYCAADcD0JSAgABBAEEAKQPYiYCAADcD2JSAgABBAEEAKQPgiYCAADcD4JSAgABBAEEAKQPoiYCAADcD6JSAgABBAEEAKQPwiYCAADcD8JSAgABBAEEAKQP4iYCAADcD+JSAgABBAEEAKQOAioCAADcDgJWAgABBAEEAKQOIioCAADcDiJWAgABBAEEAKQOQioCAADcDkJWAgABBAEEAKQOgioCAADcDmJWAgABBAEEAKQOojoCAADcDoJWAgABBAEEALQCxjoCAAEECdEEALQCwjoCAAEEDdHJBAC0Aso6AgABBAXRyQQAtALOOgIAAcq1C/wGDNwOolYCAAEEAQQAxALSOgIAANwOwlYCAAEEAQQAxALWOgIAANwO4lYCAAEEAQQApA/COgIAANwPAlYCAAEEAQQAxAPiOgIAANwPIlYCAAEEAQQAxAISPgIAANwPQlYCAAEEAQQApA4iPgIAANwPYlYCAAEEAQQApA7iOgIAANwPglYCAAEEAQQApA8COgIAANwPolYCAAEEAQQApA8iOgIAANwPwlYCAAEEAQQApA9COgIAANwP4lYCAAANAIABBoJaAgABqIAEgAGoiAkGIAmopAwA3AwAgAEGoloCAAGogAkGQAmopAwA3AwAgAEGwloCAAGogAkGYAmopAwA3AwAgAEG4loCAAGogAkGgAmopAwA3AwAgAEEgaiIAQYAERw0AC0Ggk4CAACEACyAACwwAQgFBARCMgICAAAsLDwEAQYAICwj//////////w==");

// src/cores/silica-wasm.ts
var DEFAULT_RAM = 4 * 1024 * 1024;
var MIN_RAM = 64 * 1024;
var MAX_RAM = 256 * 1024 * 1024;
var DEFAULT_BASE = 4096;
var compiled = null;
function compileOnce() {
  return compiled ??= WebAssembly.compile(silica_core_default);
}
var GPR_NAMES = Array.from({ length: 31 }, (_, i) => `X${i}`);
var SilicaWasmCore = class {
  id = "silica-aarch64-wasm";
  name = "Silica AArch64 (WebAssembly)";
  version = "1.0.0";
  supportedArchs = ["aarch64"];
  capabilities = ["functional-only", "breakpoints", "console", "elf"];
  x = null;
  ramBase = 0;
  ramSize = 0;
  bps = /* @__PURE__ */ new Set();
  decoder = new TextDecoder("utf-8");
  listeners = {};
  destroyed = false;
  lastStatus = "ready";
  async load(options = {}) {
    if (this.x) throw new Error("core already loaded");
    const ram = options.ramBytes ?? DEFAULT_RAM;
    if (!Number.isInteger(ram) || ram < MIN_RAM || ram > MAX_RAM || ram % 16 !== 0) {
      throw new RangeError(`ramBytes must be a multiple of 16 between ${MIN_RAM} and ${MAX_RAM}`);
    }
    const module = await compileOnce();
    const x = new WebAssembly.Instance(module, {}).exports;
    if (x.silica_abi_version() !== ABI_VERSION) throw new Error("wasm core ABI version mismatch");
    const rc = x.silica_init(ram);
    if (rc !== 0) throw new Error(`silica_init failed (${rc}): could not allocate ${ram} bytes of guest RAM`);
    this.x = x;
    this.ramBase = x.silica_ram_ptr();
    this.ramSize = x.silica_ram_size();
  }
  core() {
    if (this.destroyed) throw new Error("core was destroyed");
    if (!this.x) throw new Error("core not loaded: call load() first");
    return this.x;
  }
  mem8() {
    return new Uint8Array(this.core().memory.buffer);
  }
  // ── loading ──────────────────────────────────────────────────────────────
  loadBinary(data, options = {}) {
    const x = this.core();
    const bytes = data instanceof Uint8Array ? data : new Uint8Array(data);
    const mem = this.mem8();
    let result;
    if (isElf(bytes)) {
      const img = parseElf(bytes, this.ramSize);
      x.silica_reset(1);
      for (const s of img.segments) {
        mem.set(bytes.subarray(s.offset, s.offset + s.filesz), this.ramBase + s.vaddr);
      }
      result = {
        format: "elf",
        entry: options.entry ?? img.entry,
        bytes: bytes.length,
        segments: img.segments.map((s) => ({ addr: s.vaddr, size: s.memsz }))
      };
    } else {
      const base = options.base ?? DEFAULT_BASE;
      if (!Number.isInteger(base) || base < 0 || base % 4 !== 0) throw new RangeError("base must be a non-negative multiple of 4");
      if (bytes.length === 0) throw new ElfError("empty binary");
      if (base + bytes.length > this.ramSize) {
        throw new RangeError(`binary of ${bytes.length} bytes at 0x${base.toString(16)} does not fit in ${this.ramSize} bytes of RAM`);
      }
      x.silica_reset(1);
      mem.set(bytes, this.ramBase + base);
      result = { format: "raw", entry: options.entry ?? base, bytes: bytes.length, segments: [{ addr: base, size: bytes.length }] };
    }
    if (result.entry % 4 !== 0 || result.entry >= this.ramSize) throw new RangeError("entry point outside RAM or misaligned");
    const sp = options.stackPointer ?? this.ramSize - 16;
    if (sp < 0 || sp > this.ramSize) throw new RangeError("stack pointer outside RAM");
    x.silica_set_sp(BigInt(sp));
    x.silica_set_pc(BigInt(result.entry));
    this.lastStatus = "ready";
    return result;
  }
  reset() {
    this.core().silica_reset(1);
    this.lastStatus = "ready";
  }
  destroy() {
    this.destroyed = true;
    this.x = null;
    this.listeners = {};
    this.bps.clear();
  }
  // ── execution ────────────────────────────────────────────────────────────
  step(n = 1) {
    if (!Number.isInteger(n) || n < 1) throw new RangeError("n must be a positive integer");
    const x = this.core();
    const stop = x.silica_step();
    this.drainConsole();
    if (stop === STOP.EXIT) return this.onExit();
    if (stop === STOP.HALT) return this.onHalt();
    return n === 1 ? "budget" : this.run(n - 1);
  }
  run(maxInstructions) {
    const x = this.core();
    if (!Number.isFinite(maxInstructions) || maxInstructions < 0) throw new RangeError("maxInstructions must be >= 0");
    let remaining = Math.floor(maxInstructions);
    while (remaining > 0) {
      const chunk = Math.min(remaining, 1073741823);
      const before = this.stepsNow();
      const stop = x.silica_run(BigInt(chunk));
      const executed = Number(this.stepsNow() - before);
      remaining -= executed;
      this.drainConsole();
      if (stop === STOP.BREAKPOINT) {
        this.emit("breakpoint", this.getPC());
        return "breakpoint";
      }
      if (stop === STOP.EXIT) return this.onExit();
      if (stop === STOP.HALT) return this.onHalt();
      if (executed === 0) break;
    }
    return "budget";
  }
  stepsNow() {
    return this.snapshot()[ST.STEPS];
  }
  onExit() {
    if (this.lastStatus !== "exited") {
      this.lastStatus = "exited";
      this.emit("exit", this.snapshot()[ST.EXITCODE]);
    }
    return "exited";
  }
  onHalt() {
    if (this.lastStatus !== "halted") {
      this.lastStatus = "halted";
      this.emit("halt", this.getStatus().reason ?? "halted");
    }
    return "halted";
  }
  drainConsole() {
    const x = this.core();
    const n = x.silica_console_len();
    if (n === 0) return;
    if (n > CONSOLE_CAP) throw new Error("console buffer corrupted");
    const chunk = new Uint8Array(x.memory.buffer, x.silica_console_ptr(), n).slice();
    x.silica_console_clear();
    const text = this.decoder.decode(chunk, { stream: true });
    if (text) this.emit("console", text);
  }
  // ── state ────────────────────────────────────────────────────────────────
  snapshot() {
    const x = this.core();
    return new BigUint64Array(x.memory.buffer, x.silica_state_ptr(), ST.WORDS);
  }
  getStatus() {
    const s = this.snapshot();
    const instructions = s[ST.STEPS];
    if (s[ST.EXITED] === 1n) return { state: "exited", exitCode: s[ST.EXITCODE], instructions };
    const h = Number(s[ST.HALT]);
    if (h !== 0) return { state: "halted", reason: HALT_REASONS[h] ?? `halt ${h}`, instructions };
    return { state: "ready", instructions };
  }
  getRegisters() {
    const s = this.snapshot();
    const out = {};
    for (let i = 0; i < 31; i++) out[GPR_NAMES[i]] = s[ST.X0 + i];
    out.SP = s[ST.SP];
    out.PC = s[ST.PC];
    out.NZCV = s[ST.NZCV];
    return out;
  }
  setRegister(name, value) {
    const x = this.core();
    const v = BigInt.asUintN(64, value);
    const key = name.toUpperCase();
    const m = /^X(\d+)$/.exec(key);
    if (m && Number(m[1]) < 31) x.silica_set_x(Number(m[1]), v);
    else if (key === "SP") x.silica_set_sp(v);
    else if (key === "PC") {
      x.silica_set_pc(v);
      this.lastStatus = "ready";
    } else if (key === "NZCV") x.silica_set_nzcv(Number(v & 0xfn));
    else throw new RangeError(`unknown register ${name}`);
  }
  getPC() {
    return Number(this.snapshot()[ST.PC]);
  }
  // ── memory ───────────────────────────────────────────────────────────────
  getMemoryMap() {
    return [{ name: "RAM", base: 0, size: this.ramSize, writable: true }];
  }
  check(addr, len) {
    if (!Number.isInteger(addr) || !Number.isInteger(len) || addr < 0 || len < 0 || addr + len > this.ramSize) {
      throw new RangeError(`memory range 0x${addr.toString(16)}+${len} is outside RAM (${this.ramSize} bytes)`);
    }
  }
  getMemory(addr, len) {
    this.check(addr, len);
    return this.mem8().slice(this.ramBase + addr, this.ramBase + addr + len);
  }
  setMemory(addr, data) {
    this.check(addr, data.length);
    this.mem8().set(data, this.ramBase + addr);
  }
  // ── breakpoints ──────────────────────────────────────────────────────────
  addBreakpoint(addr) {
    const x = this.core();
    if (!Number.isInteger(addr) || addr < 0 || addr % 4 !== 0) throw new RangeError("breakpoint address must be a non-negative multiple of 4");
    if (this.bps.has(addr)) return;
    if (this.bps.size >= MAX_BREAKPOINTS) throw new RangeError(`at most ${MAX_BREAKPOINTS} breakpoints`);
    x.silica_bp_set(BigInt(addr));
    this.bps.add(addr);
  }
  removeBreakpoint(addr) {
    if (this.bps.delete(addr)) this.core().silica_bp_clear(BigInt(addr));
  }
  clearBreakpoints() {
    this.core().silica_bp_clear_all();
    this.bps.clear();
  }
  getBreakpoints() {
    return [...this.bps].sort((a, b) => a - b);
  }
  // ── events ───────────────────────────────────────────────────────────────
  on(event, cb) {
    (this.listeners[event] ??= /* @__PURE__ */ new Set()).add(cb);
  }
  off(event, cb) {
    this.listeners[event]?.delete(cb);
  }
  emit(event, ...args) {
    for (const cb of this.listeners[event] ?? []) cb(...args);
  }
};
var silicaWasmFactory = {
  id: "silica-aarch64-wasm",
  name: "Silica AArch64 (WebAssembly)",
  create: () => new SilicaWasmCore()
};
export {
  DEFAULT_RAM,
  ElfError,
  Instance,
  MAX_RAM,
  MIN_RAM,
  SilicaHost,
  SilicaWasmCore,
  defaultScheduler,
  isElf,
  parseElf,
  silicaWasmFactory
};
