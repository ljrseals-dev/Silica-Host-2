// Dependency-free micro-benchmarks (Criterion is Rust-only). Reports best-of-N insn/s.
#include <chrono>
#include <cstdio>
#include <vector>
#include "silica/asm.hpp"
#include "silica/cpu.hpp"

using namespace silica;
namespace e = silica::enc;
using clk = std::chrono::steady_clock;

template <class T> static inline void escape(T* p) { asm volatile("" : : "g"(p) : "memory"); }
static inline void clobber() { asm volatile("" : : : "memory"); }

template <class F>
static double best_rate(F&& f, double insns_per_call, int reps = 7) {
  double best = 0;
  for (int r = 0; r < reps; ++r) {
    const auto t0 = clk::now();
    f();
    const double s = std::chrono::duration<double>(clk::now() - t0).count();
    const double rate = insns_per_call / s;
    if (rate > best) best = rate;
  }
  return best;
}

int main() {
  std::printf("silica C++20 bench (best of 7). Sandbox/VM numbers are noisy; see README for method.\n");

  // 1. decode throughput over a varied instruction mix
  {
    std::vector<uint32_t> w;
    for (uint32_t k = 0; k < 64; ++k) {
      w.push_back(e::add_imm(k % 8, (k + 1) % 8, k));
      w.push_back(e::add_reg(k % 8, (k + 1) % 8, (k + 2) % 8));
      w.push_back(e::ldst_uimm(3, 1, k, 1, k % 8));
      w.push_back(e::ldst_uimm(3, 0, k, 1, k % 8));
      w.push_back(e::subs_imm(k % 8, 1, k));
      w.push_back(e::b_cond(e::COND_NE, -2));
      w.push_back(e::madd(1, 0, 2, 3, 1, 0));
      w.push_back(e::movz(1, k, 0, k % 8));
    }
    constexpr int ITER = 20000;
    Instruction insn;
    const double rate = best_rate([&] {
      for (int it = 0; it < ITER; ++it)
        for (uint32_t x : w) { decode_into(x, insn); escape(&insn); }
    }, double(ITER) * w.size());
    std::printf("decode_mixed           %8.1f M insn/s\n", rate / 1e6);
  }

  auto run_loop = [](const char* name, std::vector<uint32_t> prog, uint64_t iters, double insns_per_iter,
                     auto setup) {
    Cpu cpu;
    prog.push_back(0);
    cpu.load_program(0x1000, prog);
    const double rate = best_rate([&] {
      cpu.halt = HaltReason::None; cpu.regs.pc = 0x1000; cpu.regs.x[1] = iters;
      setup(cpu);
      clobber();
      cpu.run(~0ull);
      escape(&cpu.regs);
    }, double(iters) * insns_per_iter);
    std::printf("%-22s %8.1f M insn/s  (checksum x0=%llu)\n", name, rate / 1e6, (unsigned long long)cpu.regs.x[0]);
  };

  // 2. tight ALU+branch loop: ADD / SUBS / B.NE  (3 insn per iteration)
  run_loop("execute_alu_loop", {e::add_imm(0, 0, 1), e::subs_imm(1, 1, 1), e::b_cond(e::COND_NE, -2)},
           10'000'000, 3.0, [](Cpu&) {});

  // 3. mixed loop: LDR/ADD/STR/ADD/EOR/SUBS/B.NE  (7 insn: 2 ld/st, 4 dp, 1 branch)
  run_loop("execute_mixed_loop",
           {e::ldst_uimm(3, 1, 0, 3, 2), e::add_reg(2, 2, 0), e::ldst_uimm(3, 0, 0, 3, 2),
            e::add_imm(0, 0, 1), e::eor_reg(4, 4, 2), e::subs_imm(1, 1, 1), e::b_cond(e::COND_NE, -6)},
           5'000'000, 7.0, [](Cpu& c) { c.regs.x[3] = 0x8000; });
  return 0;
}
