#pragma once

#include "tfhe_module.h"

namespace synapse {
namespace targets {
namespace tfhe {

class Broadcast : public tfheModule {
public:
  Broadcast() : tfheModule(ModuleType::tfhe_Broadcast, "Broadcast") {}

  Broadcast(BDD::Node_ptr node)
      : tfheModule(ModuleType::tfhe_Broadcast, "Broadcast", node) {}

private:
  processing_result_t process(const ExecutionPlan &ep,
                              BDD::Node_ptr node) override {
    processing_result_t result;

    auto casted = BDD::cast_node<BDD::ReturnProcess>(node);

    if (!casted) {
      return result;
    }

    if (casted->get_return_operation() ==
        BDD::ReturnProcess::Operation::BCAST) {
      auto new_module = std::make_shared<Broadcast>(node);
      auto new_ep = ep.add_leaves(new_module, node->get_next(), true);

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
    auto cloned = new Broadcast(node);
    return std::shared_ptr<Module>(cloned);
  }

  virtual bool equals(const Module *other) const override {
    return other->get_type() == type;
  }
};
} // namespace tfhe
} // namespace targets
} // namespace synapse
