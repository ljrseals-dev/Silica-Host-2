#pragma once
// Tiny A64 encoders for tests, benchmarks and examples (not used by the core).
#include <cstdint>

namespace silica::enc {

using u = uint32_t;

constexpr u add_sub_imm(u sf, u sub, u s, u sh, u imm12, u rn, u rd) {
  return (sf << 31) | (sub << 30) | (s << 29) | 0x11000000u | (sh << 22) | (imm12 << 10) | (rn << 5) | rd;
}
constexpr u add_imm(u rd, u rn, u imm12, u sf = 1) { return add_sub_imm(sf, 0, 0, 0, imm12, rn, rd); }
constexpr u sub_imm(u rd, u rn, u imm12, u sf = 1) { return add_sub_imm(sf, 1, 0, 0, imm12, rn, rd); }
constexpr u subs_imm(u rd, u rn, u imm12, u sf = 1) { return add_sub_imm(sf, 1, 1, 0, imm12, rn, rd); }

constexpr u movn(u sf, u imm16, u hw, u rd) { return (sf << 31) | 0x12800000u | (hw << 21) | (imm16 << 5) | rd; }
constexpr u movz(u sf, u imm16, u hw, u rd) { return (sf << 31) | 0x52800000u | (hw << 21) | (imm16 << 5) | rd; }
constexpr u movk(u sf, u imm16, u hw, u rd) { return (sf << 31) | 0x72800000u | (hw << 21) | (imm16 << 5) | rd; }

constexpr u logical_imm(u sf, u opc, u N, u immr, u imms, u rn, u rd) {
  return (sf << 31) | (opc << 29) | 0x12000000u | (N << 22) | (immr << 16) | (imms << 10) | (rn << 5) | rd;
}

constexpr u add_sub_reg(u sf, u sub, u s, u shift, u rm, u amt, u rn, u rd) {
  return (sf << 31) | (sub << 30) | (s << 29) | 0x0B000000u | (shift << 22) | (rm << 16) | (amt << 10) | (rn << 5) | rd;
}
constexpr u add_reg(u rd, u rn, u rm, u sf = 1) { return add_sub_reg(sf, 0, 0, 0, rm, 0, rn, rd); }
constexpr u logical_reg(u sf, u opc, u shift, u n, u rm, u amt, u rn, u rd) {
  return (sf << 31) | (opc << 29) | 0x0A000000u | (shift << 22) | (n << 21) | (rm << 16) | (amt << 10) | (rn << 5) | rd;
}
constexpr u eor_reg(u rd, u rn, u rm, u sf = 1) { return logical_reg(sf, 2, 0, 0, rm, 0, rn, rd); }
constexpr u adc_sbc(u sf, u sub, u s, u rm, u rn, u rd) {
  return (sf << 31) | (sub << 30) | (s << 29) | 0x1A000000u | (rm << 16) | (rn << 5) | rd;
}
constexpr u dp2(u sf, u opcode, u rm, u rn, u rd) {
  return (sf << 31) | 0x1AC00000u | (rm << 16) | (opcode << 10) | (rn << 5) | rd;
}
constexpr u udiv(u sf, u rd, u rn, u rm) { return dp2(sf, 2, rm, rn, rd); }
constexpr u sdiv(u sf, u rd, u rn, u rm) { return dp2(sf, 3, rm, rn, rd); }
constexpr u madd(u sf, u sub, u rm, u ra, u rn, u rd) {
  return (sf << 31) | 0x1B000000u | (rm << 16) | (sub << 15) | (ra << 10) | (rn << 5) | rd;
}
constexpr u csel_family(u sf, u op, u o2, u rm, u cond, u rn, u rd) {
  return (sf << 31) | (op << 30) | 0x1A800000u | (rm << 16) | (cond << 12) | (o2 << 10) | (rn << 5) | rd;
}

constexpr u b(int32_t words) { return 0x14000000u | (static_cast<u>(words) & 0x3FFFFFFu); }
constexpr u bl(int32_t words) { return 0x94000000u | (static_cast<u>(words) & 0x3FFFFFFu); }
constexpr u b_cond(u cond, int32_t words) { return 0x54000000u | ((static_cast<u>(words) & 0x7FFFFu) << 5) | cond; }
constexpr u cbz(u sf, u nz, int32_t words, u rt) {
  return (sf << 31) | 0x34000000u | (nz << 24) | ((static_cast<u>(words) & 0x7FFFFu) << 5) | rt;
}
constexpr u tbz(u nz, u bit, int32_t words, u rt) {
  return ((bit >> 5) << 31) | 0x36000000u | (nz << 24) | ((bit & 31u) << 19) | ((static_cast<u>(words) & 0x3FFFu) << 5) | rt;
}
constexpr u br(u rn) { return 0xD61F0000u | (rn << 5); }
constexpr u blr(u rn) { return 0xD63F0000u | (rn << 5); }
constexpr u ret(u rn = 30) { return 0xD65F0000u | (rn << 5); }
constexpr u nop() { return 0xD503201Fu; }
constexpr u brk(u imm16 = 0) { return 0xD4200000u | (imm16 << 5); }

// size: 0=B 1=H 2=W 3=X; opc: 0 store, 1 load, 2 load-signed->X, 3 load-signed->W
constexpr u ldst_uimm(u size, u opc, u imm12, u rn, u rt) {
  return (size << 30) | 0x39000000u | (opc << 22) | (imm12 << 10) | (rn << 5) | rt;
}
// mode_bits: 0 unscaled, 1 post-index, 3 pre-index
constexpr u ldst_imm9(u size, u opc, int32_t imm9, u mode_bits, u rn, u rt) {
  return (size << 30) | 0x38000000u | (opc << 22) | ((static_cast<u>(imm9) & 0x1FFu) << 12) | (mode_bits << 10) | (rn << 5) | rt;
}
// mode: 1 post-index, 2 signed offset, 3 pre-index; opc: 0 = 32-bit, 2 = 64-bit; imm7 in elements
constexpr u ldp_stp(u opc, u load, u mode, int32_t imm7, u rt2, u rn, u rt) {
  return (opc << 30) | 0x28000000u | (mode << 23) | (load << 22) | ((static_cast<u>(imm7) & 0x7Fu) << 15) | (rt2 << 10) | (rn << 5) | rt;
}

constexpr u mrs(u field, u rt) { return 0xD5300000u | (field << 5) | rt; }
constexpr u msr(u field, u rt) { return 0xD5100000u | (field << 5) | rt; }
constexpr u FIELD_NZCV = 0x5A10, FIELD_TPIDR_EL0 = 0x5E82;

constexpr u COND_EQ = 0, COND_NE = 1, COND_GE = 10, COND_LT = 11;

}  // namespace silica::enc
