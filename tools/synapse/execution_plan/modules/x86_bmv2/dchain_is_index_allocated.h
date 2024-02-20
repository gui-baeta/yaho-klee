#pragma once

#include "../module.h"

namespace synapse {
namespace targets {
namespace x86_bmv2 {

class DchainIsIndexAllocated : public Module {
private:
  klee::ref<klee::Expr> dchain_addr;
  klee::ref<klee::Expr> index;
  klee::ref<klee::Expr> is_allocated;

  BDD::symbols_t generated_symbols;

public:
  DchainIsIndexAllocated()
      : Module(ModuleType::x86_BMv2_DchainIsIndexAllocated,
               TargetType::x86_BMv2, "DchainIsIndexAllocated") {}

  DchainIsIndexAllocated(BDD::Node_ptr node, klee::ref<klee::Expr> _dchain_addr,
                         klee::ref<klee::Expr> _index,
                         klee::ref<klee::Expr> _is_allocated,
                         BDD::symbols_t _generated_symbols)
      : Module(ModuleType::x86_BMv2_DchainIsIndexAllocated,
               TargetType::x86_BMv2, "DchainIsIndexAllocated", node),
        dchain_addr(_dchain_addr), index(_index), is_allocated(_is_allocated),
        generated_symbols(_generated_symbols) {}

private:
  processing_result_t process(const ExecutionPlan &ep,
                              BDD::Node_ptr node) override {
    processing_result_t result;

    auto casted = BDD::cast_node<BDD::Call>(node);

    if (!casted) {
      return result;
    }

    auto call = casted->get_call();

    if (call.function_name == BDD::symbex::FN_DCHAIN_IS_ALLOCATED) {
      assert(!call.args[BDD::symbex::FN_DCHAIN_ARG_CHAIN].expr.isNull());
      assert(!call.args[BDD::symbex::FN_DCHAIN_ARG_INDEX].expr.isNull());
      assert(!call.ret.isNull());

      auto _dchain_addr = call.args[BDD::symbex::FN_DCHAIN_ARG_CHAIN].expr;
      auto _index = call.args[BDD::symbex::FN_DCHAIN_ARG_INDEX].expr;
      auto _is_allocated = call.ret;
      auto _generated_symbols = casted->get_local_generated_symbols();

      auto new_module = std::make_shared<DchainIsIndexAllocated>(
          node, _dchain_addr, _index, _is_allocated, _generated_symbols);
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
    auto cloned = new DchainIsIndexAllocated(node, dchain_addr, index,
                                             is_allocated, generated_symbols);
    return std::shared_ptr<Module>(cloned);
  }

  virtual bool equals(const Module *other) const override {
    if (other->get_type() != type) {
      return false;
    }

    auto other_cast = static_cast<const DchainIsIndexAllocated *>(other);

    if (!kutil::solver_toolbox.are_exprs_always_equal(
            dchain_addr, other_cast->get_dchain_addr())) {
      return false;
    }

    if (!kutil::solver_toolbox.are_exprs_always_equal(
            index, other_cast->get_index())) {
      return false;
    }

    if (!kutil::solver_toolbox.are_exprs_always_equal(
            is_allocated, other_cast->get_is_allocated())) {
      return false;
    }

    if (generated_symbols != other_cast->generated_symbols) {
      return false;
    }

    return true;
  }

  const klee::ref<klee::Expr> &get_dchain_addr() const { return dchain_addr; }
  const klee::ref<klee::Expr> &get_index() const { return index; }
  const klee::ref<klee::Expr> &get_is_allocated() const { return is_allocated; }

  const BDD::symbols_t &get_generated_symbols() const {
    return generated_symbols;
  }
};
} // namespace x86_bmv2
} // namespace targets
} // namespace synapse
