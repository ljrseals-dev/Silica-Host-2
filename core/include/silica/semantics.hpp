#pragma once
// Instruction semantics, grouped as in the Arm ARM: data-processing
// (immediate), data-processing (register), branches, loads/stores, system.
// Every function returns the next PC; none can fail (faults are sticky flags).
#include "alu.hpp"
#include "memory.hpp"

namespace silica::sem {

// ---- data-processing (immediate) -----------------------------------------

// ADD/SUB (immediate): Arm ARM C6.2.5 / C6.2.5 (ADD imm), SUB imm; flag-setting ADDS/SUBS.
SILICA_INLINE uint64_t add_sub_imm(RegisterFile& r, const Instruction& i, bool sub) noexcept {
  const uint64_t a = r.rd_sp(i.rn);
  const uint64_t b = static_cast<uint64_t>(i.imm);
  const AwC o = sub ? add_with_carry(a, ~b, 1, i.sf) : add_with_carry(a, b, 0, i.sf);
  if (i.setflags) { set_flags(r.pstate, o); r.wr(i.rd, o.r); }  // Rd=31 is XZR (CMP/CMN)
  else r.wr_sp(i.rd, o.r);                                       // Rd=31 is SP
  return r.pc + 4;
}

SILICA_INLINE uint64_t mov_wide(RegisterFile& r, const Instruction& i) noexcept {
  switch (i.code) {
    case Code::MovK: {  // MOVK: replace one 16-bit field, keep the rest
      const uint64_t keep = ~(0xFFFFull << i.amount);
      r.wr(i.rd, mask_sf((r.rd(i.rd) & keep) | (static_cast<uint64_t>(i.imm) << i.amount), i.sf));
      break;
    }
    default: r.wr(i.rd, static_cast<uint64_t>(i.imm)); break;  // MOVZ / MOVN (value precomputed)
  }
  return r.pc + 4;
}

// AND/ORR/EOR (immediate), ANDS. Rd=31 is SP unless flag-setting.
SILICA_INLINE uint64_t logical_imm(RegisterFile& r, const Instruction& i) noexcept {
  const uint64_t a = mask_sf(r.rd(i.rn), i.sf), b = static_cast<uint64_t>(i.imm);
  uint64_t res = i.code == Code::AndImm ? (a & b) : i.code == Code::OrrImm ? (a | b) : (a ^ b);
  res = mask_sf(res, i.sf);
  if (i.setflags) {
    r.pstate.n = (res >> (i.sf ? 63 : 31)) & 1; r.pstate.z = res == 0;
    r.pstate.c = false; r.pstate.v = false;
    r.wr(i.rd, res);
  } else r.wr_sp(i.rd, res);
  return r.pc + 4;
}

SILICA_INLINE uint64_t adr(RegisterFile& r, const Instruction& i, bool page) noexcept {
  const uint64_t base = page ? (r.pc & ~0xFFFull) : r.pc;
  r.wr(i.rd, base + static_cast<uint64_t>(i.imm));
  return r.pc + 4;
}

// SBFM / BFM / UBFM (Arm ARM pseudocode: bot/top merge under wmask/tmask).
SILICA_INLINE uint64_t bitfield(RegisterFile& r, const Instruction& i) noexcept {
  const uint64_t wm = static_cast<uint64_t>(i.imm), tm = static_cast<uint64_t>(i.imm2);
  const unsigned R = i.amount, S = i.ra;
  const uint64_t src = mask_sf(r.rd(i.rn), i.sf);
  const uint64_t dst = i.code == Code::Bfm ? mask_sf(r.rd(i.rd), i.sf) : 0;
  const uint64_t rot = i.sf ? std::rotr(src, static_cast<int>(R))
                            : static_cast<uint64_t>(std::rotr(static_cast<uint32_t>(src), static_cast<int>(R)));
  const uint64_t bot = (dst & ~wm) | (rot & wm);
  uint64_t top = dst;
  if (i.code == Code::Sbfm) top = ((src >> S) & 1) ? ~0ull : 0ull;
  else if (i.code == Code::Ubfm) top = 0;
  r.wr(i.rd, mask_sf((top & ~tm) | (bot & tm), i.sf));
  return r.pc + 4;
}

// EXTR: extract Rn:Rm >> lsb (ROR immediate when Rn == Rm).
SILICA_INLINE uint64_t extr(RegisterFile& r, const Instruction& i) noexcept {
  const unsigned lsb = i.amount;
  uint64_t res;
  if (i.sf) res = lsb == 0 ? r.rd(i.rm) : ((r.rd(i.rm) >> lsb) | (r.rd(i.rn) << (64 - lsb)));
  else {
    const uint64_t cat = (static_cast<uint64_t>(static_cast<uint32_t>(r.rd(i.rn))) << 32) | static_cast<uint32_t>(r.rd(i.rm));
    res = static_cast<uint32_t>(cat >> lsb);
  }
  r.wr(i.rd, res);
  return r.pc + 4;
}

// ---- data-processing (register) ------------------------------------------

// ADD/SUB (extended register): Rn may be SP; Rd is SP unless flag-setting.
SILICA_INLINE uint64_t add_sub_ext(RegisterFile& r, const Instruction& i, bool sub) noexcept {
  const uint64_t a = r.rd_sp(i.rn);
  uint64_t m = r.rd(i.rm);
  switch (i.shift) {
    case 0: m &= 0xFFull; break;
    case 1: m &= 0xFFFFull; break;
    case 2: m &= 0xFFFFFFFFull; break;
    case 4: m = static_cast<uint64_t>(static_cast<int64_t>(static_cast<int8_t>(m))); break;
    case 5: m = static_cast<uint64_t>(static_cast<int64_t>(static_cast<int16_t>(m))); break;
    case 6: m = static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(m))); break;
    default: break;  // UXTX / SXTX
  }
  m <<= i.amount;
  const AwC o = sub ? add_with_carry(a, ~m, 1, i.sf) : add_with_carry(a, m, 0, i.sf);
  if (i.setflags) { set_flags(r.pstate, o); r.wr(i.rd, o.r); }
  else r.wr_sp(i.rd, o.r);
  return r.pc + 4;
}

