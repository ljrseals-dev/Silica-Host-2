#pragma once
// CPU = registers + memory + fetch/decode/execute loop.
#include <span>
#include "semantics.hpp"

namespace silica {

enum class HaltReason : uint8_t { None, ZeroWord, Brk, Invalid, MemFault };

class Cpu {
 public:
  RegisterFile regs;
  FlatMemory mem;
  uint64_t steps = 0;
  HaltReason halt = HaltReason::None;

  explicit Cpu(size_t mem_bytes = 1u << 20) : mem(mem_bytes) {}

  bool halted() const noexcept { return halt != HaltReason::None; }

  // Copy code into memory at `base` and point PC at it. False if it doesn't fit.
  bool load_program(uint64_t base, std::span<const uint32_t> code) noexcept {
    if (!mem.load_words(base, code)) return false;
    regs.pc = base;
    halt = HaltReason::None;
    mem.clear_fault();
    return true;
  }

  // One fetch-decode-execute step. Never throws, never returns an error:
  // check halted()/halt afterwards. A halting instruction still counts as a step.
  SILICA_INLINE void step() noexcept {
    if (halt != HaltReason::None) [[unlikely]] return;
    const uint64_t pc = regs.pc;
    if (pc & 3) [[unlikely]] { halt = HaltReason::MemFault; return; }
    const uint32_t word = mem.read<uint32_t>(pc);
    Instruction i;
    decode_into(word, i);
    regs.pc = execute(i);
    ++steps;
    if (mem.fault()) [[unlikely]] { halt = HaltReason::MemFault; regs.pc = pc; }
  }

  // Run until halted or `max_steps` executed; returns steps executed.
  uint64_t run(uint64_t max_steps) noexcept {
    const uint64_t start = steps;
    while (halt == HaltReason::None && steps - start < max_steps) step();
    return steps - start;
  }

 private:
  SILICA_INLINE uint64_t execute(const Instruction& i) noexcept {
    switch (i.code) {
      case Code::AddImm: return sem::add_sub_imm(regs, i, false);
      case Code::SubImm: return sem::add_sub_imm(regs, i, true);
      case Code::MovZ: case Code::MovN: case Code::MovK: return sem::mov_wide(regs, i);
      case Code::AndImm: case Code::OrrImm: case Code::EorImm: return sem::logical_imm(regs, i);
      case Code::Adr: return sem::adr(regs, i, false);
      case Code::Adrp: return sem::adr(regs, i, true);
      case Code::AddReg: return sem::add_sub_reg(regs, i, false);
      case Code::SubReg: return sem::add_sub_reg(regs, i, true);
      case Code::AndReg: case Code::OrrReg: case Code::EorReg: return sem::logical_reg(regs, i);
      case Code::Adc: return sem::adc_sbc(regs, i, false);
      case Code::Sbc: return sem::adc_sbc(regs, i, true);
      case Code::Lslv: return sem::var_shift(regs, i, 0);
      case Code::Lsrv: return sem::var_shift(regs, i, 1);
      case Code::Asrv: return sem::var_shift(regs, i, 2);
      case Code::Rorv: return sem::var_shift(regs, i, 3);
      case Code::Madd: return sem::madd_msub(regs, i, false);
      case Code::Msub: return sem::madd_msub(regs, i, true);
      case Code::Udiv: return sem::udiv(regs, i);
      case Code::Sdiv: return sem::sdiv(regs, i);
      case Code::Sbfm: case Code::Bfm: case Code::Ubfm: return sem::bitfield(regs, i);
      case Code::Extr: return sem::extr(regs, i);
      case Code::AddExt: return sem::add_sub_ext(regs, i, false);
      case Code::SubExt: return sem::add_sub_ext(regs, i, true);
      case Code::Smaddl: case Code::Smsubl: case Code::Umaddl: case Code::Umsubl:
      case Code::Smulh: case Code::Umulh: return sem::mul_wide(regs, i);
      case Code::Ccmp: case Code::Ccmn: return sem::ccmp(regs, i);
      case Code::Rbit: case Code::Rev: case Code::Rev16: case Code::Rev32: case Code::Clz: case Code::Cls:
        return sem::unary(regs, i);
      case Code::Csel: case Code::Csinc: case Code::Csinv: case Code::Csneg: return sem::csel(regs, i);
      case Code::B: case Code::Bl: case Code::Br: case Code::Blr: case Code::Ret:
      case Code::BCond: case Code::Cbz: case Code::Cbnz: case Code::Tbz: case Code::Tbnz:
        return sem::branch(regs, i);
      case Code::Ldr: case Code::Str: return sem::ldr_str(regs, mem, i);
      case Code::Ldp: case Code::Stp: return sem::ldp_stp(regs, mem, i);
      case Code::Mrs: return sem::mrs(regs, i);
      case Code::Msr: return sem::msr(regs, i);
      case Code::Nop: return regs.pc + 4;
      case Code::Halt: halt = HaltReason::ZeroWord; return regs.pc;
      case Code::Brk: halt = HaltReason::Brk; return regs.pc;
      default: halt = HaltReason::Invalid; return regs.pc;
    }
  }
};

}  // namespace silica
