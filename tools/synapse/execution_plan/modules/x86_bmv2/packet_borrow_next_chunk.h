#pragma once

#include "../module.h"

namespace synapse {
namespace targets {
namespace x86_bmv2 {

class PacketBorrowNextChunk : public Module {
private:
  klee::ref<klee::Expr> p_addr;
  klee::ref<klee::Expr> chunk_addr;
  klee::ref<klee::Expr> chunk;
  klee::ref<klee::Expr> length;

public:
  PacketBorrowNextChunk()
      : Module(ModuleType::x86_BMv2_PacketBorrowNextChunk, TargetType::x86_BMv2,
               "PacketBorrowNextChunk") {}

  PacketBorrowNextChunk(BDD::Node_ptr node, klee::ref<klee::Expr> _p_addr,
                        klee::ref<klee::Expr> _chunk_addr,
                        klee::ref<klee::Expr> _chunk,
                        klee::ref<klee::Expr> _length)
      : Module(ModuleType::x86_BMv2_PacketBorrowNextChunk, TargetType::x86_BMv2,
               "PacketBorrowNextChunk", node),
        p_addr(_p_addr), chunk_addr(_chunk_addr), chunk(_chunk),
        length(_length) {}

private:
  processing_result_t process(const ExecutionPlan &ep,
                              BDD::Node_ptr node) override {
    processing_result_t result;

    auto casted = BDD::cast_node<BDD::Call>(node);

    if (!casted) {
      return result;
    }

    auto call = casted->get_call();

    if (call.function_name == BDD::symbex::FN_BORROW_CHUNK) {
      assert(!call.args[BDD::symbex::FN_BORROW_ARG_PACKET].expr.isNull());
      assert(!call.args[BDD::symbex::FN_BORROW_ARG_CHUNK].out.isNull());
      assert(!call.extra_vars[BDD::symbex::FN_BORROW_CHUNK_EXTRA].second.isNull());
      assert(!call.args[BDD::symbex::FN_BORROW_CHUNK_ARG_LEN].expr.isNull());

      auto _p_addr = call.args[BDD::symbex::FN_BORROW_ARG_PACKET].expr;
      auto _chunk_addr = call.args[BDD::symbex::FN_BORROW_ARG_CHUNK].out;
      auto _chunk = call.extra_vars[BDD::symbex::FN_BORROW_CHUNK_EXTRA].second;
      auto _length = call.args[BDD::symbex::FN_BORROW_CHUNK_ARG_LEN].expr;

      auto new_module = std::make_shared<PacketBorrowNextChunk>(
          node, _p_addr, _chunk_addr, _chunk, _length);
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
    auto cloned =
        new PacketBorrowNextChunk(node, p_addr, chunk_addr, chunk, length);
    return std::shared_ptr<Module>(cloned);
  }

  virtual bool equals(const Module *other) const override {
    if (other->get_type() != type) {
      return false;
    }

    auto other_cast = static_cast<const PacketBorrowNextChunk *>(other);

    if (!kutil::solver_toolbox.are_exprs_always_equal(
            p_addr, other_cast->get_p_addr())) {
      return false;
    }

    if (!kutil::solver_toolbox.are_exprs_always_equal(
            chunk_addr, other_cast->get_chunk_addr())) {
      return false;
    }

    if (!kutil::solver_toolbox.are_exprs_always_equal(
            chunk, other_cast->get_chunk())) {
      return false;
    }

    if (!kutil::solver_toolbox.are_exprs_always_equal(
            length, other_cast->get_length())) {
      return false;
    }

    return true;
  }

  const klee::ref<klee::Expr> &get_p_addr() const { return p_addr; }
  const klee::ref<klee::Expr> &get_chunk_addr() const { return chunk_addr; }
  const klee::ref<klee::Expr> &get_chunk() const { return chunk; }
  const klee::ref<klee::Expr> &get_length() const { return length; }
};
} // namespace x86_bmv2
} // namespace targets
} // namespace synapse
