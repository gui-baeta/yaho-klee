#include "transpiler.h"
#include "../../../../log.h"
#include "../util.h"
#include "klee-util.h"
#include "tfhe_generator.h"

#include <assert.h>

namespace synapse {
namespace synthesizer {
namespace tfhe {

class InternalTranspiler : public klee::ExprVisitor::ExprVisitor {
private:
  tfheGenerator &generator;
  Transpiler &transpiler;
  std::stringstream code;

public:
  InternalTranspiler(tfheGenerator &_generator, Transpiler &_transpiler)
      : generator(_generator), transpiler(_transpiler) {}

  std::string get() const { return code.str(); }

private:
  std::string transpile(const klee::ref<klee::Expr> &expr);
  klee::ExprVisitor::Action visitRead(const klee::ReadExpr &e);
  klee::ExprVisitor::Action visitSelect(const klee::SelectExpr &e);
  klee::ExprVisitor::Action visitConcat(const klee::ConcatExpr &e);
  klee::ExprVisitor::Action visitExtract(const klee::ExtractExpr &e);
  klee::ExprVisitor::Action visitZExt(const klee::ZExtExpr &e);
  klee::ExprVisitor::Action visitSExt(const klee::SExtExpr &e);
  klee::ExprVisitor::Action visitAdd(const klee::AddExpr &e);
  klee::ExprVisitor::Action visitSub(const klee::SubExpr &e);
  klee::ExprVisitor::Action visitMul(const klee::MulExpr &e);
  klee::ExprVisitor::Action visitUDiv(const klee::UDivExpr &e);
  klee::ExprVisitor::Action visitSDiv(const klee::SDivExpr &e);
  klee::ExprVisitor::Action visitURem(const klee::URemExpr &e);
  klee::ExprVisitor::Action visitSRem(const klee::SRemExpr &e);
  klee::ExprVisitor::Action visitNot(const klee::NotExpr &e);
  klee::ExprVisitor::Action visitAnd(const klee::AndExpr &e);
  klee::ExprVisitor::Action visitOr(const klee::OrExpr &e);
  klee::ExprVisitor::Action visitXor(const klee::XorExpr &e);
  klee::ExprVisitor::Action visitShl(const klee::ShlExpr &e);
  klee::ExprVisitor::Action visitLShr(const klee::LShrExpr &e);
  klee::ExprVisitor::Action visitAShr(const klee::AShrExpr &e);
  klee::ExprVisitor::Action visitEq(const klee::EqExpr &e);
  klee::ExprVisitor::Action visitNe(const klee::NeExpr &e);
  klee::ExprVisitor::Action visitUlt(const klee::UltExpr &e);
  klee::ExprVisitor::Action visitUle(const klee::UleExpr &e);
  klee::ExprVisitor::Action visitUgt(const klee::UgtExpr &e);
  klee::ExprVisitor::Action visitUge(const klee::UgeExpr &e);
  klee::ExprVisitor::Action visitSlt(const klee::SltExpr &e);
  klee::ExprVisitor::Action visitSle(const klee::SleExpr &e);
  klee::ExprVisitor::Action visitSgt(const klee::SgtExpr &e);
  klee::ExprVisitor::Action visitSge(const klee::SgeExpr &e);
};

std::pair<bool, std::string>
Transpiler::try_transpile_constant(const klee::ref<klee::Expr> &expr) const {
  auto result = std::pair<bool, std::string>();
  auto is_constant = kutil::is_constant(expr);

  if (!is_constant) {
    return result;
  }

  result.first = true;

  auto is_constant_signed = kutil::is_constant_signed(expr);

  if (is_constant_signed) {
    auto value = kutil::get_constant_signed(expr);
    result.second = std::to_string(value);
  } else {
    auto value = kutil::solver_toolbox.value_from_expr(expr);
    result.second = std::to_string(value);
  }

  return result;
}

std::pair<bool, std::string>
Transpiler::try_transpile_variable(const klee::ref<klee::Expr> &expr) const {
  auto result = std::pair<bool, std::string>();
  auto variable = generator.search_variable(expr);

  if (!variable.valid) {
    return result;
  }

  std::stringstream transpilation_builder;
  auto label = variable.var->get_label();
  auto size_bits = expr->getWidth();

  if (variable.offset_bits > 0 || size_bits < variable.var->get_size()) {
    auto is_primitive = is_primitive_type(variable.var->get_size());

    if (is_primitive && !variable.var->get_is_array()) {
      auto masked = mask(label, variable.offset_bits, size_bits);
      transpilation_builder << masked;
    } else {
      assert(is_primitive_type(size_bits));
      assert(variable.offset_bits % 8 == 0);

      if (size_bits != 8) {
        transpilation_builder << "(";
        transpilation_builder << size_to_type(size_bits);
        transpilation_builder << ")";
      }

      transpilation_builder << "((uint8_t*)";

      if (!variable.var->get_is_array()) {
        transpilation_builder << "&";
      }

      transpilation_builder << variable.var->get_label();
      transpilation_builder << ")[";
      transpilation_builder << variable.offset_bits / 8;
      transpilation_builder << "]";
    }
  } else {
    if (variable.var->get_is_array()) {
      transpilation_builder << "*";
      transpilation_builder << "(";
      transpilation_builder << size_to_type(size_bits);
      transpilation_builder << "*)";
    }

    transpilation_builder << label;
  }

  result.first = true;
  result.second = transpilation_builder.str();

  return result;
}

std::string Transpiler::transpile(const klee::ref<klee::Expr> &expr) {
  auto const_result = try_transpile_constant(expr);

  if (const_result.first) {
    return const_result.second;
  }

  auto simplified = kutil::simplify(expr);
  auto variable_result = try_transpile_variable(simplified);

  if (variable_result.first) {
    return variable_result.second;
  }

  auto transpiler = InternalTranspiler(generator, *this);
  transpiler.visit(simplified);

  auto code = transpiler.get();
  assert(code.size());

  return code;
}

std::string Transpiler::size_to_type(bits_t size, bool is_signed) const {
  assert(size > 0);
  assert(size % 8 == 0);

  if (is_primitive_type(size)) {
    std::stringstream type_builder;

    if (!is_signed) {
      type_builder << "u";
    }
    type_builder << "int";
    type_builder << size;
    type_builder << "_t";
    return type_builder.str();
  }

  return "bytes_t";
}

/**
 * Applies a bitmask to a given expression, retaining only the specified bits.
 *
 * @param expr The original expression to be masked.
 * @param offset The number of bits to shift the expression to the right before masking.
 * @param size The size (number of bits) of the bitmask to be applied.
 * @return A C-style expression representing the masked result.
 *
 * This function constructs a masked expression by applying a bitmask to the
 * original expression. The bitmask is created based on the provided offset
 * and size, ensuring that only the relevant bits are retained. The resulting
 * expression is suitable for bitwise operations or manipulation of binary data.
 *
 * Example:
 * \code{.cpp}
 * std::string originalExpr = "some_expression";
 * std::string maskedExpr = transpiler.mask(originalExpr, 2, 4);
 * // Result: "((uint32_t)((some_expression) >> 2) & 0xFu)"
 * \endcode
 */
std::string Transpiler::mask(std::string expr, bits_t offset,
                             bits_t size) const {
  std::stringstream mask_builder;

  auto type = size_to_type(size);
  auto mask = 0llu;

  for (auto b = 0u; b < size; b++) {
    mask <<= 1;
    mask |= 1;
  }

  assert(mask > 0);

  mask_builder << "(";

  mask_builder << "(";
  mask_builder << type;
  mask_builder << ")";

  mask_builder << "(";

  mask_builder << "(";
  if (offset > 0) {
    mask_builder << "(";
    mask_builder << expr;
    mask_builder << ")";
    mask_builder << " >> ";
    mask_builder << offset;
  } else {
    mask_builder << expr;
  }

  mask_builder << ")";
  mask_builder << " & 0x";
  mask_builder << std::hex;
  mask_builder << mask;
  mask_builder << std::dec;
  mask_builder << "ull";
  mask_builder << ")";

  mask_builder << ")";

  return mask_builder.str();
}

std::string InternalTranspiler::transpile(const klee::ref<klee::Expr> &expr) {
  return transpiler.transpile(expr);
}

klee::ExprVisitor::Action
InternalTranspiler::visitRead(const klee::ReadExpr &e) {
  auto eref = const_cast<klee::ReadExpr *>(&e);
  auto expr_width = eref->getWidth();

  auto symbol = kutil::get_symbol(eref);
  auto variable = generator.search_variable(symbol.second);

  std::cout << std::endl << "Visiting a read" << std::endl;

  if (!variable.valid) {
    Log::err() << "Unknown variable with symbol " << symbol.second << "\n";
    exit(1);
  }

  auto var_width = variable.var->get_size();
  auto var_label = variable.var->get_label();

  assert(kutil::is_constant(e.index));

  auto index = kutil::solver_toolbox.value_from_expr(e.index);

  if (index == 0 && var_width == expr_width) {
    code << var_label;
    return klee::ExprVisitor::Action::skipChildren();
  }

  assert(expr_width < var_width);

  auto masked = transpiler.mask(var_label, index * 8, 8);
  code << masked;

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action
InternalTranspiler::visitSelect(const klee::SelectExpr &e) {
  klee::ref<klee::Expr> eref = const_cast<klee::SelectExpr *>(&e);
  std::cerr << kutil::expr_to_string(eref) << "\n";
  assert(false && "TODO");
}

klee::ExprVisitor::Action
InternalTranspiler::visitConcat(const klee::ConcatExpr &e) {
  auto eref = const_cast<klee::ConcatExpr *>(&e);

  if (kutil::is_readLSB(eref)) {
    auto symbol = kutil::get_symbol(eref);
    auto variable = generator.search_variable(symbol.second);

    std::cout << std::endl << "Visiting a concat" << std::endl;

    if (!variable.valid) {
      Log::err() << "Unknown variable with symbol " << symbol.second << "\n";
      exit(1);
    }

    assert(variable.offset_bits == 0);

    auto expr_width = eref->getWidth();
    auto var_width = variable.var->get_size();

    if (expr_width != var_width) {
      auto max_width = std::max(expr_width, var_width);
      code << "((";
      code << transpiler.size_to_type(max_width);
      code << ")(";
      code << variable.var->get_label();
      code << "))";
    } else {
      code << variable.var->get_label();
    }

    return klee::ExprVisitor::Action::skipChildren();
  }

  std::cerr << kutil::expr_to_string(eref) << "\n";
  assert(false && "TODO");
}

klee::ExprVisitor::Action
InternalTranspiler::visitExtract(const klee::ExtractExpr &e) {
  auto expr = e.expr;
  auto offset = e.offset;
  auto expr_size = expr->getWidth();
  auto size = e.width;

  auto eref = const_cast<klee::ExtractExpr *>(&e);
  auto variable_result = transpiler.try_transpile_variable(eref);

  if (variable_result.first) {
    code << variable_result.second;
    return klee::ExprVisitor::Action::skipChildren();
  }

  // capture (Extract 0 (ZExt w8 X))
  if (offset == 0 && expr->getKind() == klee::Expr::Kind::ZExt &&
      expr_size == klee::Expr::Int8) {
    assert(expr->getNumKids() == 1);
    auto zextended_expr = expr->getKid(0);
    code << transpile(zextended_expr);
    return klee::ExprVisitor::Action::skipChildren();
  }

  if (is_primitive_type(expr_size)) {
    auto transpiled = transpile(expr);
    auto masked = transpiler.mask(transpiled, offset, size);
    code << masked;
    return klee::ExprVisitor::Action::skipChildren();
  }

  if (expr->getKind() != klee::Expr::Kind::Concat) {
    klee::ref<klee::Expr> eref = const_cast<klee::ExtractExpr *>(&e);
    std::cerr << kutil::expr_to_string(eref) << "\n";
    assert(false && "TODO");
  }

  assert(size == klee::Expr::Int8 && "TODO others");

  // concats
  while (expr->getKind() == klee::Expr::Kind::Concat) {
    auto lhs = expr->getKid(0);
    auto rhs = expr->getKid(1);

    auto lhs_size = lhs->getWidth();
    auto rhs_size = rhs->getWidth();

    // Either completely on the right side, or completely on the left side
    assert(rhs_size >= offset + size ||
           (offset >= rhs_size && lhs_size >= (offset - rhs_size) + size));

    if (rhs_size >= offset + size) {
      expr = rhs;
    } else {
      offset -= rhs_size;
      expr = lhs;
    }
  }

  assert(!expr.isNull());
  assert(size <= expr->getWidth());

  auto new_extract =
      kutil::solver_toolbox.exprBuilder->Extract(expr, offset, size);

  code << transpile(new_extract);
  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action
InternalTranspiler::visitZExt(const klee::ZExtExpr &e) {
  auto sz = e.getWidth();
  auto expr = e.getKid(0);
  assert(sz % 8 == 0);

  code << "((";
  code << transpiler.size_to_type(sz);
  code << ")(";
  code << transpile(expr);
  code << "))";

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action
InternalTranspiler::visitSExt(const klee::SExtExpr &e) {
  auto sz = e.getWidth();
  auto expr = e.getKid(0);
  assert(sz % 8 == 0);

  code << "((";
  code << transpiler.size_to_type(sz, true);
  code << ")(";
  code << transpile(expr);
  code << "))";

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action InternalTranspiler::visitAdd(const klee::AddExpr &e) {
  assert(e.getNumKids() == 2);

  auto lhs = e.getKid(0);
  auto rhs = e.getKid(1);

  auto lhs_parsed = transpile(lhs);
  auto rhs_parsed = transpile(rhs);

  code << "(" << lhs_parsed << ")";
  code << " + ";
  code << "(" << rhs_parsed << ")";

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action InternalTranspiler::visitSub(const klee::SubExpr &e) {
  assert(e.getNumKids() == 2);

  auto lhs = e.getKid(0);
  auto rhs = e.getKid(1);

  auto lhs_parsed = transpile(lhs);
  auto rhs_parsed = transpile(rhs);

  code << "(" << lhs_parsed << ")";
  code << " - ";
  code << "(" << rhs_parsed << ")";

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action InternalTranspiler::visitMul(const klee::MulExpr &e) {
  assert(e.getNumKids() == 2);

  auto lhs = e.getKid(0);
  auto rhs = e.getKid(1);

  auto lhs_parsed = transpile(lhs);
  auto rhs_parsed = transpile(rhs);

  code << "(" << lhs_parsed << ")";
  code << " * ";
  code << "(" << rhs_parsed << ")";

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action
InternalTranspiler::visitUDiv(const klee::UDivExpr &e) {
  assert(e.getNumKids() == 2);

  auto lhs = e.getKid(0);
  auto rhs = e.getKid(1);

  auto lhs_parsed = transpile(lhs);
  auto rhs_parsed = transpile(rhs);

  code << "(" << lhs_parsed << ")";
  code << " / ";
  code << "(" << rhs_parsed << ")";

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action
InternalTranspiler::visitSDiv(const klee::SDivExpr &e) {
  klee::ref<klee::Expr> eref = const_cast<klee::SDivExpr *>(&e);
  std::cerr << kutil::expr_to_string(eref) << "\n";
  assert(false && "TODO");
}

klee::ExprVisitor::Action
InternalTranspiler::visitURem(const klee::URemExpr &e) {
  assert(e.getNumKids() == 2);

  auto lhs = e.getKid(0);
  auto rhs = e.getKid(1);

  auto lhs_parsed = transpile(lhs);
  auto rhs_parsed = transpile(rhs);

  code << "(" << lhs_parsed << ")";
  code << " % ";
  code << "(" << rhs_parsed << ")";

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action
InternalTranspiler::visitSRem(const klee::SRemExpr &e) {
  klee::ref<klee::Expr> eref = const_cast<klee::SRemExpr *>(&e);
  std::cerr << kutil::expr_to_string(eref) << "\n";
  assert(false && "TODO");
}

klee::ExprVisitor::Action InternalTranspiler::visitNot(const klee::NotExpr &e) {
  assert(e.getNumKids() == 1);

  auto arg = e.getKid(0);
  auto arg_parsed = transpile(arg);

  code << "!(" << arg_parsed << ")";

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action InternalTranspiler::visitAnd(const klee::AndExpr &e) {
  assert(e.getNumKids() == 2);

  auto lhs = e.getKid(0);
  auto rhs = e.getKid(1);

  auto lhs_parsed = transpile(lhs);
  auto rhs_parsed = transpile(rhs);

  code << "(" << lhs_parsed << ")";
  code << " & ";
  code << "(" << rhs_parsed << ")";

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action InternalTranspiler::visitOr(const klee::OrExpr &e) {
  assert(e.getNumKids() == 2);

  auto lhs = e.getKid(0);
  auto rhs = e.getKid(1);

  auto lhs_parsed = transpile(lhs);
  auto rhs_parsed = transpile(rhs);

  code << "(" << lhs_parsed << ")";
  code << " | ";
  code << "(" << rhs_parsed << ")";

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action InternalTranspiler::visitXor(const klee::XorExpr &e) {
  assert(e.getNumKids() == 2);

  auto lhs = e.getKid(0);
  auto rhs = e.getKid(1);

  auto lhs_parsed = transpile(lhs);
  auto rhs_parsed = transpile(rhs);

  code << "(" << lhs_parsed << ")";
  code << " ^ ";
  code << "(" << rhs_parsed << ")";

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action InternalTranspiler::visitShl(const klee::ShlExpr &e) {
  assert(e.getNumKids() == 2);

  auto lhs = e.getKid(0);
  auto rhs = e.getKid(1);

  auto lhs_parsed = transpile(lhs);
  auto rhs_parsed = transpile(rhs);

  code << "(" << lhs_parsed << ")";
  code << " << ";
  code << "(" << rhs_parsed << ")";

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action
InternalTranspiler::visitLShr(const klee::LShrExpr &e) {
  assert(e.getNumKids() == 2);

  auto lhs = e.getKid(0);
  auto rhs = e.getKid(1);

  auto lhs_parsed = transpile(lhs);
  auto rhs_parsed = transpile(rhs);

  code << "(" << lhs_parsed << ")";
  code << " >> ";
  code << "(" << rhs_parsed << ")";

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action
InternalTranspiler::visitAShr(const klee::AShrExpr &e) {
  // FIXME: this should be different than the logical shift

  assert(e.getNumKids() == 2);

  auto lhs = e.getKid(0);
  auto rhs = e.getKid(1);

  auto lhs_parsed = transpile(lhs);
  auto rhs_parsed = transpile(rhs);

  code << "(" << lhs_parsed << ")";
  code << " >> ";
  code << "(" << rhs_parsed << ")";

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action InternalTranspiler::visitEq(const klee::EqExpr &e) {
  assert(e.getNumKids() == 2);

  auto lhs = e.getKid(0);
  auto rhs = e.getKid(1);

  auto lhs_parsed = transpile(lhs);
  auto rhs_parsed = transpile(rhs);

  code << "(" << lhs_parsed << ")";
  code << " == ";
  code << "(" << rhs_parsed << ")";

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action InternalTranspiler::visitNe(const klee::NeExpr &e) {
  assert(e.getNumKids() == 2);

  auto lhs = e.getKid(0);
  auto rhs = e.getKid(1);

  auto lhs_parsed = transpile(lhs);
  auto rhs_parsed = transpile(rhs);

  code << "(" << lhs_parsed << ")";
  code << " != ";
  code << "(" << rhs_parsed << ")";

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action InternalTranspiler::visitUlt(const klee::UltExpr &e) {
  assert(e.getNumKids() == 2);

  auto lhs = e.getKid(0);
  auto rhs = e.getKid(1);

  auto lhs_parsed = transpile(lhs);
  auto rhs_parsed = transpile(rhs);

  code << "(" << lhs_parsed << ")";
  code << " < ";
  code << "(" << rhs_parsed << ")";

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action InternalTranspiler::visitUle(const klee::UleExpr &e) {
  assert(e.getNumKids() == 2);

  auto lhs = e.getKid(0);
  auto rhs = e.getKid(1);

  auto lhs_parsed = transpile(lhs);
  auto rhs_parsed = transpile(rhs);

  code << "(" << lhs_parsed << ")";
  code << " <= ";
  code << "(" << rhs_parsed << ")";

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action InternalTranspiler::visitUgt(const klee::UgtExpr &e) {
  assert(e.getNumKids() == 2);

  auto lhs = e.getKid(0);
  auto rhs = e.getKid(1);

  auto lhs_parsed = transpile(lhs);
  auto rhs_parsed = transpile(rhs);

  code << "(" << lhs_parsed << ")";
  code << " > ";
  code << "(" << rhs_parsed << ")";

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action InternalTranspiler::visitUge(const klee::UgeExpr &e) {
  assert(e.getNumKids() == 2);

  auto lhs = e.getKid(0);
  auto rhs = e.getKid(1);

  auto lhs_parsed = transpile(lhs);
  auto rhs_parsed = transpile(rhs);

  code << "(" << lhs_parsed << ")";
  code << " >= ";
  code << "(" << rhs_parsed << ")";

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action InternalTranspiler::visitSlt(const klee::SltExpr &e) {
  assert(e.getNumKids() == 2);

  auto lhs = e.getKid(0);
  auto rhs = e.getKid(1);

  auto lhs_parsed = transpile(lhs);
  auto rhs_parsed = transpile(rhs);

  code << "(" << lhs_parsed << ")";
  code << " < ";
  code << "(" << rhs_parsed << ")";

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action InternalTranspiler::visitSle(const klee::SleExpr &e) {
  assert(e.getNumKids() == 2);

  auto lhs = e.getKid(0);
  auto rhs = e.getKid(1);

  auto lhs_parsed = transpile(lhs);
  auto rhs_parsed = transpile(rhs);

  code << "(" << lhs_parsed << ")";
  code << " <= ";
  code << "(" << rhs_parsed << ")";

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action InternalTranspiler::visitSgt(const klee::SgtExpr &e) {
  assert(e.getNumKids() == 2);

  auto lhs = e.getKid(0);
  auto rhs = e.getKid(1);

  auto lhs_parsed = transpile(lhs);
  auto rhs_parsed = transpile(rhs);

  code << "(" << lhs_parsed << ")";
  code << " > ";
  code << "(" << rhs_parsed << ")";

  return klee::ExprVisitor::Action::skipChildren();
}

klee::ExprVisitor::Action InternalTranspiler::visitSge(const klee::SgeExpr &e) {
  assert(e.getNumKids() == 2);

  auto lhs = e.getKid(0);
  auto rhs = e.getKid(1);

  auto lhs_parsed = transpile(lhs);
  auto rhs_parsed = transpile(rhs);

  code << "(" << lhs_parsed << ")";
  code << " >= ";
  code << "(" << rhs_parsed << ")";

  return klee::ExprVisitor::Action::skipChildren();
}

} // namespace tfhe
} // namespace synthesizer
} // namespace synapse