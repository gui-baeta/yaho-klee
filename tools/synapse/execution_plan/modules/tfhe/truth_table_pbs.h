#pragma once

#include "tfhe_module.h"

#include "else.h"
#include "then.h"

namespace synapse {
namespace targets {
namespace tfhe {

/* If given the following code:
 * let c = 0
 * if a > 5 {
 *    c += 1
 * } else {
 *    c += 2
 * }
 *
 * Should construct code similar to this:
 *
 * cond_val = (a.gt(5))
 * c = cond_val*(c + 1) + (not cond_val)*(c + 2)
 *
 * Where the construction is like so:
 * res = cond_then * (then_block) + (cond_else) * (else_block)
 *
 * If the inputted code has an if with two conditions,
 *  the BDD will actually have nested ifs:
 *
 * For example:
 *
 * if a > 5 && b > 5 {}
 *
 * will actually become
 *
 * if a > 5 {
 *     if b > 5 {}
 * }
 * FIXME WIP: Only able to process then binary conditions
 */
class TruthTablePBS : public tfheModule {
private:
  // Collection of all the binary conditions to evaluate
  klee::ref<klee::Expr> condition;

public:
  TruthTablePBS() : tfheModule(ModuleType::tfhe_TruthTablePBS, "TruthTablePBS") {}

  TruthTablePBS(BDD::Node_ptr node, klee::ref<klee::Expr> _condition)
      : tfheModule(ModuleType::tfhe_TruthTablePBS, "TruthTablePBS", node), condition(_condition) {}

private:
  processing_result_t process(const ExecutionPlan &ep,
                              BDD::Node_ptr node) override {
    processing_result_t result;
    return result; // FIXME Not being used for the time being

    // Check if it's a branch node
    auto casted = BDD::cast_node<BDD::Branch>(node);
    if (!casted) {
      return result;
    }
    // At this point, we know it's a branch

    // We want to store the if _condition
    assert(!casted->get_condition().isNull());
    auto _condition = casted->get_condition();

    // See if this _condition is binary
    // FIXME Doesn't work
//    if (!klee::NotExpr::classof(_condition)) {
//      return result;
//    }

    // See if the then block has a _condition
    //    If it does, check if that _condition is also boolean.

    // TODO See if the else block has a _condition

    auto new_if_module = std::make_shared<TruthTablePBS>(node, _condition);
    auto new_then_module = std::make_shared<Then>(node);
    auto new_else_module = std::make_shared<Else>(node);

    auto if_leaf = ExecutionPlan::leaf_t(new_if_module, nullptr);
    auto then_leaf =
        ExecutionPlan::leaf_t(new_then_module, casted->get_on_true());
    auto else_leaf =
        ExecutionPlan::leaf_t(new_else_module, casted->get_on_false());

    std::vector<ExecutionPlan::leaf_t> if_leaves{if_leaf};
    std::vector<ExecutionPlan::leaf_t> then_else_leaves{then_leaf, else_leaf};

    auto ep_if = ep.add_leaves(if_leaves);
    auto ep_if_then_else = ep_if.add_leaves(then_else_leaves);

    result.module = new_if_module;
    result.next_eps.push_back(ep_if_then_else);

    return result;
  }

public:
  virtual void visit(ExecutionPlanVisitor &visitor,
                     const ExecutionPlanNode *ep_node) const override {
    visitor.visit(ep_node, this);
  }

  virtual Module_ptr clone() const override {
    auto cloned = new TruthTablePBS(node, condition);
    return std::shared_ptr<Module>(cloned);
  }

  virtual bool equals(const Module *other) const override {
    if (other->get_type() != type) {
      return false;
    }

    auto other_cast = static_cast<const TruthTablePBS *>(other);

    if (!kutil::solver_toolbox.are_exprs_always_equal(
            condition, other_cast->get_condition())) {
      return false;
    }

    return true;
  }

  const klee::ref<klee::Expr> &get_condition() const { return condition; }
};
} // namespace tfhe
} // namespace targets
} // namespace synapse
