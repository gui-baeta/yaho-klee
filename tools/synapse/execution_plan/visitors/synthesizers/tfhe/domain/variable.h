#pragma once

#include <string>
#include <vector>

#include "../../../../memory_bank.h"
#include "../../code_builder.h"
#include "../../util.h"

#include "klee-util.h"
#include "klee/Expr.h"

namespace synapse {
namespace synthesizer {
namespace tfhe {

class Variable {
protected:
  std::string label;
  bits_t size;
  addr_t addr;
  bool is_array;

  std::vector<BDD::label_t> symbols;
  std::vector<klee::ref<klee::Expr>> exprs;

public:
  Variable(const Variable &variable)
      : label(variable.label), size(variable.size), addr(variable.addr),
        is_array(variable.is_array), symbols(variable.symbols),
        exprs(variable.exprs) {}

  Variable(std::string _label, bits_t _size, klee::ref<klee::Expr> expr)
      : label(_label), size(_size), addr(0), is_array(false) {
    add_expr(expr);
  }

  Variable(std::string _label, bits_t _size,
           const std::vector<std::string> &_symbols)
      : label(_label), size(_size), addr(0), is_array(false),
        symbols(_symbols) {}

  Variable(std::string _label, const BDD::symbol_t symbol)
      : label(_label), size(symbol.expr->getWidth()), addr(0), is_array(false),
        symbols({symbol.label}) {}

  Variable(std::string _label, bits_t _size)
      : label(_label), size(_size), addr(0) {}

  Variable(std::string _label, klee::ref<klee::Expr> expr)
      : label(_label), size(expr->getWidth()), addr(0), is_array(false) {
    add_expr(expr);
  }

  const std::string &get_label() const { return label; }
  bits_t get_size() const { return size; }

  void add_expr(klee::ref<klee::Expr> expr) {
    exprs.push_back(expr);

    BDD::label_t symbol;

    if (!kutil::is_readLSB(expr, symbol)) {
      return;
    }

    symbols.push_back(symbol);
  }

  void set_expr(klee::ref<klee::Expr> expr) {
    assert(exprs.size() == 0);
    add_expr(expr);
  }

  void set_addr(addr_t _addr) { addr = _addr; }

  bool has_expr() const { return exprs.size() > 0; }

  klee::ref<klee::Expr> get_expr() const {
    assert(exprs.size() == 1);
    return exprs[0];
  }

  virtual std::string get_type() const {
    assert(size > 0);
    assert(size % 8 == 0);

    std::stringstream type;

    if (is_primitive_type(size)) {
      type << "uint";
      type << size;
      type << "_t";
    } else {
      assert(addr);
      type << "uint8_t";
    }

    if (addr) {
      type << "*";
    }

    return type.str();
  }

  void set_is_array() { is_array = true; }
  bool get_is_array() const { return is_array; }

  bool match(std::string s) const {
    auto found_it = std::find(symbols.begin(), symbols.end(), s);
    return found_it != symbols.end();
  }

  bool match(klee::ref<klee::Expr> e) const {
    for (auto expr : exprs) {
      auto eq = kutil::solver_toolbox.are_exprs_always_equal(expr, e);

      if (eq) {
        return true;
      }
    }

    return false;
  }

  bool match(addr_t a) const { return addr == a; }

  kutil::solver_toolbox_t::contains_result_t
  contains(klee::ref<klee::Expr> e) const {
    for (auto expr : exprs) {
      auto contains_result = kutil::solver_toolbox.contains(expr, e);

      if (contains_result.contains) {
        return contains_result;
      }
    }

    return kutil::solver_toolbox_t::contains_result_t();
  }
};

struct variable_query_t {
  bool valid;
  std::unique_ptr<Variable> var;
  unsigned offset_bits;

  variable_query_t() : valid(false) {}

  variable_query_t(const Variable &_var, unsigned _offset_bits)
      : valid(true), var(std::unique_ptr<Variable>(new Variable(_var))),
        offset_bits(_offset_bits) {}
};

} // namespace tfhe
} // namespace synthesizer
} // namespace synapse