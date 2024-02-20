#pragma once

#include "tfhe_module.h"

namespace synapse {
namespace targets {
namespace tfhe {

class DchainRejuvenateIndex : public tfheModule {
private:
  addr_t dchain_addr;
  klee::ref<klee::Expr> index;
  klee::ref<klee::Expr> time;

public:
  DchainRejuvenateIndex()
      : tfheModule(ModuleType::tfhe_DchainRejuvenateIndex, "DchainRejuvenate") {}

  DchainRejuvenateIndex(BDD::Node_ptr node, addr_t _dchain_addr,
                        klee::ref<klee::Expr> _index,
                        klee::ref<klee::Expr> _time)
      : tfheModule(ModuleType::tfhe_DchainRejuvenateIndex, "DchainRejuvenate",
                  node),
        dchain_addr(_dchain_addr), index(_index), time(_time) {}

private:
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
      save_dchain(ep, _dchain_addr);

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
} // namespace tfhe
} // namespace targets
} // namespace synapse
