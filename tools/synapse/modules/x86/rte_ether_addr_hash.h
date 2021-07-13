#pragma once

#include "../../log.h"
#include "../module.h"
#include "call-paths-to-bdd.h"

namespace synapse {
namespace targets {
namespace x86 {

class RteEtherAddrHash : public Module {
private:
  klee::ref<klee::Expr> obj;
  klee::ref<klee::Expr> hash;

public:
  RteEtherAddrHash()
      : Module(ModuleType::x86_RteEtherAddrHash, Target::x86, "EtherHash") {}

  RteEtherAddrHash(BDD::BDDNode_ptr node, klee::ref<klee::Expr> _obj,
                   klee::ref<klee::Expr> _hash)
      : Module(ModuleType::x86_RteEtherAddrHash, Target::x86, "EtherHash",
               node),
        obj(_obj), hash(_hash) {}

private:
  processing_result_t process_call(const ExecutionPlan &ep,
                                   BDD::BDDNode_ptr node,
                                   const BDD::Call *casted) override {
    processing_result_t result;
    auto call = casted->get_call();

    if (call.function_name == "rte_ether_addr_hash") {
      assert(!call.args["obj"].in.isNull());
      assert(!call.ret.isNull());

      auto _obj = call.args["obj"].in;
      auto _hash = call.ret;

      auto new_module = std::make_shared<RteEtherAddrHash>(node, _obj, _hash);
      auto new_ep = ep.add_leaves(new_module, node->get_next());

      result.module = new_module;
      result.next_eps.push_back(new_ep);
    }

    return result;
  }

public:
  virtual void visit(ExecutionPlanVisitor &visitor) const override {
    visitor.visit(this);
  }

  virtual Module_ptr clone() const override {
    auto cloned = new RteEtherAddrHash(node, obj, hash);
    return std::shared_ptr<Module>(cloned);
  }

  virtual bool equals(const Module *other) const override {
    if (other->get_type() != type) {
      return false;
    }

    auto other_cast = static_cast<const RteEtherAddrHash *>(other);

    if (!BDD::solver_toolbox.are_exprs_always_equal(obj,
                                                    other_cast->get_obj())) {
      return false;
    }

    if (!BDD::solver_toolbox.are_exprs_always_equal(hash,
                                                    other_cast->get_hash())) {
      return false;
    }

    return true;
  }

  const klee::ref<klee::Expr> &get_obj() const { return obj; }
  const klee::ref<klee::Expr> &get_hash() const { return hash; }
};
} // namespace x86
} // namespace targets
} // namespace synapse
