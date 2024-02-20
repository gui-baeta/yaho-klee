#pragma once

#include "../module.h"
#include "ignore.h"

namespace synapse {
namespace targets {
namespace x86_tofino {

class PacketModifyTCP : public Module {
private:
  klee::ref<klee::Expr> tcp_chunk;
  std::vector<modification_t> modifications;

public:
  PacketModifyTCP()
      : Module(ModuleType::x86_Tofino_PacketModifyTCP, TargetType::x86_Tofino,
               "PacketModifyTCP") {}

  PacketModifyTCP(BDD::Node_ptr node, klee::ref<klee::Expr> _tcp_chunk,
                  const std::vector<modification_t> &_modifications)
      : Module(ModuleType::x86_Tofino_PacketModifyTCP, TargetType::x86_Tofino,
               "PacketModifyTCP", node),
        tcp_chunk(_tcp_chunk), modifications(_modifications) {}

private:
  klee::ref<klee::Expr> get_tcp_chunk(const BDD::Node *node) const {
    assert(node->get_type() == BDD::Node::NodeType::CALL);

    auto call_node = static_cast<const BDD::Call *>(node);
    auto call = call_node->get_call();

    assert(call.function_name == BDD::symbex::FN_BORROW_CHUNK);
    assert(!call.extra_vars[BDD::symbex::FN_BORROW_CHUNK_EXTRA].second.isNull());

    return call.extra_vars[BDD::symbex::FN_BORROW_CHUNK_EXTRA].second;
  }

  bool is_ip_options(const BDD::Node *node) const {
    assert(node->get_type() == BDD::Node::NodeType::CALL);

    auto call_node = static_cast<const BDD::Call *>(node);
    auto call = call_node->get_call();

    auto len = call.args[BDD::symbex::FN_BORROW_CHUNK_ARG_LEN].expr;
    return len->getKind() != klee::Expr::Kind::Constant;
  }

  processing_result_t process(const ExecutionPlan &ep,
                              BDD::Node_ptr node) override {
    processing_result_t result;

    auto casted = BDD::cast_node<BDD::Call>(node);

    if (!casted) {
      return result;
    }

    auto call = casted->get_call();

    if (call.function_name != BDD::symbex::FN_RETURN_CHUNK) {
      return result;
    }

    auto all_prev_packet_borrow_next_chunk =
        get_prev_fn(ep, node, BDD::symbex::FN_BORROW_CHUNK);

    assert(all_prev_packet_borrow_next_chunk.size());

    auto all_prev_packet_return_chunk =
        get_prev_fn(ep, node, BDD::symbex::FN_RETURN_CHUNK);

    if (all_prev_packet_return_chunk.size() != 0 ||
        all_prev_packet_borrow_next_chunk.size() < 3 ||
        is_ip_options(all_prev_packet_borrow_next_chunk[0].get())) {
      return result;
    }

    auto borrow_tcp = all_prev_packet_borrow_next_chunk[0].get();

    auto curr_tcp_chunk = call.args[BDD::symbex::FN_BORROW_CHUNK_EXTRA].in;
    auto prev_tcp_chunk = get_tcp_chunk(borrow_tcp);

    if (curr_tcp_chunk->getWidth() != 20 * 8 ||
        prev_tcp_chunk->getWidth() != 20 * 8) {
      return result;
    }

    auto _modifications = build_modifications(prev_tcp_chunk, curr_tcp_chunk);

    if (_modifications.size() == 0) {
      auto new_module = std::make_shared<Ignore>(node);
      auto new_ep = ep.ignore_leaf(node->get_next(), TargetType::x86_Tofino);

      result.module = new_module;
      result.next_eps.push_back(new_ep);

      return result;
    }

    auto new_module =
        std::make_shared<PacketModifyTCP>(node, prev_tcp_chunk, _modifications);
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
    auto cloned = new PacketModifyTCP(node, tcp_chunk, modifications);
    return std::shared_ptr<Module>(cloned);
  }

  virtual bool equals(const Module *other) const override {
    if (other->get_type() != type) {
      return false;
    }

    auto other_cast = static_cast<const PacketModifyTCP *>(other);

    if (!kutil::solver_toolbox.are_exprs_always_equal(tcp_chunk,
                                                      other_cast->tcp_chunk)) {
      return false;
    }

    auto other_modifications = other_cast->get_modifications();

    if (modifications.size() != other_modifications.size()) {
      return false;
    }

    for (unsigned i = 0; i < modifications.size(); i++) {
      auto modification = modifications[i];
      auto other_modification = other_modifications[i];

      if (modification.byte != other_modification.byte) {
        return false;
      }

      if (!kutil::solver_toolbox.are_exprs_always_equal(
              modification.expr, other_modification.expr)) {
        return false;
      }
    }

    return true;
  }

  klee::ref<klee::Expr> get_tcp_chunk() const { return tcp_chunk; }

  const std::vector<modification_t> &get_modifications() const {
    return modifications;
  }
};
} // namespace x86_tofino
} // namespace targets
} // namespace synapse
