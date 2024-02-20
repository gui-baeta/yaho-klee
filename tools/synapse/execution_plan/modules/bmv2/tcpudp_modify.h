#pragma once

#include "../module.h"
#include "ignore.h"

namespace synapse {
namespace targets {
namespace bmv2 {

class TcpUdpModify : public Module {
private:
  std::vector<modification_t> modifications;

public:
  TcpUdpModify()
      : Module(ModuleType::BMv2_TcpUdpModify, TargetType::BMv2,
               "TcpUdpModify") {}

  TcpUdpModify(BDD::Node_ptr node,
               const std::vector<modification_t> &_modifications)
      : Module(ModuleType::BMv2_TcpUdpModify, TargetType::BMv2, "TcpUdpModify",
               node),
        modifications(_modifications) {}

private:
  klee::ref<klee::Expr> get_tcpudp_chunk(const BDD::Node *node) const {
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

    auto borrow_tcpudp = all_prev_packet_borrow_next_chunk[0].get();

    auto curr_tcpudp_chunk = call.args[BDD::symbex::FN_BORROW_CHUNK_EXTRA].in;
    auto prev_tcpudp_chunk = get_tcpudp_chunk(borrow_tcpudp);

    assert(curr_tcpudp_chunk->getWidth() == 4 * 8);
    assert(prev_tcpudp_chunk->getWidth() == 4 * 8);

    auto _modifications =
        build_modifications(prev_tcpudp_chunk, curr_tcpudp_chunk);

    if (_modifications.size() == 0) {
      auto new_module = std::make_shared<Ignore>(node);
      auto new_ep = ep.ignore_leaf(node->get_next(), TargetType::BMv2);

      result.module = new_module;
      result.next_eps.push_back(new_ep);

      return result;
    }

    auto new_module = std::make_shared<TcpUdpModify>(node, _modifications);
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
    auto cloned = new TcpUdpModify(node, modifications);
    return std::shared_ptr<Module>(cloned);
  }

  virtual bool equals(const Module *other) const override {
    if (other->get_type() != type) {
      return false;
    }

    auto other_cast = static_cast<const TcpUdpModify *>(other);

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

  const std::vector<modification_t> &get_modifications() const {
    return modifications;
  }
};
} // namespace bmv2
} // namespace targets
} // namespace synapse
