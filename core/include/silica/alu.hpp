#pragma once
// ALU primitives shared by the semantics. Pure functions of their inputs.
#include "decoder.hpp"
#include "registers.hpp"

namespace silica {

struct AwC { uint64_t r; bool n, z, c, v; };

// AddWithCarry(x, y, carry_in) -- Arm ARM shared pseudocode. For sf == 0 the
// operation is performed on the low 32 bits and the result is zero-extended.
SILICA_INLINE AwC add_with_carry(uint64_t x, uint64_t y, uint64_t cin, bool sf) noexcept {
  AwC o;
  if (sf) {
    const uint64_t s1 = x + y;
    const bool c1 = s1 < x;
    const uint64_t s2 = s1 + cin;
    const bool c2 = s2 < s1;
    o.r = s2;
    o.c = c1 | c2;
    o.v = (((x ^ s2) & (y ^ s2)) >> 63) & 1;
    o.n = s2 >> 63;
    o.z = s2 == 0;
  } else {
    const uint32_t x32 = static_cast<uint32_t>(x), y32 = static_cast<uint32_t>(y);
    const uint64_t s = static_cast<uint64_t>(x32) + y32 + cin;
    const uint32_t r = static_cast<uint32_t>(s);
    o.r = r;
    o.c = (s >> 32) & 1;
    o.v = (((x32 ^ r) & (y32 ^ r)) >> 31) & 1;
    o.n = r >> 31;
    o.z = r == 0;
  }
  return o;
}

SILICA_INLINE void set_flags(PState& ps, const AwC& a) noexcept {
  ps.n = a.n; ps.z = a.z; ps.c = a.c; ps.v = a.v;
}

SILICA_INLINE uint64_t mask_sf(uint64_t v, bool sf) noexcept {
  return sf ? v : (v & 0xFFFFFFFFull);
}

// ShiftReg: type 0 LSL, 1 LSR, 2 ASR, 3 ROR within the operand size.
SILICA_INLINE uint64_t shift_val(uint64_t v, unsigned type, unsigned amt, bool sf) noexcept {
  if (sf) {
    switch (type) {
      case 0: return v << amt;
      case 1: return v >> amt;
      case 2: return static_cast<uint64_t>(static_cast<int64_t>(v) >> amt);
      default: return std::rotr(v, static_cast<int>(amt));
    }
  }
  const uint32_t x = static_cast<uint32_t>(v);
  switch (type) {
    case 0: return static_cast<uint32_t>(x << amt);
    case 1: return x >> amt;
    case 2: return static_cast<uint32_t>(static_cast<int32_t>(x) >> amt);
    default: return std::rotr(x, static_cast<int>(amt));
  }
}

// ConditionHolds(cond) -- Arm ARM.
SILICA_INLINE bool cond_holds(unsigned cond, const PState& p) noexcept {
  bool r;
  switch (cond >> 1) {
    case 0: r = p.z; break;
    case 1: r = p.c; break;
    case 2: r = p.n; break;
    case 3: r = p.v; break;
    case 4: r = p.c && !p.z; break;
    case 5: r = p.n == p.v; break;
    case 6: r = p.n == p.v && !p.z; break;
    default: r = true; break;
  }
  if ((cond & 1) && cond != 0xF) r = !r;
  return r;
}

}  // namespace silica
