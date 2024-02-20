#pragma once

#include "else.h"
#include "ignore.h"
#include "then.h"
#include "tofino_module.h"

namespace synapse {
namespace targets {
namespace tofino {

class ParserCondition : public TofinoModule {
private:
  klee::ref<klee::Expr> condition;
  bool reject_on_false;

  // This indicates if this custom header validation should not only be applied
  // at the parser, but also at the apply block. This is relevant for NFs whose
  // behavior differs depending on the parsed headers (e.g. TCP vs UDP).
  bool apply_is_valid;
  klee::ref<klee::Expr> discriminated_chunk;

  // Hack: because of Tofino limitations, variable length headers are
  // implemented as headers on a struct. E.g. for IP options:
  //   typedef ipv4_options_h[10] ipv4_options_t
  // However, only headers can be checked for validation. Therefore, instead of
  // checking if the header is valid, we check if its first element is valid. In
  // this case:
  //   ipv4_options[0].isValid()
  bool has_variable_length;

  // The discriminated chunk actually happens if the condition is false.
  // Therefore, we should check if the header is invalid (instead of checking
  // its validity) to generate the on true branch code path.
  bool discriminated_on_false_condition;

public:
  ParserCondition()
      : TofinoModule(ModuleType::Tofino_ParserCondition, "ParserCondition") {}

  ParserCondition(BDD::Node_ptr node, klee::ref<klee::Expr> _condition,
                  bool _reject_on_false,
                  klee::ref<klee::Expr> _discriminated_chunk,
                  bool _has_variable_length,
                  bool _discriminated_on_false_condition)
      : TofinoModule(ModuleType::Tofino_ParserCondition, "ParserCondition",
                     node),
        condition(_condition), reject_on_false(_reject_on_false),
        apply_is_valid(true), discriminated_chunk(_discriminated_chunk),
        has_variable_length(_has_variable_length),
        discriminated_on_false_condition(_discriminated_on_false_condition) {}

  ParserCondition(BDD::Node_ptr node, klee::ref<klee::Expr> _condition,
                  bool _reject_on_false)
      : TofinoModule(ModuleType::Tofino_ParserCondition, "ParserCondition",
                     node),
        condition(_condition), reject_on_false(_reject_on_false),
        apply_is_valid(false) {}

private:
  bool borrow_has_variable_length(BDD::Node_ptr node) const {
    auto call_node = BDD::cast_node<BDD::Call>(node);
    assert(call_node);

    auto call = call_node->get_call();

    assert(call.function_name == BDD::symbex::FN_BORROW_CHUNK);
    assert(!call.args[BDD::symbex::FN_BORROW_CHUNK_ARG_LEN].expr.isNull());

    auto _length = call.args[BDD::symbex::FN_BORROW_CHUNK_ARG_LEN].expr;

    return _length->getKind() != klee::Expr::Kind::Constant;
  }

  klee::ref<klee::Expr> get_chunk_from_borrow(BDD::Node_ptr node) const {
    auto call_node = BDD::cast_node<BDD::Call>(node);
    assert(call_node);

    auto call = call_node->get_call();

    assert(call.function_name == BDD::symbex::FN_BORROW_CHUNK);
    assert(!call.extra_vars[BDD::symbex::FN_BORROW_CHUNK_EXTRA].second.isNull());

    auto _chunk = call.extra_vars[BDD::symbex::FN_BORROW_CHUNK_EXTRA].second;

    return _chunk;
  }

  void commit_module(const ExecutionPlan &ep, BDD::Node_ptr node,
                     Module_ptr new_module, bool build_on_false,
                     processing_result_t &result) const {

    if (build_on_false) {
      auto new_then_module = std::make_shared<Then>(node);
      auto new_else_module = std::make_shared<Else>(node);

      auto casted = BDD::cast_node<BDD::Branch>(node);
      assert(casted);

      auto if_leaf = ExecutionPlan::leaf_t(new_module, nullptr);
      auto then_leaf =
          ExecutionPlan::leaf_t(new_then_module, casted->get_on_true());
      auto else_leaf =
          ExecutionPlan::leaf_t(new_else_module, casted->get_on_false());

      std::vector<ExecutionPlan::leaf_t> if_leaves{if_leaf};
      std::vector<ExecutionPlan::leaf_t> then_else_leaves{then_leaf, else_leaf};

      auto ep_if = ep.add_leaves(if_leaves);
      auto ep_if_then_else = ep_if.add_leaves(then_else_leaves);

      result.next_eps.push_back(ep_if_then_else);
    } else {
      auto new_ep = ep.add_leaf(new_module, node->get_next());
      result.next_eps.push_back(new_ep);
    }

    result.module = new_module;
  }

