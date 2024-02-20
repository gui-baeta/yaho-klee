#pragma once

#include "../module.h"

namespace synapse {
namespace targets {
namespace x86_bmv2 {

class VectorReturn : public Module {
private:
  klee::ref<klee::Expr> vector_addr;
  klee::ref<klee::Expr> index;
  klee::ref<klee::Expr> value_addr;
  klee::ref<klee::Expr> value;

public:
  VectorReturn()
      : Module(ModuleType::x86_BMv2_VectorReturn, TargetType::x86_BMv2,
               "VectorReturn") {}

  VectorReturn(BDD::Node_ptr node, klee::ref<klee::Expr> _vector_addr,
               klee::ref<klee::Expr> _index, klee::ref<klee::Expr> _value_addr,
               klee::ref<klee::Expr> _value)
      : Module(ModuleType::x86_BMv2_VectorReturn, TargetType::x86_BMv2,
               "VectorReturn", node),
        vector_addr(_vector_addr), index(_index), value_addr(_value_addr),
        value(_value) {}

private:
  processing_result_t process(const ExecutionPlan &ep,
                              BDD::Node_ptr node) override {
    processing_result_t result;

    auto casted = BDD::cast_node<BDD::Call>(node);

    if (!casted) {
      return result;
    }

    auto call = casted->get_call();

    if (call.function_name == BDD::symbex::FN_VECTOR_RETURN) {
      assert(!call.args[BDD::symbex::FN_VECTOR_ARG_VECTOR].expr.isNull());
      assert(!call.args[BDD::symbex::FN_VECTOR_ARG_INDEX].expr.isNull());
      assert(!call.args[BDD::symbex::FN_VECTOR_ARG_VALUE].expr.isNull());
      assert(!call.args[BDD::symbex::FN_VECTOR_ARG_VALUE].in.isNull());

      auto _vector_addr = call.args[BDD::symbex::FN_VECTOR_ARG_VECTOR].expr;
      auto _index = call.args[BDD::symbex::FN_VECTOR_ARG_INDEX].expr;
      auto _value_addr = call.args[BDD::symbex::FN_VECTOR_ARG_VALUE].expr;
      auto _value = call.args[BDD::symbex::FN_VECTOR_ARG_VALUE].in;

      auto new_module = std::make_shared<VectorReturn>(
          node, _vector_addr, _index, _value_addr, _value);
      auto new_ep = ep.add_leaf(new_module, node->get_next());

      result.module = new_module;
      result.next_eps.push_back(new_ep);
    }

    return result;
  }

public:
  virtual void visit(ExecutionPlanVisitor &visitor,
                     const ExecutionPlanNode *ep_node) const override {
    visitor.visit(ep_node, this);
  }

  virtual Module_ptr clone() const override {
    auto cloned = new VectorReturn(node, vector_addr, index, value_addr, value);
    return std::shared_ptr<Module>(cloned);
  }

  virtual bool equals(const Module *other) const override {
    if (other->get_type() != type) {
      return false;
    }

    auto other_cast = static_cast<const VectorReturn *>(other);

    if (!kutil::solver_toolbox.are_exprs_always_equal(
            vector_addr, other_cast->get_vector_addr())) {
      return false;
    }

    if (!kutil::solver_toolbox.are_exprs_always_equal(
            index, other_cast->get_index())) {
      return false;
    }

    if (!kutil::solver_toolbox.are_exprs_always_equal(
            value_addr, other_cast->get_value_addr())) {
      return false;
    }

    if (!kutil::solver_toolbox.are_exprs_always_equal(
            value, other_cast->get_value())) {
      return false;
    }

    return true;
  }

  const klee::ref<klee::Expr> &get_vector_addr() const { return vector_addr; }
  const klee::ref<klee::Expr> &get_index() const { return index; }
  const klee::ref<klee::Expr> &get_value_addr() const { return value_addr; }
  const klee::ref<klee::Expr> &get_value() const { return value; }
};
} // namespace x86_bmv2
} // namespace targets
} // namespace synapse
