#pragma once
// Architectural register state. Zero heap. x[31] is kept as a permanent zero
// (XZR) so reads are branch-free; SP is stored separately.
#include "common.hpp"

namespace silica {

struct PState {
  bool n = false, z = false, c = false, v = false;
  uint8_t el = 0;     // exception level (0-3)
  uint8_t spsel = 0;  // SP select
  constexpr uint8_t nzcv() const noexcept {
    return static_cast<uint8_t>((n << 3) | (z << 2) | (c << 1) | (v << 0));
  }
  constexpr void set_nzcv(uint8_t f) noexcept {
    n = (f >> 3) & 1; z = (f >> 2) & 1; c = (f >> 1) & 1; v = f & 1;
  }
};

// Fixed-index system register slots (no maps, no allocation).
enum SysIdx : uint8_t {
  SYS_TPIDR_EL0 = 0,
  SYS_VBAR_EL1,
  SYS_ELR_EL1,
  SYS_SPSR_EL1,
  SYS_COUNT,
  SYS_NZCV = 15  // pseudo-index: maps to PState.NZCV
};

struct alignas(16) RegisterFile {
  uint64_t x[32] = {};        // X0-X30; x[31] invariant == 0 (XZR)
  uint64_t sp = 0;
  uint64_t v[32][2] = {};     // V0-V31, 128-bit as two u64 (lo, hi)
  uint64_t pc = 0;
  PState pstate{};
  uint64_t sys[SYS_COUNT] = {};

  // Register 31 reads as zero (XZR / WZR).
  SILICA_INLINE uint64_t rd(unsigned n) const noexcept { return x[n]; }
  SILICA_INLINE void wr(unsigned n, uint64_t val) noexcept { x[n] = val; x[31] = 0; }
  // Register 31 means SP.
  SILICA_INLINE uint64_t rd_sp(unsigned n) const noexcept { return n == 31 ? sp : x[n]; }
  SILICA_INLINE void wr_sp(unsigned n, uint64_t val) noexcept {
    if (n == 31) sp = val; else x[n] = val;
  }
};

}  // namespace silica
