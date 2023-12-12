#pragma once

#include "variable.h"

namespace synapse {
namespace synthesizer {
namespace tfhe {

class StaticByteArray : public Variable {
public:
  StaticByteArray(std::string _label, bits_t _size, klee::ref<klee::Expr> _expr)
      : Variable(_label, _size, _expr) {
    is_array = true;
  }

  StaticByteArray(std::string _label, klee::ref<klee::Expr> _size,
                  klee::ref<klee::Expr> _expr)
      : StaticByteArray(_label, 8, _expr) {
    assert(kutil::is_constant(_size));
    size = kutil::solver_toolbox.value_from_expr(_size);
  }

  virtual std::string get_type() const override { return "uint8_t"; }
};

} // namespace tfhe
} // namespace synthesizer
} // namespace synapse