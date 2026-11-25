#pragma once
// Flat little-endian address space [0, size). Out-of-range access sets a
// sticky fault flag (no exceptions, no Result on the hot path).
#include <cstddef>
#include <cstring>
#include <span>
#include <vector>
#include "common.hpp"

namespace silica {

class FlatMemory {
 public:
  explicit FlatMemory(size_t size = 1u << 20) : data_(size < 16 ? 16 : size) {
    size_ = data_.size();
  }
  size_t size() const noexcept { return size_; }
  bool fault() const noexcept { return fault_; }
  // Raw backing store (SilicaHost: lets the embedder read/write guest RAM directly).
  uint8_t* data() noexcept { return data_.data(); }
  void clear_fault() noexcept { fault_ = false; }

  template <class T>
  SILICA_INLINE T read(uint64_t a) noexcept {
    if (a > size_ - sizeof(T)) [[unlikely]] { fault_ = true; return T{}; }
    T v;
    std::memcpy(&v, data_.data() + a, sizeof(T));
    return v;
  }
  template <class T>
  SILICA_INLINE void write(uint64_t a, T v) noexcept {
    if (a > size_ - sizeof(T)) [[unlikely]] { fault_ = true; return; }
    std::memcpy(data_.data() + a, &v, sizeof(T));
  }
  bool load_words(uint64_t addr, std::span<const uint32_t> w) noexcept {
    const size_t bytes = w.size() * 4;
    if (addr > size_ || bytes > size_ - addr) return false;
    std::memcpy(data_.data() + addr, w.data(), bytes);
    return true;
  }

 private:
  std::vector<uint8_t> data_;
  size_t size_ = 0;
  bool fault_ = false;
};

}  // namespace silica
