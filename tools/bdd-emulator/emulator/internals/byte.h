#pragma once

#include <algorithm>
#include <assert.h>
#include <inttypes.h>
#include <iomanip>
#include <ostream>
#include <utility>

namespace BDD {
namespace emulation {

typedef uint8_t byte_t;

struct bytes_t {
  byte_t *values;
  uint32_t size;

  bytes_t() : values(nullptr), size(0) {}

  bytes_t(uint32_t _size) : values(new byte_t[_size]), size(_size) {
    for (auto i = 0u; i < size; i++) {
      values[i] = 0;
    }
  }

  bytes_t(uint32_t _size, uint64_t value) : bytes_t(_size) {
    for (auto i = 0u; i < size; i++) {
      values[i] = (value >> (8 * (size - i - 1))) & 0xff;
    }
  }

  bytes_t(const bytes_t &key) : bytes_t(key.size) {
    for (auto i = 0u; i < size; i++) {
      values[i] = key.values[i];
    }
  }

  bytes_t(const bytes_t &key, uint32_t offset) : bytes_t(key.size - offset) {
    for (auto i = offset; i - offset < size; i++) {
      values[i - offset] = key.values[i];
    }
  }

  ~bytes_t() {
    if (values) {
      delete[] values;
      values = nullptr;
      size = 0;
    }
  }

  byte_t &operator[](int i) {
    assert(i < (int)size);
    return values[i];
  }

  const byte_t &operator[](int i) const {
    assert(i < (int)size);
    return values[i];
  }

  // copy assignment
  bytes_t &operator=(const bytes_t &other) noexcept {
    // Guard self assignment
    if (this == &other) {
      return *this;
    }

    auto size_to_copy = std::min(size, other.size);
    std::copy(other.values, other.values + size_to_copy, values);
    return *this;
  }

  struct hash {
    std::size_t operator()(const bytes_t &k) const {
      int hash = 0;

      for (auto i = 0u; i < k.size; i++) {
        hash ^= std::hash<int>()(k.values[i]);
      }

      return hash;
    }
  };
};

inline bool operator==(const bytes_t &lhs, const bytes_t &rhs) {
  if (lhs.size != rhs.size) {
    return false;
  }

  for (auto i = 0u; i < lhs.size; i++) {
    if (lhs.values[i] != rhs.values[i]) {
      return false;
    }
  }

  return true;
}

inline std::ostream &operator<<(std::ostream &os, const bytes_t &bytes) {
  os << "{";
  for (auto i = 0u; i < bytes.size; i++) {
    if (i > 0)
      os << ",";
    os << "0x" << std::hex << std::setw(2) << std::setfill('0') << (int)bytes[i]
       << std::dec;
  }
  os << "}";
  return os;
}

} // namespace emulation
} // namespace BDD