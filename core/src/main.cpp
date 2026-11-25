// silica-sim: run a raw little-endian A64 binary (or a built-in demo) on the functional core.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "silica/asm.hpp"
#include "silica/cpu.hpp"

using namespace silica;

static const char* reason(HaltReason r) {
  switch (r) {
    case HaltReason::None: return "none (step limit)";
    case HaltReason::ZeroWord: return "zero word";
    case HaltReason::Brk: return "BRK";
    case HaltReason::Invalid: return "invalid/unmodelled instruction";
    case HaltReason::MemFault: return "memory fault";
  }
  return "?";
}

int main(int argc, char** argv) {
  uint64_t max_steps = 100000000ull;
  const char* path = nullptr;
  for (int a = 1; a < argc; ++a) {
    if (!std::strcmp(argv[a], "--max") && a + 1 < argc) max_steps = std::strtoull(argv[++a], nullptr, 0);
    else if (!std::strcmp(argv[a], "--help")) {
      std::puts("usage: silica-sim [--max N] [program.bin]\n"
                "  Loads little-endian A64 words at 0x1000 and runs them. No file: built-in demo (sum 1..10).");
      return 0;
    } else path = argv[a];
  }
  std::vector<uint32_t> prog;
  if (path) {
    std::FILE* f = std::fopen(path, "rb");
    if (!f) { std::perror(path); return 1; }
    uint32_t w;
    while (std::fread(&w, 4, 1, f) == 1) prog.push_back(w);
    std::fclose(f);
  } else {
    namespace e = enc;
    prog = {e::movz(1, 0, 0, 0), e::movz(1, 10, 0, 1), e::add_reg(0, 0, 1),
            e::subs_imm(1, 1, 1), e::b_cond(e::COND_NE, -2), 0};
  }
  Cpu cpu;
  if (!cpu.load_program(0x1000, prog)) { std::fputs("program does not fit in memory\n", stderr); return 1; }
  cpu.run(max_steps);
  std::printf("steps=%llu halt=%s pc=0x%llx NZCV=%d%d%d%d\n", (unsigned long long)cpu.steps, reason(cpu.halt),
              (unsigned long long)cpu.regs.pc, cpu.regs.pstate.n, cpu.regs.pstate.z, cpu.regs.pstate.c, cpu.regs.pstate.v);
  for (int i = 0; i < 31; ++i)
    if (cpu.regs.x[i]) std::printf("x%-2d = 0x%016llx\n", i, (unsigned long long)cpu.regs.x[i]);
  return 0;
}
