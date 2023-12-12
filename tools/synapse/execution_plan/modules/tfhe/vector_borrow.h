#pragma once

#include "tfhe_module.h"

namespace synapse {
namespace targets {
namespace tfhe {

class VectorBorrow : public tfheModule {
private:
  addr_t vector_addr;
  klee::ref<klee::Expr> index;
  addr_t value_out;
  klee::ref<klee::Expr> borrowed_cell;

public:
  VectorBorrow() : tfheModule(ModuleType::tfhe_VectorBorrow, "VectorBorrow") {}

  VectorBorrow(BDD::Node_ptr node, addr_t _vector_addr,
               klee::ref<klee::Expr> _index, addr_t _value_out,
               klee::ref<klee::Expr> _borrowed_cell)
      : tfheModule(ModuleType::tfhe_VectorBorrow, "VectorBorrow", node),
        vector_addr(_vector_addr), index(_index), value_out(_value_out),
        borrowed_cell(_borrowed_cell) {}

private:
  processing_result_t process(const ExecutionPlan &ep,
                              BDD::Node_ptr node) override {
    processing_result_t result;

    auto casted = BDD::cast_node<BDD::Call>(node);

    if (!casted) {
      return result;
    }

    auto call = casted->get_call();

    if (call.function_name == BDD::symbex::FN_VECTOR_BORROW) {
      assert(!call.args[BDD::symbex::FN_VECTOR_ARG_VECTOR].expr.isNull());
      assert(!call.args[BDD::symbex::FN_VECTOR_ARG_INDEX].expr.isNull());
      assert(!call.args[BDD::symbex::FN_VECTOR_ARG_OUT].out.isNull());
      assert(!call.extra_vars[BDD::symbex::FN_VECTOR_EXTRA].second.isNull());

      auto _vector = call.args[BDD::symbex::FN_VECTOR_ARG_VECTOR].expr;
      auto _index = call.args[BDD::symbex::FN_VECTOR_ARG_INDEX].expr;
      auto _value_out = call.args[BDD::symbex::FN_VECTOR_ARG_OUT].out;
      auto _borrowed_cell = call.extra_vars[BDD::symbex::FN_VECTOR_EXTRA].second;

      auto _vector_addr = kutil::expr_addr_to_obj_addr(_vector);
      auto _value_out_addr = kutil::expr_addr_to_obj_addr(_value_out);

      save_vector(ep, _vector_addr);

      auto new_module = std::make_shared<VectorBorrow>(
          node, _vector_addr, _index, _value_out_addr, _borrowed_cell);
      auto new_ep = ep.add_leaves(new_module, node->get_next());

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
        new VectorBorrow(node, vector_addr, index, value_out, borrowed_cell);
    return std::shared_ptr<Module>(cloned);
  }

  virtual bool equals(const Module *other) const override {
    if (other->get_type() != type) {
      return false;
    }

    auto other_cast = static_cast<const VectorBorrow *>(other);

    if (vector_addr != other_cast->get_vector_addr()) {
      return false;
    }

    if (!kutil::solver_toolbox.are_exprs_always_equal(
            index, other_cast->get_index())) {
      return false;
    }

    if (value_out != other_cast->get_value_out()) {
      return false;
    }

    if (!kutil::solver_toolbox.are_exprs_always_equal(
            borrowed_cell, other_cast->get_borrowed_cell())) {
      return false;
    }

    return true;
  }

  addr_t get_vector_addr() const { return vector_addr; }
  const klee::ref<klee::Expr> &get_index() const { return index; }
  addr_t get_value_out() const { return value_out; }
  const klee::ref<klee::Expr> &get_borrowed_cell() const {
    return borrowed_cell;
  }
};
} // namespace tfhe
} // namespace targets
} // namespace synapse
