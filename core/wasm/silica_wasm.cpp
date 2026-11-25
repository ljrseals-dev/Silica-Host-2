// WebAssembly entry point for the Silica AArch64 functional core.
// Freestanding wasm32: no libc, no exceptions. One Cpu per wasm instance
// (multi-instance hosting = multiple WebAssembly.Instance objects, each isolated).
#include "silica/cpu.hpp"
#include "silica_abi.h"

using namespace silica;

inline void* operator new(size_t, void* p) noexcept { return p; }  // placement new (no <new> in freestanding)

// ── allocator: bump allocator over memory.grow (guest RAM is allocated once) ──
extern "C" unsigned char __heap_base;
static size_t g_heap_top = 0;
static constexpr size_t kPage = 65536;

static void* bump_alloc(size_t n) {
  if (g_heap_top == 0) g_heap_top = (reinterpret_cast<size_t>(&__heap_base) + 15) & ~size_t(15);
  n = (n + 15) & ~size_t(15);
  const size_t need_end = g_heap_top + n;
  if (need_end < g_heap_top) return nullptr;  // overflow
  const size_t have = __builtin_wasm_memory_size(0) * kPage;
  if (need_end > have) {
    const size_t pages = (need_end - have + kPage - 1) / kPage;
    if (__builtin_wasm_memory_grow(0, pages) == static_cast<size_t>(-1)) return nullptr;
  }
  void* p = reinterpret_cast<void*>(g_heap_top);
  g_heap_top = need_end;
  return p;
}
void* operator new(size_t n) {
  void* p = bump_alloc(n);
  if (!p) __builtin_trap();  // cannot throw; silica_init pre-checks sizes so this is unreachable in practice
  return p;
}
void operator delete(void*) noexcept {}
void operator delete(void*, size_t) noexcept {}

// ── state ──
alignas(16) static unsigned char g_cpu_storage[sizeof(Cpu)];
static Cpu* g_cpu = nullptr;
static uint64_t g_state[SILICA_STATE_WORDS];
static uint8_t g_console[SILICA_CONSOLE_CAP];
static uint32_t g_console_len = 0;
static uint64_t g_bps[SILICA_MAX_BREAKPOINTS];
static uint32_t g_nbp = 0;
static uint64_t g_skip_pc = ~0ull;  // PC we last stopped at on a breakpoint: stepping off it must make progress
static bool g_exited = false;
static uint64_t g_exit_code = 0;
static constexpr uint64_t kMaxRam = 1ull << 30;  // 1 GiB (wasm32 limit is 4 GiB; keep headroom)

static bool is_bp(uint64_t pc) {
  for (uint32_t i = 0; i < g_nbp; ++i) if (g_bps[i] == pc) return true;
  return false;
}

// Service BRK #SILICA_HOSTCALL_IMM. Returns true if the BRK was a hostcall (halt cleared, pc advanced).
static bool service_hostcall() {
  Cpu& c = *g_cpu;
  const uint64_t pc = c.regs.pc;
  const uint32_t word = c.mem.read<uint32_t>(pc);
  if (c.mem.fault()) { c.mem.clear_fault(); return false; }
  if (((word >> 5) & 0xFFFFu) != SILICA_HOSTCALL_IMM) return false;
  const uint64_t op = c.regs.x[0], a1 = c.regs.x[1], a2 = c.regs.x[2];
  uint64_t ret = ~0ull;
  switch (op) {
    case 0: g_exited = true; g_exit_code = a1; ret = 0; break;
    case 1:
      if (g_console_len < SILICA_CONSOLE_CAP) { g_console[g_console_len++] = static_cast<uint8_t>(a1); ret = 1; }
      else ret = 0;
      break;
    case 2: {
      const uint64_t ram = c.mem.size();
      if (a1 > ram || a2 > ram - a1) break;  // bad guest pointer -> -1
      const uint64_t room = SILICA_CONSOLE_CAP - g_console_len;
      const uint64_t n = a2 < room ? a2 : room;
      const uint8_t* src = c.mem.data() + a1;
      for (uint64_t i = 0; i < n; ++i) g_console[g_console_len + i] = src[i];
      g_console_len += static_cast<uint32_t>(n);
      ret = n;
      break;
    }
    default: break;
  }
  c.regs.wr(0, ret);
  c.regs.pc = pc + 4;
  c.halt = HaltReason::None;
  return true;
}

