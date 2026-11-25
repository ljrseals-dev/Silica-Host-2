/**
 * SilicaHost runtime: plugin registry + instances with a time-sliced run loop.
 * The host never blocks the event loop for longer than `sliceMs`; the scheduler is injectable for tests.
 */
import type {
  EmulatorPlugin, InitOptions, LoadOptions, LoadResult, PluginFactory, StopReason,
} from "./plugin";

export type RunState = "idle" | "running" | "paused" | "halted" | "exited";

export interface Scheduler {
  schedule(cb: () => void): unknown;
  cancel(handle: unknown): void;
  now(): number;
}

export const defaultScheduler: Scheduler = {
  schedule: (cb) => setTimeout(cb, 0),
  cancel: (h) => clearTimeout(h as ReturnType<typeof setTimeout>),
  now: () => (typeof performance !== "undefined" ? performance.now() : Date.now()),
};

export interface InstanceEvents {
  /** Run state changed. */
  state: (state: RunState, reason: StopReason | null) => void;
  /** Console text from the guest. */
  console: (text: string) => void;
  /** Fired after every slice and every manual step (cheap to throttle in a UI). */
  tick: () => void;
}

export interface HostOptions {
  scheduler?: Scheduler;
  /** Max wall-clock time spent per slice. Default 8 ms. */
  sliceMs?: number;
  /** Console history kept per instance (characters). Default 256 KiB. */
  consoleLimit?: number;
}

const CONSOLE_LIMIT = 256 * 1024;

export class Instance {
  state: RunState = "idle";
  lastStop: StopReason | null = null;
  consoleText = "";
  /** Instructions per second measured over the most recent run. */
  ips = 0;
  lastLoad: { data: Uint8Array; options?: LoadOptions } | null = null;

  private handle: unknown = null;
  private chunk = 20_000;
  private limit = Infinity;
  private runStart = 0;
  private runInstr = 0;
  private listeners: { [E in keyof InstanceEvents]: Set<InstanceEvents[E]> } = {
    state: new Set(), console: new Set(), tick: new Set(),
  };

  constructor(
    readonly id: number,
    public name: string,
    readonly plugin: EmulatorPlugin,
    private readonly sched: Scheduler,
    private readonly sliceMs: number,
    private readonly consoleLimit: number,
  ) {
    plugin.on("console", (t) => {
      this.consoleText += t;
      if (this.consoleText.length > this.consoleLimit) this.consoleText = this.consoleText.slice(-this.consoleLimit);
      this.listeners.console.forEach((cb) => cb(t));
    });
  }

  on<E extends keyof InstanceEvents>(e: E, cb: InstanceEvents[E]): () => void {
    this.listeners[e].add(cb);
    return () => this.listeners[e].delete(cb);
  }

  private setState(s: RunState, reason: StopReason | null): void {
    this.state = s;
    this.lastStop = reason;
    this.listeners.state.forEach((cb) => cb(s, reason));
  }

  loadBinary(data: ArrayBuffer | Uint8Array, options?: LoadOptions): LoadResult {
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
  restart(): LoadResult | null {
    this.pause();
    this.plugin.reset();
    this.consoleText = "";
    const r = this.lastLoad ? this.plugin.loadBinary(this.lastLoad.data, this.lastLoad.options) : null;
    this.setState(r ? "paused" : "idle", null);
    this.listeners.tick.forEach((cb) => cb());
    return r;
  }

  /** Start/continue running. `limit` caps the instructions executed by this call. */
  play(limit = Infinity): void {
    if (this.state === "running") return;
    if (this.state === "halted" || this.state === "exited" || this.state === "idle") return;
    this.limit = limit;
    this.runStart = this.sched.now();
    this.runInstr = 0;
    this.setState("running", null);
    this.handle = this.sched.schedule(() => this.slice());
  }

  pause(): void {
    if (this.handle !== null) { this.sched.cancel(this.handle); this.handle = null; }
    if (this.state === "running") this.setState("paused", null);
  }

  step(n = 1): StopReason {
    if (this.state === "halted" || this.state === "exited" || this.state === "idle") return this.lastStop ?? "halted";
    this.pause();
    const r = this.plugin.step(n);
    this.afterStop(r);
    this.listeners.tick.forEach((cb) => cb());
    return r;
  }

  private afterStop(r: StopReason): void {
    if (r === "halted") this.setState("halted", r);
    else if (r === "exited") this.setState("exited", r);
    else this.setState("paused", r);
  }

  private slice(): void {
    this.handle = null;
    if (this.state !== "running") return;
    const t0 = this.sched.now();
    let stop: StopReason = "budget";
    // Run in calibrated chunks until the slice budget is used up.
    while (stop === "budget" && this.sched.now() - t0 < this.sliceMs && this.runInstr < this.limit) {
      const n = Math.min(this.chunk, this.limit - this.runInstr);
      const c0 = this.sched.now();
      stop = this.plugin.run(n);
      const dt = this.sched.now() - c0;
      this.runInstr += n;
      // Aim for ~1/4 of a slice per chunk so the deadline is respected without hammering the clock.
      if (dt > 0) this.chunk = Math.max(1000, Math.min(50_000_000, Math.floor((n * (this.sliceMs / 4)) / dt)));
    }
    const elapsed = (this.sched.now() - this.runStart) / 1000;
    if (elapsed > 0) this.ips = this.runInstr / elapsed;
    this.listeners.tick.forEach((cb) => cb());
    if (stop !== "budget") { this.afterStop(stop); return; }
    if (this.runInstr >= this.limit) { this.setState("paused", "budget"); return; }
    this.handle = this.sched.schedule(() => this.slice());
  }
}

export class SilicaHost {
  private factories = new Map<string, PluginFactory>();
  private insts = new Map<number, Instance>();
  private nextId = 0;
  private readonly sched: Scheduler;
  private readonly sliceMs: number;
  private readonly consoleLimit: number;

  constructor(opts: HostOptions = {}) {
    this.sched = opts.scheduler ?? defaultScheduler;
    this.sliceMs = opts.sliceMs ?? 8;
    this.consoleLimit = opts.consoleLimit ?? CONSOLE_LIMIT;
  }

  registerPlugin(factory: PluginFactory): void {
    if (this.factories.has(factory.id)) throw new Error(`plugin "${factory.id}" already registered`);
    this.factories.set(factory.id, factory);
  }
  plugins(): PluginFactory[] { return [...this.factories.values()]; }

  async createInstance(pluginId: string, opts: { name?: string; init?: InitOptions } = {}): Promise<Instance> {
    const f = this.factories.get(pluginId);
    if (!f) throw new Error(`unknown plugin "${pluginId}"`);
    const plugin = f.create();
    await plugin.load(opts.init);
    const id = this.nextId++;
    const inst = new Instance(id, opts.name ?? `${f.name} #${id}`, plugin, this.sched, this.sliceMs, this.consoleLimit);
    this.insts.set(id, inst);
    return inst;
  }

  getInstance(id: number): Instance | undefined { return this.insts.get(id); }
  instances(): Instance[] { return [...this.insts.values()]; }

  destroyInstance(id: number): void {
    const i = this.insts.get(id);
    if (!i) return;
    i.pause();
    i.plugin.destroy();
    this.insts.delete(id);
  }
}
