#pragma once

#include "tfhe_module.h"

namespace synapse {
namespace targets {
namespace tfhe {

class Drop : public tfheModule {
public:
  Drop() : tfheModule(ModuleType::tfhe_Drop, "Drop") {}
  Drop(BDD::Node_ptr node) : tfheModule(ModuleType::tfhe_Drop, "Drop", node) {}

private:
  processing_result_t process(const ExecutionPlan &ep,
                              BDD::Node_ptr node) override {
    processing_result_t result;

    auto casted = BDD::cast_node<BDD::ReturnProcess>(node);

    if (!casted) {
      return result;
    }

    if (casted->get_return_operation() == BDD::ReturnProcess::Operation::DROP) {
      auto new_module = std::make_shared<Drop>(node);
      auto new_ep = ep.add_leaf(new_module, node->get_next(), true);

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
    auto cloned = new Drop(node);
    return std::shared_ptr<Module>(cloned);
  }

  virtual bool equals(const Module *other) const override {
    return other->get_type() == type;
  }
};
} // namespace tfhe
} // namespace targets
} // namespace synapse