extern "C" {

__attribute__((export_name("silica_abi_version"))) uint32_t silica_abi_version() { return SILICA_ABI_VERSION; }

// 0 ok; -1 already initialised; -2 bad size; -3 out of memory.
__attribute__((export_name("silica_init"))) int32_t silica_init(uint32_t ram_bytes) {
  if (g_cpu) return -1;
  if (ram_bytes < 4096 || ram_bytes > kMaxRam || (ram_bytes & 15)) return -2;
  // Pre-flight the grow so operator new can never trap: reserve ram + slack, then rewind.
  const size_t save = g_heap_top;
  void* probe = bump_alloc(ram_bytes);
  if (!probe) return -3;
  g_heap_top = save;
  g_cpu = new (g_cpu_storage) Cpu(ram_bytes);
  return 0;
}

__attribute__((export_name("silica_ram_ptr"))) uint32_t silica_ram_ptr() {
  return g_cpu ? static_cast<uint32_t>(reinterpret_cast<size_t>(g_cpu->mem.data())) : 0;
}
__attribute__((export_name("silica_ram_size"))) uint32_t silica_ram_size() {
  return g_cpu ? static_cast<uint32_t>(g_cpu->mem.size()) : 0;
}

__attribute__((export_name("silica_reset"))) void silica_reset(uint32_t clear_ram) {
  if (!g_cpu) return;
  g_cpu->regs = RegisterFile{};
  g_cpu->steps = 0;
  g_cpu->halt = HaltReason::None;
  g_cpu->mem.clear_fault();
  if (clear_ram) { uint8_t* p = g_cpu->mem.data(); for (size_t i = 0, n = g_cpu->mem.size(); i < n; ++i) p[i] = 0; }
  g_console_len = 0; g_exited = false; g_exit_code = 0; g_skip_pc = ~0ull;
}

__attribute__((export_name("silica_set_x"))) void silica_set_x(uint32_t idx, uint64_t v) { if (g_cpu && idx < 31) g_cpu->regs.wr(idx, v); }
__attribute__((export_name("silica_set_sp"))) void silica_set_sp(uint64_t v) { if (g_cpu) g_cpu->regs.sp = v; }
__attribute__((export_name("silica_set_pc"))) void silica_set_pc(uint64_t v) {
  if (!g_cpu) return;
  g_cpu->regs.pc = v;
  g_cpu->halt = HaltReason::None;  // re-pointing the PC resumes a halted core
  g_cpu->mem.clear_fault();
  g_exited = false;
  g_skip_pc = ~0ull;
}
__attribute__((export_name("silica_set_nzcv"))) void silica_set_nzcv(uint32_t f) { if (g_cpu) g_cpu->regs.pstate.set_nzcv(static_cast<uint8_t>(f)); }

__attribute__((export_name("silica_bp_set"))) int32_t silica_bp_set(uint64_t addr) {
  if (is_bp(addr)) return 0;
  if (g_nbp >= SILICA_MAX_BREAKPOINTS) return -1;
  g_bps[g_nbp++] = addr;
  return 0;
}
__attribute__((export_name("silica_bp_clear"))) void silica_bp_clear(uint64_t addr) {
  for (uint32_t i = 0; i < g_nbp; ++i) if (g_bps[i] == addr) { g_bps[i] = g_bps[--g_nbp]; return; }
}
__attribute__((export_name("silica_bp_clear_all"))) void silica_bp_clear_all() { g_nbp = 0; }

// Run up to max_steps instructions. A breakpoint fires *before* the instruction at that PC executes.
// Resuming from the PC where a breakpoint last fired steps over it; starting anywhere else on a
// breakpoint (fresh load, new PC) stops immediately. `force_first` (single-step) always executes.
static uint32_t run_impl(uint64_t max_steps, bool force_first) {
  if (!g_cpu) return SILICA_STOP_HALT;
  Cpu& c = *g_cpu;
  for (uint64_t i = 0; i < max_steps; ++i) {
    if (g_exited) return SILICA_STOP_EXIT;
    if (c.halt != HaltReason::None) return SILICA_STOP_HALT;
    if (g_nbp && is_bp(c.regs.pc) && !(i == 0 && (force_first || c.regs.pc == g_skip_pc))) {
      g_skip_pc = c.regs.pc;
      return SILICA_STOP_BREAKPOINT;
    }
    c.step();
    if (c.halt == HaltReason::Brk && service_hostcall()) {
      if (g_exited) return SILICA_STOP_EXIT;
      if (g_console_len > SILICA_CONSOLE_CAP - 256) return SILICA_STOP_CONSOLE;
    }
  }
  if (g_exited) return SILICA_STOP_EXIT;
  return c.halt != HaltReason::None ? SILICA_STOP_HALT : SILICA_STOP_BUDGET;
}

__attribute__((export_name("silica_run"))) uint32_t silica_run(uint64_t max_steps) { return run_impl(max_steps, false); }
// A single step always executes one instruction, even when sitting on a breakpoint.
__attribute__((export_name("silica_step"))) uint32_t silica_step() { return run_impl(1, true); }

__attribute__((export_name("silica_state_ptr"))) uint32_t silica_state_ptr() {
  if (!g_cpu) return 0;
  const RegisterFile& r = g_cpu->regs;
  for (int i = 0; i < 31; ++i) g_state[SILICA_ST_X0 + i] = r.x[i];
  g_state[SILICA_ST_SP] = r.sp;
  g_state[SILICA_ST_PC] = r.pc;
  g_state[SILICA_ST_NZCV] = r.pstate.nzcv();
  g_state[SILICA_ST_EL] = r.pstate.el;
  g_state[SILICA_ST_SPSEL] = r.pstate.spsel;
  g_state[SILICA_ST_STEPS] = g_cpu->steps;
  g_state[SILICA_ST_HALT] = static_cast<uint64_t>(g_cpu->halt);
  g_state[SILICA_ST_EXITED] = g_exited ? 1 : 0;
  g_state[SILICA_ST_EXITCODE] = g_exit_code;
  for (int i = 0; i < SYS_COUNT; ++i) g_state[SILICA_ST_SYS0 + i] = r.sys[i];
  for (int i = 0; i < 32; ++i) { g_state[SILICA_ST_V0 + 2 * i] = r.v[i][0]; g_state[SILICA_ST_V0 + 2 * i + 1] = r.v[i][1]; }
  return static_cast<uint32_t>(reinterpret_cast<size_t>(g_state));
}

__attribute__((export_name("silica_console_ptr"))) uint32_t silica_console_ptr() { return static_cast<uint32_t>(reinterpret_cast<size_t>(g_console)); }
__attribute__((export_name("silica_console_len"))) uint32_t silica_console_len() { return g_console_len; }
__attribute__((export_name("silica_console_clear"))) void silica_console_clear() { g_console_len = 0; }

}  // extern "C"
