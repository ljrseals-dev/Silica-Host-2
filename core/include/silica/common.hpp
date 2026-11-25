#pragma once
// Shared helpers: inlining macro, bit extraction, sign extension.
#include <bit>
#include <cstdint>

#if defined(__GNUC__) || defined(__clang__)
#define SILICA_INLINE inline __attribute__((always_inline))
#else
#define SILICA_INLINE inline
#endif

namespace silica {

static_assert(std::endian::native == std::endian::little,
              "silica requires a little-endian host (memcpy-based memory access)");

// Extract bits [hi:lo] of w (inclusive), Arm ARM notation.
constexpr uint32_t bits(uint32_t w, unsigned hi, unsigned lo) noexcept {
  return static_cast<uint32_t>((w >> lo) & ((1ull << (hi - lo + 1)) - 1ull));
}

// SignExtend(v, nbits) per Arm ARM.
constexpr int64_t sext(uint64_t v, unsigned nbits) noexcept {
  const uint64_t m = 1ull << (nbits - 1);
  return static_cast<int64_t>((v ^ m) - m);
}

}  // namespace silica
