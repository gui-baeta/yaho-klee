#pragma once

#include "../module.h"

namespace synapse {
namespace targets {
namespace x86_bmv2 {

class CurrentTime : public Module {
private:
  klee::ref<klee::Expr> time;
  BDD::symbols_t generated_symbols;

public:
  CurrentTime()
      : Module(ModuleType::x86_BMv2_CurrentTime, TargetType::x86_BMv2,
               "CurrentTime") {}

  CurrentTime(BDD::Node_ptr node, klee::ref<klee::Expr> _time,
              BDD::symbols_t _generated_symbols)
      : Module(ModuleType::x86_BMv2_CurrentTime, TargetType::x86_BMv2,
               "CurrentTime", node),
        time(_time), generated_symbols(_generated_symbols) {}

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
      auto _generated_symbols = casted->get_local_generated_symbols();

      auto new_module =
          std::make_shared<CurrentTime>(node, _time, _generated_symbols);
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
    auto cloned = new CurrentTime(node, time, generated_symbols);
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

    if (generated_symbols != other_cast->generated_symbols) {
      return false;
    }

    return true;
  }

  const klee::ref<klee::Expr> &get_time() const { return time; }

  const BDD::symbols_t &get_generated_symbols() const {
    return generated_symbols;
  }
};
} // namespace x86_bmv2
} // namespace targets
} // namespace synapse