// CCMP / CCMN (register or imm5): flags = cond ? compare(Rn, op2) : #nzcv.
SILICA_INLINE uint64_t ccmp(RegisterFile& r, const Instruction& i) noexcept {
  if (cond_holds(i.cond, r.pstate)) {
    const uint64_t a = r.rd(i.rn), b = i.inv ? i.rm : r.rd(i.rm);
    set_flags(r.pstate, i.code == Code::Ccmp ? add_with_carry(a, ~b, 1, i.sf) : add_with_carry(a, b, 0, i.sf));
  } else r.pstate.set_nzcv(i.ra);
  return r.pc + 4;
}

SILICA_INLINE uint64_t umulh64(uint64_t a, uint64_t b) noexcept {  // no __int128: wasm32 has no __multi3
  const uint64_t a0 = static_cast<uint32_t>(a), a1 = a >> 32, b0 = static_cast<uint32_t>(b), b1 = b >> 32;
  const uint64_t p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1;
  const uint64_t mid = (p00 >> 32) + static_cast<uint32_t>(p01) + static_cast<uint32_t>(p10);
  return p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32);
}

// SMADDL/SMSUBL/UMADDL/UMSUBL (SMULL/UMULL aliases), SMULH/UMULH.
SILICA_INLINE uint64_t mul_wide(RegisterFile& r, const Instruction& i) noexcept {
  const uint64_t n = r.rd(i.rn), m = r.rd(i.rm), a = r.rd(i.ra);
  uint64_t res;
  switch (i.code) {
    case Code::Smaddl: case Code::Smsubl: {
      const uint64_t p = static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(n)) * static_cast<int64_t>(static_cast<int32_t>(m)));
      res = i.code == Code::Smaddl ? a + p : a - p; break;
    }
    case Code::Umaddl: case Code::Umsubl: {
      const uint64_t p = static_cast<uint64_t>(static_cast<uint32_t>(n)) * static_cast<uint32_t>(m);
      res = i.code == Code::Umaddl ? a + p : a - p; break;
    }
    case Code::Umulh: res = umulh64(n, m); break;
    default:  // Smulh
      res = umulh64(n, m) - (static_cast<int64_t>(n) < 0 ? m : 0) - (static_cast<int64_t>(m) < 0 ? n : 0);
      break;
  }
  r.wr(i.rd, res);
  return r.pc + 4;
}

