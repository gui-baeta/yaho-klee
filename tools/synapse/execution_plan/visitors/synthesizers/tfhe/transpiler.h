#pragma once

#include "klee-util.h"
#include <klee/Expr.h>

#include <string>
#include <vector>

#include "domain/variable.h"

namespace synapse {
namespace synthesizer {
namespace tfhe {

class tfheGenerator;

class Transpiler {
private:
  tfheGenerator &generator;

public:
  Transpiler(tfheGenerator &_generator) : generator(_generator) {}

  std::string transpile(const klee::ref<klee::Expr> &expr);
  std::string size_to_type(bits_t size, bool is_signed = false) const;
  std::string mask(std::string expr, bits_t offset, bits_t size) const;

  std::pair<bool, std::string>
  try_transpile_variable(const klee::ref<klee::Expr> &expr) const;

  std::pair<bool, std::string>
  try_transpile_constant(const klee::ref<klee::Expr> &expr) const;
};

} // namespace tfhe
} // namespace synthesizer
} // namespace synapse