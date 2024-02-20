#pragma once

#include "tfhe_module.h"

namespace synapse {
namespace targets {
namespace tfhe {

class CurrentTime : public tfheModule {
private:
  klee::ref<klee::Expr> time;

public:
  CurrentTime() : tfheModule(ModuleType::tfhe_CurrentTime, "CurrentTime") {}

  CurrentTime(BDD::Node_ptr node, klee::ref<klee::Expr> _time)
      : tfheModule(ModuleType::tfhe_CurrentTime, "CurrentTime", node),
        time(_time) {}

private:
  processing_result_t process(const ExecutionPlan &ep,
                              BDD::Node_ptr node) override {
    processing_result_t result;

    auto casted = BDD::cast_node<BDD::Call>(node);

    if (!casted) {
      return result;
    }

    auto call = casted->get_call();

    if (call.function_name == BDD::symbex::FN_CURRENT_TIME) {
      assert(!call.ret.isNull());
      auto _time = call.ret;

      auto new_module = std::make_shared<CurrentTime>(node, _time);
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
    auto cloned = new CurrentTime(node, time);
    return std::shared_ptr<Module>(cloned);
  }

  virtual bool equals(const Module *other) const override {
    if (other->get_type() != type) {
      return false;
    }

    auto other_cast = static_cast<const CurrentTime *>(other);

    if (!kutil::solver_toolbox.are_exprs_always_equal(time,
                                                      other_cast->get_time())) {
      return false;
    }

    return true;
  }

  const klee::ref<klee::Expr> &get_time() const { return time; }
};
} // namespace tfhe
} // namespace targets
} // namespace synapse
