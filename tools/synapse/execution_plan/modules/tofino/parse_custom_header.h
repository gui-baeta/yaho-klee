#pragma once

#include "tofino_module.h"

namespace synapse {
namespace targets {
namespace tofino {

class ParseCustomHeader : public TofinoModule {
private:
  klee::ref<klee::Expr> chunk;
  bits_t size;

public:
  ParseCustomHeader()
      : TofinoModule(ModuleType::Tofino_ParseCustomHeader,
                     "ParseCustomHeader") {}

  ParseCustomHeader(BDD::Node_ptr node, klee::ref<klee::Expr> _chunk,
                       bits_t _size)
      : TofinoModule(ModuleType::Tofino_ParseCustomHeader,
                     "ParseCustomHeader", node),
        chunk(_chunk), size(_size) {}

private:
  processing_result_t process(const ExecutionPlan &ep,
                              BDD::Node_ptr node) override {
    processing_result_t result;

    auto casted = BDD::cast_node<BDD::Call>(node);

    if (!casted) {
      return result;
    }

    auto call = casted->get_call();

    if (call.function_name != BDD::symbex::FN_BORROW_CHUNK) {
      return result;
    }

    assert(!call.args[BDD::symbex::FN_BORROW_CHUNK_ARG_LEN].expr.isNull());
    assert(!call.extra_vars[BDD::symbex::FN_BORROW_CHUNK_EXTRA].second.isNull());

    auto _length = call.args[BDD::symbex::FN_BORROW_CHUNK_ARG_LEN].expr;
    auto _chunk = call.extra_vars[BDD::symbex::FN_BORROW_CHUNK_EXTRA].second;

    // Only interested in parsing fixed size headers.
    if (_length->getKind() != klee::Expr::Kind::Constant) {
      return result;
    }

    auto _length_bytes = kutil::solver_toolbox.value_from_expr(_length);
    auto _length_bits = _length_bytes * 8;

    auto new_module =
        std::make_shared<ParseCustomHeader>(node, _chunk, _length_bits);
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
    auto cloned = new ParseCustomHeader(node, chunk, size);
    return std::shared_ptr<Module>(cloned);
  }

  virtual bool equals(const Module *other) const override {
    if (other->get_type() != type) {
      return false;
    }

    auto other_parser = static_cast<const ParseCustomHeader *>(other);

    auto other_chunk = other_parser->get_chunk();

    if (!kutil::solver_toolbox.are_exprs_always_equal(chunk, other_chunk)) {
      return false;
    }

    auto other_size = other_parser->get_size();

    if (size != other_size) {
      return false;
    }

    return true;
  }

  const klee::ref<klee::Expr> &get_chunk() const { return chunk; }
  bits_t get_size() const { return size; }
};
} // namespace tofino
} // namespace targets
} // namespace synapse
