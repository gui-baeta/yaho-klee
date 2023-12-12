#pragma once

#include <string>
#include <vector>

#include "klee/Expr.h"

#include "byte_array.h"
#include "static_byte_array.h"
#include "variable.h"

namespace synapse {
namespace synthesizer {
namespace tfhe {

struct stack_t {
  std::vector<std::vector<Variable>> stack;
  int var_name_counter;

  stack_t() : var_name_counter(0) { push(); }

  stack_t(const std::vector<Variable> &vars) : stack_t() {
    for (auto v : vars) {
      append(v);
    }
  }

  void push() {
    auto new_frame = std::vector<Variable>();
    stack.insert(stack.begin(), new_frame);
  }

  void pop() {
    assert(stack.size());
    stack.erase(stack.begin());
  }

  std::vector<Variable> get() const {
    std::vector<Variable> vars;

    for (auto frame : stack) {
      vars.insert(vars.end(), frame.begin(), frame.end());
    }

    return vars;
  }

  variable_query_t get(std::string symbol) const {
    for (const auto &frame : stack) {
      for (const auto &variable : frame) {
        if (variable.match(symbol)) {
          return variable_query_t(variable, 0);
        }
      }
    }

    return variable_query_t();
  }

  variable_query_t get(addr_t addr) const {
    for (const auto &frame : stack) {
      for (const auto &variable : frame) {
        if (variable.match(addr)) {
          return variable_query_t(variable, 0);
        }
      }
    }

    return variable_query_t();
  }

  variable_query_t get(klee::ref<klee::Expr> expr) const {
    for (const auto &frame : stack) {
      for (const auto &variable : frame) {
        if (variable.match(expr)) {
          return variable_query_t(variable, 0);
        }

        auto contains_result = variable.contains(expr);

        if (contains_result.contains) {
          return variable_query_t(variable, contains_result.offset_bits);
        }
      }
    }

    return variable_query_t();
  }

  void append(Variable var) {
    assert(stack.size());
    stack.begin()->insert(stack.begin()->begin(), var);
  }

  std::string get_new_label(const std::string &base) {
    return base + std::to_string(var_name_counter++);
  }
};

} // namespace tfhe
} // namespace synthesizer
} // namespace synapse