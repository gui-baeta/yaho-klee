#pragma once

#include "tfhe_module.h"

namespace synapse {
namespace targets {
namespace tfhe {

class DchainAllocateNewIndex : public tfheModule {
private:
  addr_t dchain_addr;
  klee::ref<klee::Expr> time;
  klee::ref<klee::Expr> index_out;
  BDD::symbol_t out_of_space;

public:
  DchainAllocateNewIndex()
      : tfheModule(ModuleType::tfhe_DchainAllocateNewIndex, "DchainAllocate") {}

  DchainAllocateNewIndex(BDD::Node_ptr node, addr_t _dchain_addr,
                         klee::ref<klee::Expr> _time,
                         klee::ref<klee::Expr> _index_out,
                         const BDD::symbol_t &_out_of_space)
      : tfheModule(ModuleType::tfhe_DchainAllocateNewIndex, "DchainAllocate",
                  node),
        dchain_addr(_dchain_addr), time(_time), index_out(_index_out),
        out_of_space(_out_of_space) {}

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

      auto _dchain_addr = kutil::expr_addr_to_obj_addr(_dchain);

      auto _generated_symbols = casted->get_local_generated_symbols();
      auto _out_of_space =
          BDD::get_symbol(_generated_symbols, BDD::symbex::DCHAIN_OUT_OF_SPACE);

      save_dchain(ep, _dchain_addr);

      auto new_module = std::make_shared<DchainAllocateNewIndex>(
          node, _dchain_addr, _time, _index_out, _out_of_space);
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
                                             out_of_space);
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

    if (out_of_space.label != other_cast->get_out_of_space().label) {
      return false;
    }

    return true;
  }

  const addr_t &get_dchain_addr() const { return dchain_addr; }
  const klee::ref<klee::Expr> &get_time() const { return time; }
  const klee::ref<klee::Expr> &get_index_out() const { return index_out; }
  const BDD::symbol_t &get_out_of_space() const { return out_of_space; }
};
} // namespace tfhe
} // namespace targets
} // namespace synapse