// RBIT / REV / REV16 / REV32 / CLZ / CLS.
SILICA_INLINE uint64_t unary(RegisterFile& r, const Instruction& i) noexcept {
  const uint64_t x = mask_sf(r.rd(i.rn), i.sf);
  uint64_t res = 0;
  switch (i.code) {
    case Code::Rbit: {
      uint64_t v = x;
      v = ((v >> 1) & 0x5555555555555555ull) | ((v & 0x5555555555555555ull) << 1);
      v = ((v >> 2) & 0x3333333333333333ull) | ((v & 0x3333333333333333ull) << 2);
      v = ((v >> 4) & 0x0F0F0F0F0F0F0F0Full) | ((v & 0x0F0F0F0F0F0F0F0Full) << 4);
      v = __builtin_bswap64(v);
      res = i.sf ? v : (v >> 32);
      break;
    }
    case Code::Rev: res = i.sf ? __builtin_bswap64(x) : __builtin_bswap32(static_cast<uint32_t>(x)); break;
    case Code::Rev16: {
      const uint64_t m = 0x00FF00FF00FF00FFull;
      res = ((x & m) << 8) | ((x >> 8) & m);
      break;
    }
    case Code::Rev32:
      res = (static_cast<uint64_t>(__builtin_bswap32(static_cast<uint32_t>(x >> 32))) << 32) | __builtin_bswap32(static_cast<uint32_t>(x));
      break;
    case Code::Clz:
      res = i.sf ? static_cast<uint64_t>(std::countl_zero(x)) : static_cast<uint64_t>(std::countl_zero(static_cast<uint32_t>(x)));
      break;
    default: {  // Cls
      if (i.sf) res = static_cast<uint64_t>(std::countl_zero(x ^ static_cast<uint64_t>(static_cast<int64_t>(x) >> 1))) - 1;
      else {
        const uint32_t w = static_cast<uint32_t>(x);
        res = static_cast<uint64_t>(std::countl_zero(w ^ static_cast<uint32_t>(static_cast<int32_t>(w) >> 1))) - 1;
      }
      break;
    }
  }
  r.wr(i.rd, res);
  return r.pc + 4;
}

SILICA_INLINE uint64_t add_sub_reg(RegisterFile& r, const Instruction& i, bool sub) noexcept {
  const uint64_t a = r.rd(i.rn);
  const uint64_t b = shift_val(r.rd(i.rm), i.shift, i.amount, i.sf);
  const AwC o = sub ? add_with_carry(a, ~b, 1, i.sf) : add_with_carry(a, b, 0, i.sf);
  if (i.setflags) set_flags(r.pstate, o);
  r.wr(i.rd, o.r);
  return r.pc + 4;
}

SILICA_INLINE uint64_t adc_sbc(RegisterFile& r, const Instruction& i, bool sub) noexcept {
  const uint64_t a = r.rd(i.rn), b = r.rd(i.rm);
  const AwC o = add_with_carry(a, sub ? ~b : b, r.pstate.c, i.sf);
  if (i.setflags) set_flags(r.pstate, o);
  r.wr(i.rd, o.r);
  return r.pc + 4;
}

SILICA_INLINE uint64_t logical_reg(RegisterFile& r, const Instruction& i) noexcept {
  const uint64_t a = mask_sf(r.rd(i.rn), i.sf);
  uint64_t b = shift_val(r.rd(i.rm), i.shift, i.amount, i.sf);
  if (i.inv) b = mask_sf(~b, i.sf);
  uint64_t res = i.code == Code::AndReg ? (a & b) : i.code == Code::OrrReg ? (a | b) : (a ^ b);
  res = mask_sf(res, i.sf);
  if (i.setflags) {
    r.pstate.n = (res >> (i.sf ? 63 : 31)) & 1; r.pstate.z = res == 0;
    r.pstate.c = false; r.pstate.v = false;
  }
  r.wr(i.rd, res);
  return r.pc + 4;
}

// LSLV/LSRV/ASRV/RORV: shift amount = Rm modulo datasize.
SILICA_INLINE uint64_t var_shift(RegisterFile& r, const Instruction& i, unsigned type) noexcept {
  const unsigned amt = static_cast<unsigned>(r.rd(i.rm)) & (i.sf ? 63u : 31u);
  r.wr(i.rd, shift_val(r.rd(i.rn), type, amt, i.sf));
  return r.pc + 4;
}

