#pragma once

#include "../module.h"
#include "memory_bank.h"

namespace synapse {
namespace targets {
namespace x86_tofino {

class DchainFreeIndex : public Module {
private:
  addr_t dchain_addr;
  klee::ref<klee::Expr> index;

public:
  DchainFreeIndex()
      : Module(ModuleType::x86_Tofino_DchainFreeIndex, TargetType::x86_Tofino,
               "DchainFreeIndex") {}

  DchainFreeIndex(BDD::Node_ptr node, addr_t _dchain_addr,
                  klee::ref<klee::Expr> _index)
      : Module(ModuleType::x86_Tofino_DchainFreeIndex, TargetType::x86_Tofino,
               "DchainFreeIndex", node),
        dchain_addr(_dchain_addr), index(_index) {}

private:
  processing_result_t process(const ExecutionPlan &ep,
                              BDD::Node_ptr node) override {
    processing_result_t result;

    auto casted = BDD::cast_node<BDD::Call>(node);

    if (!casted) {
      return result;
    }

    auto call = casted->get_call();

    if (call.function_name == BDD::symbex::FN_DCHAIN_FREE_INDEX) {
      assert(!call.args[BDD::symbex::FN_DCHAIN_ARG_CHAIN].expr.isNull());
      assert(!call.args[BDD::symbex::FN_DCHAIN_ARG_INDEX].expr.isNull());

      auto _dchain = call.args[BDD::symbex::FN_DCHAIN_ARG_CHAIN].expr;
      auto _index = call.args[BDD::symbex::FN_DCHAIN_ARG_INDEX].expr;

      auto _dchain_addr = kutil::expr_addr_to_obj_addr(_dchain);
      
      auto mb = ep.get_memory_bank<x86TofinoMemoryBank>(x86_Tofino);
      auto saved = mb->has_data_structure(_dchain_addr);

      if (!saved) {
        auto config = BDD::symbex::get_dchain_config(ep.get_bdd(), _dchain_addr);
        auto dchain_ds = std::shared_ptr<x86TofinoMemoryBank::ds_t>(
            new x86TofinoMemoryBank::dchain_t(_dchain_addr, node->get_id(),
                                              config.index_range));
        mb->add_data_structure(dchain_ds);
      }

      auto new_module =
          std::make_shared<DchainFreeIndex>(node, _dchain_addr, _index);
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
    auto cloned = new DchainFreeIndex(node, dchain_addr, index);
    return std::shared_ptr<Module>(cloned);
  }

  virtual bool equals(const Module *other) const override {
    if (other->get_type() != type) {
      return false;
    }

    auto other_cast = static_cast<const DchainFreeIndex *>(other);

    if (dchain_addr != other_cast->get_dchain_addr()) {
      return false;
    }

    if (!kutil::solver_toolbox.are_exprs_always_equal(
            index, other_cast->get_index())) {
      return false;
    }

    return true;
  }

  const addr_t &get_dchain_addr() const { return dchain_addr; }
  const klee::ref<klee::Expr> &get_index() const { return index; }
};
} // namespace x86_tofino
} // namespace targets
} // namespace synapse
