#pragma once

#include "variable.h"

namespace synapse {
namespace synthesizer {
namespace tfhe {

class ByteArray : public Variable {
private:
  klee::ref<klee::Expr> var_len;

public:
  ByteArray(std::string _label, addr_t _addr) : Variable(_label, 0) {
    addr = _addr;
    is_array = true;
  }

  ByteArray(std::string _label, bits_t _size, klee::ref<klee::Expr> _expr,
            addr_t _addr)
      : Variable(_label, _size, _expr) {
    addr = _addr;
    is_array = true;
  }

  ByteArray(std::string _label, klee::ref<klee::Expr> _expr, addr_t _addr)
      : Variable(_label, _expr->getWidth(), _expr) {
    addr = _addr;
    is_array = true;
  }

  ByteArray(std::string _label, klee::ref<klee::Expr> _size,
            klee::ref<klee::Expr> _expr, addr_t _addr)
      : ByteArray(_label, 8, _expr, _addr) {
    if (!kutil::is_constant(_size)) {
      var_len = _size;
    } else {
      size = kutil::solver_toolbox.value_from_expr(_size);
    }
  }

  virtual std::string get_type() const override { return "uint8_t*"; }
};

} // namespace tfhe
} // namespace synthesizer
} // namespace synapse