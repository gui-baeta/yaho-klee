#pragma once

#include "tofino_module.h"

namespace synapse {
namespace targets {
namespace tofino {

class Forward : public TofinoModule {
private:
  int port;

public:
  Forward() : TofinoModule(ModuleType::Tofino_Forward, "Forward") {}

  Forward(BDD::Node_ptr node, int _port)
      : TofinoModule(ModuleType::Tofino_Forward, "Forward", node), port(_port) {
  }

private:
  processing_result_t process(const ExecutionPlan &ep,
                              BDD::Node_ptr node) override {
    processing_result_t result;

    auto casted = BDD::cast_node<BDD::ReturnProcess>(node);

    if (!casted) {
      return result;
    }

    if (casted->get_return_operation() != BDD::ReturnProcess::Operation::FWD) {
      return result;
    }

    auto _port = casted->get_return_value();

    auto new_module = std::make_shared<Forward>(node, _port);
    auto new_ep = ep.add_leaf(new_module, node, false, true);
    auto with_postponed = apply_postponed(new_ep, node, node->get_next());

    result.module = new_module;
    result.next_eps.push_back(with_postponed);

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
} // namespace tofino
} // namespace targets
} // namespace synapse