// MADD/MSUB (MUL/MNEG are aliases with Ra=XZR).
SILICA_INLINE uint64_t madd_msub(RegisterFile& r, const Instruction& i, bool sub) noexcept {
  const uint64_t p = r.rd(i.rn) * r.rd(i.rm);
  r.wr(i.rd, mask_sf(sub ? r.rd(i.ra) - p : r.rd(i.ra) + p, i.sf));
  return r.pc + 4;
}

// UDIV/SDIV: divide by zero -> 0; signed overflow (MIN / -1) wraps to MIN. No traps.
SILICA_INLINE uint64_t udiv(RegisterFile& r, const Instruction& i) noexcept {
  const uint64_t a = mask_sf(r.rd(i.rn), i.sf), b = mask_sf(r.rd(i.rm), i.sf);
  r.wr(i.rd, b == 0 ? 0 : a / b);
  return r.pc + 4;
}
SILICA_INLINE uint64_t sdiv(RegisterFile& r, const Instruction& i) noexcept {
  uint64_t res;
  if (i.sf) {
    const int64_t a = static_cast<int64_t>(r.rd(i.rn)), b = static_cast<int64_t>(r.rd(i.rm));
    res = b == 0 ? 0 : b == -1 ? 0 - static_cast<uint64_t>(a) : static_cast<uint64_t>(a / b);
  } else {
    const int32_t a = static_cast<int32_t>(r.rd(i.rn)), b = static_cast<int32_t>(r.rd(i.rm));
    res = b == 0 ? 0 : b == -1 ? static_cast<uint32_t>(0u - static_cast<uint32_t>(a))
                               : static_cast<uint32_t>(a / b);
  }
  r.wr(i.rd, res);
  return r.pc + 4;
}

// CSEL / CSINC / CSINV / CSNEG.
SILICA_INLINE uint64_t csel(RegisterFile& r, const Instruction& i) noexcept {
  uint64_t res;
  if (cond_holds(i.cond, r.pstate)) res = r.rd(i.rn);
  else {
    const uint64_t m = r.rd(i.rm);
    switch (i.code) {
      case Code::Csinc: res = m + 1; break;
      case Code::Csinv: res = ~m; break;
      case Code::Csneg: res = 0 - m; break;
      default: res = m; break;
    }
  }
  r.wr(i.rd, mask_sf(res, i.sf));
  return r.pc + 4;
}

// ---- branches --------------------------------------------------------------

SILICA_INLINE uint64_t branch(RegisterFile& r, const Instruction& i) noexcept {
  const uint64_t pc = r.pc;
  switch (i.code) {
    case Code::B: return pc + static_cast<uint64_t>(i.imm);
    case Code::Bl: r.wr(30, pc + 4); return pc + static_cast<uint64_t>(i.imm);
    case Code::Br: case Code::Ret: return r.rd(i.rn);
    case Code::Blr: { const uint64_t t = r.rd(i.rn); r.wr(30, pc + 4); return t; }
    case Code::BCond:
      return cond_holds(i.cond, r.pstate) ? pc + static_cast<uint64_t>(i.imm) : pc + 4;
    case Code::Cbz: return mask_sf(r.rd(i.rd), i.sf) == 0 ? pc + static_cast<uint64_t>(i.imm) : pc + 4;
    case Code::Cbnz: return mask_sf(r.rd(i.rd), i.sf) != 0 ? pc + static_cast<uint64_t>(i.imm) : pc + 4;
    case Code::Tbz: return ((r.rd(i.rd) >> i.amount) & 1) == 0 ? pc + static_cast<uint64_t>(i.imm) : pc + 4;
    default: return ((r.rd(i.rd) >> i.amount) & 1) != 0 ? pc + static_cast<uint64_t>(i.imm) : pc + 4;
  }
}

// ---- loads / stores -------------------------------------------------------
// Alignment checks are not modelled (SCTLR-dependent); unaligned access succeeds.

