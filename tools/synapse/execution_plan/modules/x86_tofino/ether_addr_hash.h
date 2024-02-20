#pragma once

#include "../module.h"

namespace synapse {
namespace targets {
namespace x86_tofino {

class EtherAddrHash : public Module {
private:
  klee::ref<klee::Expr> addr;
  BDD::symbol_t generated_symbol;

public:
  EtherAddrHash()
      : Module(ModuleType::x86_Tofino_EtherAddrHash, TargetType::x86_Tofino,
               "EtherAddrHash") {}

  EtherAddrHash(BDD::Node_ptr node, klee::ref<klee::Expr> _addr,
                BDD::symbol_t _generated_symbol)
      : Module(ModuleType::x86_Tofino_EtherAddrHash, TargetType::x86_Tofino,
               "EtherAddrHash", node),
        addr(_addr), generated_symbol(_generated_symbol) {}

private:
  processing_result_t process(const ExecutionPlan &ep,
                              BDD::Node_ptr node) override {
    processing_result_t result;

    auto casted = BDD::cast_node<BDD::Call>(node);

    if (!casted) {
      return result;
    }

    auto call = casted->get_call();

    if (call.function_name == BDD::symbex::FN_ETHER_HASH) {
      assert(!call.args[BDD::symbex::FN_ETHER_HASH_ARG_OBJ].in.isNull());

      auto _addr = call.args[BDD::symbex::FN_ETHER_HASH_ARG_OBJ].in;
      auto _generated_symbols = casted->get_local_generated_symbols();

      assert(_generated_symbols.size() == 1);
      auto _generated_symbol = *_generated_symbols.begin();

      auto new_module =
          std::make_shared<EtherAddrHash>(node, _addr, _generated_symbol);
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
    auto cloned = new EtherAddrHash(node, addr, generated_symbol);
    return std::shared_ptr<Module>(cloned);
  }

  virtual bool equals(const Module *other) const override {
    if (other->get_type() != type) {
      return false;
    }

    auto other_cast = static_cast<const EtherAddrHash *>(other);

    if (!kutil::solver_toolbox.are_exprs_always_equal(addr,
                                                      other_cast->get_addr())) {
      return false;
    }

    if (generated_symbol.label != other_cast->generated_symbol.label) {
      return false;
    }

    return true;
  }

  const klee::ref<klee::Expr> &get_addr() const { return addr; }
  const BDD::symbol_t &get_generated_symbol() const { return generated_symbol; }
};
} // namespace x86_tofino
} // namespace targets
} // namespace synapse
