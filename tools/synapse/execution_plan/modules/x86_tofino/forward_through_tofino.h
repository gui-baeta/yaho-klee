#pragma once

#include "../module.h"

namespace synapse {
namespace targets {
namespace x86_tofino {

class ForwardThroughTofino : public Module {
private:
  int port;

public:
  ForwardThroughTofino()
      : Module(ModuleType::x86_Tofino_ForwardThroughTofino,
               TargetType::x86_Tofino, "ForwardThroughTofino") {}

  ForwardThroughTofino(BDD::Node_ptr node, int _port)
      : Module(ModuleType::x86_Tofino_ForwardThroughTofino,
               TargetType::x86_Tofino, "ForwardThroughTofino", node),
        port(_port) {}

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

    auto forward = std::make_shared<ForwardThroughTofino>(node, _port);
    auto forward_ep = ep.add_leaf(forward, node->get_next(), true);

    result.module = forward;
    result.next_eps.push_back(forward_ep);

    return result;
  }

public:
  virtual void visit(ExecutionPlanVisitor &visitor,
                     const ExecutionPlanNode *ep_node) const override {
    visitor.visit(ep_node, this);
  }

  virtual Module_ptr clone() const override {
    auto cloned = new ForwardThroughTofino(node, port);
    return std::shared_ptr<Module>(cloned);
  }

  virtual bool equals(const Module *other) const override {
    if (other->get_type() != type) {
      return false;
    }

    auto other_cast = static_cast<const ForwardThroughTofino *>(other);

    if (port != other_cast->get_port()) {
      return false;
    }

    return true;
  }

  int get_port() const { return port; }
};
} // namespace x86_tofino
} // namespace targets
} // namespace synapse
