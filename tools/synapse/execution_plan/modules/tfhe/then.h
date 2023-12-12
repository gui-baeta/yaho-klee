#pragma once

#include "tfhe_module.h"

#include "else.h"

namespace synapse {
namespace targets {
namespace tfhe {

class Then : public tfheModule {
public:
  Then() : tfheModule(ModuleType::tfhe_Then, "Then") {}
  Then(BDD::Node_ptr node) : tfheModule(ModuleType::tfhe_Then, "Then", node) {}

private:
  processing_result_t process(const ExecutionPlan &ep,
                              BDD::Node_ptr node) override {
    return processing_result_t();
  }

public:
  virtual void visit(ExecutionPlanVisitor &visitor,
                     const ExecutionPlanNode *ep_node) const override {
    visitor.visit(ep_node, this);
  }

  virtual Module_ptr clone() const override {
    auto cloned = new Then(node);
    return std::shared_ptr<Module>(cloned);
  }

  virtual bool equals(const Module *other) const override {
    return other->get_type() == type;
  }
};
} // namespace tfhe
} // namespace targets
} // namespace synapse
