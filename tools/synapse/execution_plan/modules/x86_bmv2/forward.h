#pragma once

#include "../module.h"

namespace synapse {
namespace targets {
namespace x86_bmv2 {

class Forward : public Module {
private:
  int port;

public:
  Forward()
      : Module(ModuleType::x86_BMv2_Forward, TargetType::x86_BMv2, "Forward") {}

  Forward(BDD::Node_ptr node, int _port)
      : Module(ModuleType::x86_BMv2_Forward, TargetType::x86_BMv2, "Forward",
               node),
        port(_port) {}

private:
  processing_result_t process(const ExecutionPlan &ep,
                              BDD::Node_ptr node) override {
    processing_result_t result;

    auto casted = BDD::cast_node<BDD::ReturnProcess>(node);

    if (!casted) {
      return result;
    }

    if (casted->get_return_operation() == BDD::ReturnProcess::Operation::FWD) {
      auto _port = casted->get_return_value();

      auto new_module = std::make_shared<Forward>(node, _port);
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
    auto cloned = new Forward(node, port);
    return std::shared_ptr<Module>(cloned);
  }

  virtual bool equals(const Module *other) const override {
    if (other->get_type() != type) {
      return false;
    }

    auto other_cast = static_cast<const Forward *>(other);

    if (port != other_cast->get_port()) {
      return false;
    }

    return true;
  }

  int get_port() const { return port; }
};
} // namespace x86_bmv2
} // namespace targets
} // namespace synapse