SILICA_INLINE uint64_t reg_offset(const RegisterFile& r, const Instruction& i) noexcept {
  uint64_t m = r.rd(i.rm);
  if (i.shift == 2) m &= 0xFFFFFFFFull;                                                    // UXTW
  else if (i.shift == 6) m = static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(m)));  // SXTW
  return m << i.amount;                                                                    // LSL / SXTX: full width
}

SILICA_INLINE uint64_t ldr_str(RegisterFile& r, FlatMemory& m, const Instruction& i) noexcept {
  const uint64_t base = r.rd_sp(i.rn);
  const uint64_t off = i.mode == 3 ? reg_offset(r, i) : static_cast<uint64_t>(i.imm);
  const bool wb = i.mode == 1 || i.mode == 2;
  const uint64_t addr = i.mode == 2 ? base : base + off;  // post-index accesses the base
  if (i.code == Code::Str) {
    const uint64_t v = r.rd(i.rd);
    switch (i.size) {
      case 0: m.write<uint8_t>(addr, static_cast<uint8_t>(v)); break;
      case 1: m.write<uint16_t>(addr, static_cast<uint16_t>(v)); break;
      case 2: m.write<uint32_t>(addr, static_cast<uint32_t>(v)); break;
      default: m.write<uint64_t>(addr, v); break;
    }
    if (wb) r.wr_sp(i.rn, base + off);
  } else {
    uint64_t v;
    switch (i.size) {
      case 0: v = m.read<uint8_t>(addr);  if (i.sext) v = static_cast<uint64_t>(static_cast<int64_t>(static_cast<int8_t>(v))); break;
      case 1: v = m.read<uint16_t>(addr); if (i.sext) v = static_cast<uint64_t>(static_cast<int64_t>(static_cast<int16_t>(v))); break;
      case 2: v = m.read<uint32_t>(addr); if (i.sext) v = static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(v))); break;
      default: v = m.read<uint64_t>(addr); break;
    }
    if (wb) r.wr_sp(i.rn, base + off);
    r.wr(i.rd, mask_sf(v, i.sf));
  }
  return r.pc + 4;
}

SILICA_INLINE uint64_t ldp_stp(RegisterFile& r, FlatMemory& m, const Instruction& i) noexcept {
  const uint64_t base = r.rd_sp(i.rn);
  const uint64_t off = static_cast<uint64_t>(i.imm);
  const uint64_t addr = i.mode == 2 ? base : base + off;
  if (i.code == Code::Stp) {
    const uint64_t a = r.rd(i.rd), b = r.rd(i.ra);
    if (i.sf) { m.write<uint64_t>(addr, a); m.write<uint64_t>(addr + 8, b); }
    else { m.write<uint32_t>(addr, static_cast<uint32_t>(a)); m.write<uint32_t>(addr + 4, static_cast<uint32_t>(b)); }
    if (i.mode) r.wr_sp(i.rn, base + off);
  } else {
    uint64_t a, b;
    if (i.sext) {  // LDPSW
      a = static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(m.read<uint32_t>(addr))));
      b = static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(m.read<uint32_t>(addr + 4))));
    } else if (i.sf) { a = m.read<uint64_t>(addr); b = m.read<uint64_t>(addr + 8); }
    else { a = m.read<uint32_t>(addr); b = m.read<uint32_t>(addr + 4); }
    if (i.mode) r.wr_sp(i.rn, base + off);
    r.wr(i.rd, a); r.wr(i.ra, b);
  }
  return r.pc + 4;
}

// ---- system ---------------------------------------------------------------
// MRS/MSR over the fixed-index register map; barriers decode to NOP.

SILICA_INLINE uint64_t mrs(RegisterFile& r, const Instruction& i) noexcept {
  const uint64_t v = i.imm == SYS_NZCV ? static_cast<uint64_t>(r.pstate.nzcv()) << 28
                                       : r.sys[static_cast<size_t>(i.imm)];
  r.wr(i.rd, v);
  return r.pc + 4;
}
SILICA_INLINE uint64_t msr(RegisterFile& r, const Instruction& i) noexcept {
  const uint64_t v = r.rd(i.rd);
  if (i.imm == SYS_NZCV) r.pstate.set_nzcv(static_cast<uint8_t>((v >> 28) & 0xF));
  else r.sys[static_cast<size_t>(i.imm)] = v;
  return r.pc + 4;
}

}  // namespace silica::sem
