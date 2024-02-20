#pragma once

#include "x86_module.h"

namespace synapse {
namespace targets {
namespace x86 {

class SketchFetch : public x86Module {
private:
  addr_t sketch_addr;
  BDD::symbol_t overflow;

public:
  SketchFetch() : x86Module(ModuleType::x86_SketchFetch, "SketchFetch") {}

  SketchFetch(BDD::Node_ptr node, addr_t _sketch_addr,
              BDD::symbol_t _overflow)
      : x86Module(ModuleType::x86_SketchFetch, "SketchFetch", node),
        sketch_addr(_sketch_addr), overflow(_overflow) {}

private:
  processing_result_t process(const ExecutionPlan &ep,
                              BDD::Node_ptr node) override {
    processing_result_t result;

    auto casted = BDD::cast_node<BDD::Call>(node);

    if (!casted) {
      return result;
    }

    auto call = casted->get_call();

    if (call.function_name != BDD::symbex::FN_SKETCH_FETCH) {
      return result;
    }

    assert(!call.args[BDD::symbex::FN_SKETCH_ARG_SKETCH].expr.isNull());
    auto _sketch = call.args[BDD::symbex::FN_SKETCH_ARG_SKETCH].expr;
    auto _sketch_addr = kutil::expr_addr_to_obj_addr(_sketch);

    auto _generated_symbols = casted->get_local_generated_symbols();
    auto _overflow =
        BDD::get_symbol(_generated_symbols, BDD::symbex::SKETCH_OVERFLOW_SYMBOL);

    save_sketch(ep, _sketch_addr);

    auto new_module =
        std::make_shared<SketchFetch>(node, _sketch_addr, _overflow);
    auto new_ep = ep.add_leaf(new_module, node->get_next());

    result.module = new_module;
    result.next_eps.push_back(new_ep);

    return result;
  }

public:
  virtual void visit(ExecutionPlanVisitor &visitor,
                     const ExecutionPlanNode *ep_node) const override {
    visitor.visit(ep_node, this);
  }

  virtual Module_ptr clone() const override {
    auto cloned = new SketchFetch(node, sketch_addr, overflow);
    return std::shared_ptr<Module>(cloned);
  }

  virtual bool equals(const Module *other) const override {
    if (other->get_type() != type) {
      return false;
    }

    auto other_cast = static_cast<const SketchFetch *>(other);

    if (sketch_addr != other_cast->get_sketch_addr()) {
      return false;
    }

    if (overflow.label != other_cast->get_overflow().label) {
      return false;
    }

    return true;
  }

  addr_t get_sketch_addr() const { return sketch_addr; }
  const BDD::symbol_t &get_overflow() const { return overflow; }
};
} // namespace x86
} // namespace targets
} // namespace synapse