  void generate_parser_condition(const ExecutionPlan &ep, BDD::Node_ptr node,
                                 klee::ref<klee::Expr> _condition,
                                 bool _reject_on_false,
                                 processing_result_t &result) const {
    auto new_module =
        std::make_shared<ParserCondition>(node, _condition, _reject_on_false);
    commit_module(ep, node, new_module, !_reject_on_false, result);
  }

  void generate_parser_condition_with_header_validation(
      const ExecutionPlan &ep, BDD::Node_ptr node,
      klee::ref<klee::Expr> _condition,
      klee::ref<klee::Expr> _discriminated_chunk, bool _has_variable_length,
      bool _discriminated_on_false_condition,
      processing_result_t &result) const {
    auto new_module = std::make_shared<ParserCondition>(
        node, _condition, false, _discriminated_chunk, _has_variable_length,
        _discriminated_on_false_condition);
    commit_module(ep, node, new_module, true, result);
  }

  processing_result_t process(const ExecutionPlan &ep,
                              BDD::Node_ptr node) override {
    processing_result_t result;

    auto casted = BDD::cast_node<BDD::Branch>(node);

    if (!casted) {
      return result;
    }

    if (!is_parser_condition(node)) {
      return result;
    }

    auto _condition = casted->get_condition();
    auto _parsing_cond = cleanup_parsing_condition(_condition);

    auto lhs = casted->get_on_true();
    auto rhs = casted->get_on_false();

    auto lhs_borrows =
        get_all_functions_after_node(lhs, {BDD::symbex::FN_BORROW_CHUNK}, true);
    auto rhs_borrows =
        get_all_functions_after_node(rhs, {BDD::symbex::FN_BORROW_CHUNK}, true);

    if (lhs_borrows.size() == 0 && rhs_borrows.size() == 0) {
      generate_parser_condition(ep, node, _parsing_cond, false, result);
      return result;
    }

    // This only works because we are working under the assumption that before
    // parsing a header we always perform some kind of checking. Thus, we will
    // never encounter borrows on both sides (directly at least).

    assert(lhs_borrows.size() > 0 || rhs_borrows.size() > 0);
    assert(lhs_borrows.size() != rhs_borrows.size());

    auto lhs_is_discriminating = lhs_borrows.size() > rhs_borrows.size();

    auto discriminating_borrow =
        lhs_is_discriminating ? lhs_borrows[0] : rhs_borrows[0];

    auto not_discriminating_path = lhs_is_discriminating ? rhs : lhs;

    if (!is_parser_drop(not_discriminating_path)) {
      // Check for the header's validity in the apply block

      auto _discriminating_chunk = get_chunk_from_borrow(discriminating_borrow);
      auto _has_variable_length =
          borrow_has_variable_length(discriminating_borrow);
      auto _discriminating_on_false = !lhs_is_discriminating;

      generate_parser_condition_with_header_validation(
          ep, node, _parsing_cond, _discriminating_chunk, _has_variable_length,
          _discriminating_on_false, result);
    } else {
      generate_parser_condition(ep, node, _parsing_cond, true, result);
    }

    return result;
  }

public:
  virtual void visit(ExecutionPlanVisitor &visitor,
                     const ExecutionPlanNode *ep_node) const override {
    visitor.visit(ep_node, this);
  }

  virtual Module_ptr clone() const override {
    auto cloned = new ParserCondition(node, condition, reject_on_false,
                                      discriminated_chunk, has_variable_length,
                                      discriminated_on_false_condition);
    return std::shared_ptr<Module>(cloned);
  }

  virtual bool equals(const Module *other) const override {
    if (other->get_type() != type) {
      return false;
    }

    auto other_cast = static_cast<const ParserCondition *>(other);

    if (!kutil::solver_toolbox.are_exprs_always_equal(condition,
                                                      other_cast->condition)) {
      return false;
    }

    if (reject_on_false != other_cast->reject_on_false) {
      return false;
    }

    if (apply_is_valid != other_cast->apply_is_valid) {
      return false;
    }

    if (!kutil::solver_toolbox.are_exprs_always_equal(
            discriminated_chunk, other_cast->discriminated_chunk)) {
      return false;
    }

    if (has_variable_length != other_cast->has_variable_length) {
      return false;
    }

    if (discriminated_on_false_condition !=
        other_cast->discriminated_on_false_condition) {
      return false;
    }

    return true;
  }

  klee::ref<klee::Expr> get_condition() const { return condition; }

  bool get_reject_on_false() const { return reject_on_false; }
  bool get_apply_is_valid() const { return apply_is_valid; }

  klee::ref<klee::Expr> get_discriminated_chunk() const {
    return discriminated_chunk;
  }

  bool get_has_variable_length() const { return has_variable_length; }

  bool get_discriminated_on_false_condition() const {
    return discriminated_on_false_condition;
  }
};
} // namespace tofino
} // namespace targets
} // namespace synapse
