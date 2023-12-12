#pragma once

#include "tfhe_module.h"

namespace synapse {
namespace targets {
namespace tfhe {

class RteEtherAddrHash : public tfheModule {
private:
  klee::ref<klee::Expr> obj;
  klee::ref<klee::Expr> hash;

  BDD::symbols_t generated_symbols;

public:
  RteEtherAddrHash()
      : tfheModule(ModuleType::tfhe_RteEtherAddrHash, "EtherHash") {}

  RteEtherAddrHash(BDD::Node_ptr node, klee::ref<klee::Expr> _obj,
                   klee::ref<klee::Expr> _hash,
                   BDD::symbols_t _generated_symbols)
      : tfheModule(ModuleType::tfhe_RteEtherAddrHash, "EtherHash", node),
        obj(_obj), hash(_hash), generated_symbols(_generated_symbols) {}

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
      assert(!call.ret.isNull());

      auto _obj = call.args[BDD::symbex::FN_ETHER_HASH_ARG_OBJ].in;
      auto _hash = call.ret;

      auto _generated_symbols = casted->get_local_generated_symbols();

      auto new_module = std::make_shared<RteEtherAddrHash>(node, _obj, _hash,
                                                           _generated_symbols);
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
    auto cloned = new RteEtherAddrHash(node, obj, hash, generated_symbols);
    return std::shared_ptr<Module>(cloned);
  }

  virtual bool equals(const Module *other) const override {
    if (other->get_type() != type) {
      return false;
    }

    auto other_cast = static_cast<const RteEtherAddrHash *>(other);

    if (!kutil::solver_toolbox.are_exprs_always_equal(obj,
                                                      other_cast->get_obj())) {
      return false;
    }

    if (!kutil::solver_toolbox.are_exprs_always_equal(hash,
                                                      other_cast->get_hash())) {
      return false;
    }

    if (generated_symbols != other_cast->generated_symbols) {
      return false;
    }

    return true;
  }

  const klee::ref<klee::Expr> &get_obj() const { return obj; }
  const klee::ref<klee::Expr> &get_hash() const { return hash; }

  const BDD::symbols_t &get_generated_symbols() const {
    return generated_symbols;
  }
};
} // namespace tfhe
} // namespace targets
} // namespace synapse
