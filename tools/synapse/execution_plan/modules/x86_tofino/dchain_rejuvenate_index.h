#pragma once

#include "../module.h"
#include "ignore.h"
#include "memory_bank.h"

namespace synapse {
namespace targets {
namespace x86_tofino {

class DchainRejuvenateIndex : public Module {
private:
  addr_t dchain_addr;
  klee::ref<klee::Expr> index;
  klee::ref<klee::Expr> time;

public:
  DchainRejuvenateIndex()
      : Module(ModuleType::x86_Tofino_DchainRejuvenateIndex,
               TargetType::x86_Tofino, "DchainRejuvenate") {}

  DchainRejuvenateIndex(BDD::Node_ptr node, addr_t _dchain_addr,
                        klee::ref<klee::Expr> _index,
                        klee::ref<klee::Expr> _time)
      : Module(ModuleType::x86_Tofino_DchainRejuvenateIndex,
               TargetType::x86_Tofino, "DchainRejuvenate", node),
        dchain_addr(_dchain_addr), index(_index), time(_time) {}

private:
  bool already_rejuvenated_by_data_plane(const ExecutionPlan &ep,
                                         addr_t obj) const {
    auto mb = ep.get_memory_bank();
    auto uses_int_allocator =
        mb->check_placement_decision(obj, PlacementDecision::IntegerAllocator);

    if (!uses_int_allocator) {
      return false;
    }

    auto prev =
        get_prev_modules(ep, {ModuleType::Tofino_IntegerAllocatorRejuvenate});

    assert(
        prev.size() > 0 &&
        "Preemptive pruning on SendToController shouldn't allow this to fail");

    return true;
  }

  processing_result_t process(const ExecutionPlan &ep,
                              BDD::Node_ptr node) override {
    processing_result_t result;

    auto casted = BDD::cast_node<BDD::Call>(node);

    if (!casted) {
      return result;
    }

    auto call = casted->get_call();

    if (call.function_name == BDD::symbex::FN_DCHAIN_REJUVENATE) {
      assert(!call.args[BDD::symbex::FN_DCHAIN_ARG_CHAIN].expr.isNull());
      assert(!call.args[BDD::symbex::FN_DCHAIN_ARG_INDEX].expr.isNull());
      assert(!call.args[BDD::symbex::FN_DCHAIN_ARG_TIME].expr.isNull());

      auto _dchain = call.args[BDD::symbex::FN_DCHAIN_ARG_CHAIN].expr;
      auto _index = call.args[BDD::symbex::FN_DCHAIN_ARG_INDEX].expr;
      auto _time = call.args[BDD::symbex::FN_DCHAIN_ARG_TIME].expr;
      auto _dchain_addr = kutil::expr_addr_to_obj_addr(_dchain);

      auto already_rejuvenated =
          already_rejuvenated_by_data_plane(ep, _dchain_addr);

      if (already_rejuvenated) {
        auto new_module = std::make_shared<Ignore>(node);
        auto new_ep = ep.ignore_leaf(node->get_next(), TargetType::x86_Tofino);

        result.module = new_module;
        result.next_eps.push_back(new_ep);

        return result;
      }

      auto mb = ep.get_memory_bank<x86TofinoMemoryBank>(x86_Tofino);
      auto saved = mb->has_data_structure(_dchain_addr);

      if (!saved) {
        auto config = BDD::symbex::get_dchain_config(ep.get_bdd(), _dchain_addr);
        auto dchain_ds = std::shared_ptr<x86TofinoMemoryBank::ds_t>(
            new x86TofinoMemoryBank::dchain_t(_dchain_addr, node->get_id(),
                                              config.index_range));
        mb->add_data_structure(dchain_ds);
      }

      auto new_module = std::make_shared<DchainRejuvenateIndex>(
          node, _dchain_addr, _index, _time);
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
    auto cloned = new DchainRejuvenateIndex(node, dchain_addr, index, time);
    return std::shared_ptr<Module>(cloned);
  }

  virtual bool equals(const Module *other) const override {
    if (other->get_type() != type) {
      return false;
    }

    auto other_cast = static_cast<const DchainRejuvenateIndex *>(other);

    if (dchain_addr != other_cast->get_dchain_addr()) {
      return false;
    }

    if (!kutil::solver_toolbox.are_exprs_always_equal(
            index, other_cast->get_index())) {
      return false;
    }

    if (!kutil::solver_toolbox.are_exprs_always_equal(time,
                                                      other_cast->get_time())) {
      return false;
    }

    return true;
  }

  const addr_t &get_dchain_addr() const { return dchain_addr; }
  const klee::ref<klee::Expr> &get_index() const { return index; }
  const klee::ref<klee::Expr> &get_time() const { return time; }
};
} // namespace x86_tofino
} // namespace targets
} // namespace synapse
