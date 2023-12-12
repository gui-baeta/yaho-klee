#pragma once

#include "tfhe_module.h"

namespace synapse {
namespace targets {
namespace tfhe {

// TODO Rename this class to SumInConditionalBranch
class TernarySum : public tfheModule {
private:
  std::string expression;

public:
  TernarySum()
      : tfheModule(ModuleType::tfhe_TernarySum, "TernarySum") {}

  TernarySum(BDD::Node_ptr node,
             std::string _expression)
      : tfheModule(ModuleType::tfhe_TernarySum, "TernarySum", node),
        expression(_expression) {}

private:
  klee::ref<klee::Expr> get_original_chunk(const ExecutionPlan &ep,
                                           BDD::Node_ptr node) const {
    auto prev_borrows =
        get_prev_fn(ep, node,
                    std::vector<std::string>{BDD::symbex::FN_BORROW_CHUNK,
                                             BDD::symbex::FN_BORROW_SECRET});
    auto prev_returns = get_prev_fn(ep, node, BDD::symbex::FN_RETURN_CHUNK);

    assert(prev_borrows.size());
    assert(prev_borrows.size() > prev_returns.size());

    auto target = prev_borrows[prev_returns.size()];

    auto call_node = BDD::cast_node<BDD::Call>(target);
    assert(call_node);

    auto call = call_node->get_call();

    assert(call.function_name == BDD::symbex::FN_BORROW_CHUNK ||
           call.function_name == BDD::symbex::FN_BORROW_SECRET);
    assert(
        !call.extra_vars[BDD::symbex::FN_BORROW_CHUNK_EXTRA].second.isNull());

    return call.extra_vars[BDD::symbex::FN_BORROW_CHUNK_EXTRA].second;
  }

  processing_result_t process(const ExecutionPlan &ep,
                              BDD::Node_ptr node) override {
    processing_result_t result;

    return result; // FIXME Just to text the generation of the Execution Plan

    // Check if this is a valid return chunk
    auto casted = BDD::cast_node<BDD::Call>(node);
    if (!casted) {
      return result;
    }
    auto call = casted->get_call();
    if (call.function_name != BDD::symbex::FN_RETURN_CHUNK) {
      return result;
    }
    assert(!call.args[BDD::symbex::FN_BORROW_CHUNK_EXTRA].expr.isNull());
    assert(!call.args[BDD::symbex::FN_BORROW_CHUNK_EXTRA].in.isNull());

    // TODO Check if this is a sum. If not, bail out

    // TODO Check if the previous node is a Conditional node. If not, bail out


    // Get previous Conditional nodes
//    std::vector<Module_ptr> previous_conditionals =
//        get_prev_modules(ep, {ModuleType::tfhe_Conditional});
//
//    // Get only the nearest previous Conditional node
//    Module_ptr previous_conditional = previous_conditionals[0];
//
//    // Build the expression for this arm.
//    auto _current_chunk = call.args[BDD::symbex::FN_BORROW_CHUNK_EXTRA].in;
//    // Is it a concatenation?
//    assert(_current_chunk->getKind() == klee::Expr::Kind::Concat)
//    klee:ref<klee:Expr> left_kid = _current_chunk.getKid(0);
//
//    // Is it an add, between two expression?
//    assert(left_kid->getKind() == klee::Expr::Kind::Add);
//    klee::ref<klee::Expr> left_add_expr = left_kid.getKid(0);
//
//    std::string value_str = left_add_expr.toString();
//    klee::ref<klee::Expr> right_add_expr = left_kid.getKid(1);
//    // Is it reading a chunk?. Ex: packet_chunks[2]
//    assert(right_add_expr->getKind() == klee::Expr::Kind::Read);
//    // FIXME Should I cast it this way?
//    auto read_expr = static_cast<klee::ReadExpr *>(right_add_expr);
//    std::string index_str = read_expr.index.toString();

//    auto _chunk = call.args[BDD::symbex::FN_BORROW_CHUNK_EXTRA].expr;
//    auto _current_chunk = call.args[BDD::symbex::FN_BORROW_CHUNK_EXTRA].in;
//    auto _original_chunk = get_original_chunk(ep, node);
//
//    auto _chunk_addr = kutil::expr_addr_to_obj_addr(_chunk);
//    auto _modifications = build_modifications(_original_chunk, _current_chunk);

//    // Check if the Conditional node was already visited by the other arm
//    if (previous_conditional->already_visited) {
//
//    }
//
//    auto new_module = std::make_shared<TernarySum>(
//        node, value_str + "c" + index_str);
//    auto new_ep = ep.add_leaves(new_module, node->get_next());
//
//    result.module = new_module;
//    result.next_eps.push_back(new_ep);

    return result;
  }

public:
  virtual void visit(ExecutionPlanVisitor &visitor,
                     const ExecutionPlanNode *ep_node) const override {
    visitor.visit(ep_node, this);
  }

  virtual Module_ptr clone() const override {
    auto cloned =
        new TernarySum(node, this->get_expression());
    return std::shared_ptr<Module>(cloned);
  }

  virtual bool equals(const Module *other) const override {
    if (other->get_type() != type) {
      return false;
    }

    auto other_cast = static_cast<const TernarySum *>(other);

    if (!this->get_expression().compare(other_cast->get_expression())) {
      return false;
    }

    return true;
  }

  const std::string &get_expression() const { return this->expression; }
};
} // namespace tfhe
} // namespace targets
} // namespace synapse
