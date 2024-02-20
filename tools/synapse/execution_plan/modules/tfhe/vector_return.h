#pragma once

#include "tfhe_module.h"

namespace synapse {
namespace targets {
namespace tfhe {

class VectorReturn : public tfheModule {
private:
  addr_t vector_addr;
  klee::ref<klee::Expr> index;
  addr_t value_addr;
  std::vector<modification_t> modifications;

public:
  VectorReturn() : tfheModule(ModuleType::tfhe_VectorReturn, "VectorReturn") {}

  VectorReturn(BDD::Node_ptr node, addr_t _vector_addr,
               klee::ref<klee::Expr> _index, addr_t _value_addr,
               const std::vector<modification_t> &_modifications)
      : tfheModule(ModuleType::tfhe_VectorReturn, "VectorReturn", node),
        vector_addr(_vector_addr), index(_index), value_addr(_value_addr),
        modifications(_modifications) {}

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

      auto _vector = call.args[BDD::symbex::FN_VECTOR_ARG_VECTOR].expr;
      auto _index = call.args[BDD::symbex::FN_VECTOR_ARG_INDEX].expr;
      auto _value_addr_expr = call.args[BDD::symbex::FN_VECTOR_ARG_VALUE].expr;
      auto _value = call.args[BDD::symbex::FN_VECTOR_ARG_VALUE].in;

      auto _vector_addr = kutil::expr_addr_to_obj_addr(_vector);
      auto _value_addr = kutil::expr_addr_to_obj_addr(_value_addr_expr);

      auto _original_value = get_original_vector_value(ep, node, _vector_addr);
      auto _modifications = build_modifications(_original_value, _value);

      save_vector(ep, _vector_addr);

      auto new_module = std::make_shared<VectorReturn>(
          node, _vector_addr, _index, _value_addr, _modifications);
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
    auto cloned =
        new VectorReturn(node, vector_addr, index, value_addr, modifications);
    return std::shared_ptr<Module>(cloned);
  }

  virtual bool equals(const Module *other) const override {
    if (other->get_type() != type) {
      return false;
    }

    auto other_cast = static_cast<const VectorReturn *>(other);

    if (vector_addr != other_cast->get_vector_addr()) {
      return false;
    }

    if (!kutil::solver_toolbox.are_exprs_always_equal(
            index, other_cast->get_index())) {
      return false;
    }

    if (value_addr != other_cast->get_value_addr()) {
      return false;
    }

    auto other_modifications = other_cast->get_modifications();

    if (modifications.size() != other_modifications.size()) {
      return false;
    }

    for (unsigned i = 0; i < modifications.size(); i++) {
      auto modification = modifications[i];
      auto other_modification = other_modifications[i];

      if (modification.byte != other_modification.byte) {
        return false;
      }

      if (!kutil::solver_toolbox.are_exprs_always_equal(
              modification.expr, other_modification.expr)) {
        return false;
      }
    }

    return true;
  }

  addr_t get_vector_addr() const { return vector_addr; }
  klee::ref<klee::Expr> get_index() const { return index; }
  addr_t get_value_addr() const { return value_addr; }

  const std::vector<modification_t> &get_modifications() const {
    return modifications;
  }
};
} // namespace tfhe
} // namespace targets
} // namespace synapse
