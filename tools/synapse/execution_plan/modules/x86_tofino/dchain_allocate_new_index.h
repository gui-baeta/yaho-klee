#pragma once

#include "../module.h"
#include "memory_bank.h"

namespace synapse {
namespace targets {
namespace x86_tofino {

class DchainAllocateNewIndex : public Module {
private:
  addr_t dchain_addr;
  klee::ref<klee::Expr> time;
  klee::ref<klee::Expr> index_out;
  klee::ref<klee::Expr> success;

  BDD::symbols_t generated_symbols;

public:
  DchainAllocateNewIndex()
      : Module(ModuleType::x86_Tofino_DchainAllocateNewIndex,
               TargetType::x86_Tofino, "DchainAllocate") {}

  DchainAllocateNewIndex(BDD::Node_ptr node, addr_t _dchain_addr,
                         klee::ref<klee::Expr> _time,
                         klee::ref<klee::Expr> _index_out,
                         klee::ref<klee::Expr> _success,
                         BDD::symbols_t _generated_symbols)
      : Module(ModuleType::x86_Tofino_DchainAllocateNewIndex,
               TargetType::x86_Tofino, "DchainAllocate", node),
        dchain_addr(_dchain_addr), time(_time), index_out(_index_out),
        success(_success), generated_symbols(_generated_symbols) {}

private:
  processing_result_t process(const ExecutionPlan &ep,
                              BDD::Node_ptr node) override {
    processing_result_t result;

    auto casted = BDD::cast_node<BDD::Call>(node);

    if (!casted) {
      return result;
    }

    auto call = casted->get_call();

    if (call.function_name == BDD::symbex::FN_DCHAIN_ALLOCATE_NEW_INDEX) {
      assert(!call.args[BDD::symbex::FN_DCHAIN_ARG_CHAIN].expr.isNull());
      assert(!call.args[BDD::symbex::FN_DCHAIN_ARG_TIME].expr.isNull());
      assert(!call.args[BDD::symbex::FN_DCHAIN_ARG_OUT].out.isNull());
      assert(!call.ret.isNull());

      auto _dchain = call.args[BDD::symbex::FN_DCHAIN_ARG_CHAIN].expr;
      auto _time = call.args[BDD::symbex::FN_DCHAIN_ARG_TIME].expr;
      auto _index_out = call.args[BDD::symbex::FN_DCHAIN_ARG_OUT].out;
      auto _success = call.ret;

      auto _generated_symbols = casted->get_local_generated_symbols();
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

      auto new_module = std::make_shared<DchainAllocateNewIndex>(
          node, _dchain_addr, _time, _index_out, _success, _generated_symbols);
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
    auto cloned = new DchainAllocateNewIndex(node, dchain_addr, time, index_out,
                                             success, generated_symbols);
    return std::shared_ptr<Module>(cloned);
  }

  virtual bool equals(const Module *other) const override {
    if (other->get_type() != type) {
      return false;
    }

    auto other_cast = static_cast<const DchainAllocateNewIndex *>(other);

    if (dchain_addr != other_cast->get_dchain_addr()) {
      return false;
    }

    if (!kutil::solver_toolbox.are_exprs_always_equal(time,
                                                      other_cast->get_time())) {
      return false;
    }

    if (!kutil::solver_toolbox.are_exprs_always_equal(
            index_out, other_cast->get_index_out())) {
      return false;
    }

    if (!kutil::solver_toolbox.are_exprs_always_equal(
            success, other_cast->get_success())) {
      return false;
    }

    if (generated_symbols != other_cast->generated_symbols) {
      return false;
    }

    return true;
  }

  const addr_t &get_dchain_addr() const { return dchain_addr; }
  const klee::ref<klee::Expr> &get_time() const { return time; }
  const klee::ref<klee::Expr> &get_index_out() const { return index_out; }
  const klee::ref<klee::Expr> &get_success() const { return success; }

  const BDD::symbols_t &get_generated_symbols() const {
    return generated_symbols;
  }
};
} // namespace x86_tofino
} // namespace targets
} // namespace synapse
