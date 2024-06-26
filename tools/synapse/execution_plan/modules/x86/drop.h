#pragma once

#include "x86_module.h"

namespace synapse {
namespace targets {
namespace x86 {

class Drop : public x86Module {
public:
  Drop() : x86Module(ModuleType::x86_Drop, "Drop") {}
  Drop(BDD::Node_ptr node) : x86Module(ModuleType::x86_Drop, "Drop", node) {}

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
    auto cloned = new Drop(node);
    return std::shared_ptr<Module>(cloned);
  }

  virtual bool equals(const Module *other) const override {
    return other->get_type() == type;
  }
};
} // namespace x86
} // namespace targets
} // namespace synapse
