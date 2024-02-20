#pragma once

#include "x86_module.h"

namespace synapse {
namespace targets {
namespace x86 {

class LoadBalancedFlowHash : public x86Module {
private:
  klee::ref<klee::Expr> obj;
  BDD::symbol_t hash;

public:
  LoadBalancedFlowHash()
      : x86Module(ModuleType::x86_LoadBalancedFlowHash,
                  "LoadBalancedFlowHash") {}

  LoadBalancedFlowHash(BDD::Node_ptr node, klee::ref<klee::Expr> _obj,
                       BDD::symbol_t _hash)
      : x86Module(ModuleType::x86_LoadBalancedFlowHash, "LoadBalancedFlowHash",
                  node),
        obj(_obj), hash(_hash) {}

private:
  processing_result_t process(const ExecutionPlan &ep,
                              BDD::Node_ptr node) override {
    processing_result_t result;

    auto casted = BDD::cast_node<BDD::Call>(node);

    if (!casted) {
      return result;
    }

    auto call = casted->get_call();

    if (call.function_name != BDD::symbex::FN_LOADBALANCEDFLOW_HASH) {
      return result;
    }

    assert(!call.args[BDD::symbex::FN_LOADBALANCEDFLOW_HASH_ARG_OBJ].in.isNull());
    auto _obj = call.args[BDD::symbex::FN_LOADBALANCEDFLOW_HASH_ARG_OBJ].in;

    auto _generated_symbols = casted->get_local_generated_symbols();
    auto _hash = BDD::get_symbol(_generated_symbols,
                                 BDD::symbex::LOADBALANCEDFLOW_HASH_SYMBOL);

    auto new_module = std::make_shared<LoadBalancedFlowHash>(node, _obj, _hash);
    auto new_ep = ep.add_leaf(new_module, node->get_next());

    result.module = new_module;
    result.next_eps.push_back(new_ep);

    return result;
  }

public:
  virtual void visit(ExecutionPlanVisitor &visitor,
                     const ExecutionPlanNode *ep_node) const override {
    visitor.visit(ep_node, this);
  }

  virtual Module_ptr clone() const override {
    auto cloned = new LoadBalancedFlowHash(node, obj, hash);
    return std::shared_ptr<Module>(cloned);
  }

  virtual bool equals(const Module *other) const override {
    if (other->get_type() != type) {
      return false;
    }

    auto other_cast = static_cast<const LoadBalancedFlowHash *>(other);

    if (obj != other_cast->get_obj()) {
      return false;
    }

    if (hash.label != other_cast->get_hash().label) {
      return false;
    }

    return true;
  }

  klee::ref<klee::Expr> get_obj() const { return obj; }
  const BDD::symbol_t &get_hash() const { return hash; }
};
} // namespace x86
} // namespace targets
} // namespace synapse
