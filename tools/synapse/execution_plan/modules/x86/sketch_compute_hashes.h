#pragma once

#include "x86_module.h"

namespace synapse {
namespace targets {
namespace x86 {

class SketchComputeHashes : public x86Module {
private:
  addr_t sketch_addr;
  klee::ref<klee::Expr> key;

public:
  SketchComputeHashes()
      : x86Module(ModuleType::x86_SketchComputeHashes, "SketchComputeHashes") {}

  SketchComputeHashes(BDD::Node_ptr node, addr_t _sketch_addr,
                      klee::ref<klee::Expr> _key)
      : x86Module(ModuleType::x86_SketchComputeHashes, "SketchComputeHashes",
                  node),
        sketch_addr(_sketch_addr), key(_key) {}

private:
  processing_result_t process(const ExecutionPlan &ep,
                              BDD::Node_ptr node) override {
    processing_result_t result;

    auto casted = BDD::cast_node<BDD::Call>(node);

    if (!casted) {
      return result;
    }

    auto call = casted->get_call();

    if (call.function_name != BDD::symbex::FN_SKETCH_COMPUTE_HASHES) {
      return result;
    }

    assert(!call.args[BDD::symbex::FN_SKETCH_ARG_SKETCH].expr.isNull());
    assert(!call.args[BDD::symbex::FN_SKETCH_ARG_KEY].expr.isNull());

    auto _sketch = call.args[BDD::symbex::FN_SKETCH_ARG_SKETCH].expr;
    auto _key = call.args[BDD::symbex::FN_SKETCH_ARG_KEY].expr;

    auto _sketch_addr = kutil::expr_addr_to_obj_addr(_sketch);

    save_sketch(ep, _sketch_addr);

    auto new_module =
        std::make_shared<SketchComputeHashes>(node, _sketch_addr, _key);
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
    auto cloned = new SketchComputeHashes(node, sketch_addr, key);
    return std::shared_ptr<Module>(cloned);
  }

  virtual bool equals(const Module *other) const override {
    if (other->get_type() != type) {
      return false;
    }

    auto other_cast = static_cast<const SketchComputeHashes *>(other);

    if (sketch_addr != other_cast->get_sketch_addr()) {
      return false;
    }

    if (!kutil::solver_toolbox.are_exprs_always_equal(key,
                                                      other_cast->get_key())) {
      return false;
    }

    return true;
  }

  addr_t get_sketch_addr() const { return sketch_addr; }
  klee::ref<klee::Expr> get_key() const { return key; }
};
} // namespace x86
} // namespace targets
} // namespace synapse
