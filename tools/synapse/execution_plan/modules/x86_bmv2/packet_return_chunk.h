#pragma once

#include "../module.h"

namespace synapse {
namespace targets {
namespace x86_bmv2 {

class PacketReturnChunk : public Module {
private:
  klee::ref<klee::Expr> chunk_addr;
  klee::ref<klee::Expr> chunk;

public:
  PacketReturnChunk()
      : Module(ModuleType::x86_BMv2_PacketReturnChunk, TargetType::x86_BMv2,
               "PacketReturnChunk") {}

  PacketReturnChunk(BDD::Node_ptr node, klee::ref<klee::Expr> _chunk_addr,
                    klee::ref<klee::Expr> _chunk)
      : Module(ModuleType::x86_BMv2_PacketReturnChunk, TargetType::x86_BMv2,
               "PacketReturnChunk", node),
        chunk_addr(_chunk_addr), chunk(_chunk) {}

private:
  processing_result_t process(const ExecutionPlan &ep,
                              BDD::Node_ptr node) override {
    processing_result_t result;

    auto casted = BDD::cast_node<BDD::Call>(node);

    if (!casted) {
      return result;
    }

    auto call = casted->get_call();

    if (call.function_name == BDD::symbex::FN_RETURN_CHUNK) {
      assert(!call.args[BDD::symbex::FN_BORROW_CHUNK_EXTRA].expr.isNull());
      assert(!call.args[BDD::symbex::FN_BORROW_CHUNK_EXTRA].in.isNull());

      auto _chunk_addr = call.args[BDD::symbex::FN_BORROW_CHUNK_EXTRA].expr;
      auto _chunk = call.args[BDD::symbex::FN_BORROW_CHUNK_EXTRA].in;

      auto new_module =
          std::make_shared<PacketReturnChunk>(node, _chunk_addr, _chunk);
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
    auto cloned = new PacketReturnChunk(node, chunk_addr, chunk);
    return std::shared_ptr<Module>(cloned);
  }

  virtual bool equals(const Module *other) const override {
    if (other->get_type() != type) {
      return false;
    }

    auto other_cast = static_cast<const PacketReturnChunk *>(other);

    if (!kutil::solver_toolbox.are_exprs_always_equal(
            chunk, other_cast->get_chunk())) {
      return false;
    }

    if (!kutil::solver_toolbox.are_exprs_always_equal(
            chunk_addr, other_cast->get_chunk_addr())) {
      return false;
    }

    return true;
  }

  const klee::ref<klee::Expr> &get_chunk() const { return chunk; }
  const klee::ref<klee::Expr> &get_chunk_addr() const { return chunk_addr; }
};
} // namespace x86_bmv2
} // namespace targets
} // namespace synapse
