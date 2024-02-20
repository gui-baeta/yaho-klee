#pragma once

#include "../module.h"
#include <netinet/in.h>

namespace synapse {
namespace targets {
namespace x86_tofino {

class PacketParseIPv4Options : public Module {
private:
  klee::ref<klee::Expr> chunk;
  klee::ref<klee::Expr> length;

public:
  PacketParseIPv4Options()
      : Module(ModuleType::x86_Tofino_PacketParseIPv4Options,
               TargetType::x86_Tofino, "PacketParseIPv4Options") {}

  PacketParseIPv4Options(BDD::Node_ptr node, klee::ref<klee::Expr> _chunk,
                         klee::ref<klee::Expr> _length)
      : Module(ModuleType::x86_Tofino_PacketParseIPv4Options,
               TargetType::x86_Tofino, "PacketParseIPv4Options", node),
        chunk(_chunk), length(_length) {}

private:
  bool is_valid_ipv4(const BDD::Node *ethernet_node,
                     const klee::ConstraintManager &constraints) {
    assert(ethernet_node);
    assert(ethernet_node->get_type() == BDD::Node::NodeType::CALL);

    auto call_node = static_cast<const BDD::Call *>(ethernet_node);
    auto call = call_node->get_call();

    auto ethernet_chunk = call.extra_vars[BDD::symbex::FN_BORROW_CHUNK_EXTRA].second;

    assert(!ethernet_chunk.isNull());

    auto eth_type_expr = kutil::solver_toolbox.exprBuilder->Extract(
        ethernet_chunk, 12 * 8, 2 * 8);
    auto eth_type_ipv4 = kutil::solver_toolbox.exprBuilder->Constant(
        UINT_16_SWAP_ENDIANNESS(0x0800), 2 * 8);
    auto eq =
        kutil::solver_toolbox.exprBuilder->Eq(eth_type_expr, eth_type_ipv4);

    return kutil::solver_toolbox.is_expr_always_true(constraints, eq);
  }

  bool is_valid_ip_options(const BDD::Node *ip_options_node,
                           klee::ref<klee::Expr> len,
                           const klee::ConstraintManager &constraints) {
    assert(ip_options_node);
    assert(!len.isNull());

    // TODO: check if there are ip options
    // this is the constraint we should be looking for:
    /*
      (Eq (w32 0)
        (Or w32 (ZExt w32 (Eq (w32 0)
          (Or w32 (ZExt w32 (Eq (w8 6) N0:(Read w8 (w32 50) packet_chunks)))
            (ZExt w32 (Eq (w8 17) N0)))))
              (ZExt w32 (Ult (ZExt w64 (Sub w32 (ZExt w32 (ReadLSB w16 (w32 0)
      pkt_len)) (Extract w32 0 (Add w64 (w64 34) (ZExt w64 (Extract w16 0
      (Extract w32 0 (Mul w64 (w64 4) (SExt w64 (Extract w32 0 (Add w64 (w64
      18446744073709551611) (SExt w64 (ZExt w32 (Extract w8 0 (And w32 (ZExt w32
      (Read w8 (w32 41) packet_chunks)) (w32 15))))))))))))))))
    */

    // we are just going to assume that if the requested length is not constant,
    // then this is a request for ip options

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

    if (call.function_name != BDD::symbex::FN_BORROW_CHUNK) {
      return result;
    }

    // IP options should come after IPv4Consume
    auto all_prev_packet_borrow_next_chunk =
        get_prev_fn(ep, node, BDD::symbex::FN_BORROW_CHUNK);

    if (all_prev_packet_borrow_next_chunk.size() < 2) {
      return result;
    }

    assert(!call.args[BDD::symbex::FN_BORROW_CHUNK_ARG_LEN].expr.isNull());
    assert(!call.extra_vars[BDD::symbex::FN_BORROW_CHUNK_EXTRA].second.isNull());

    auto _length = call.args[BDD::symbex::FN_BORROW_CHUNK_ARG_LEN].expr;
    auto _chunk = call.extra_vars[BDD::symbex::FN_BORROW_CHUNK_EXTRA].second;

    auto valid = is_valid_ipv4(all_prev_packet_borrow_next_chunk[1].get(),
                               node->get_constraints());

    valid &= is_valid_ip_options(all_prev_packet_borrow_next_chunk[0].get(),
                                 _length, node->get_constraints());

    if (!valid) {
      return result;
    }

    auto new_module =
        std::make_shared<PacketParseIPv4Options>(node, _chunk, _length);
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
    auto cloned = new PacketParseIPv4Options(node, chunk, length);
    return std::shared_ptr<Module>(cloned);
  }

  virtual bool equals(const Module *other) const override {
    if (other->get_type() != type) {
      return false;
    }

    auto other_cast = static_cast<const PacketParseIPv4Options *>(other);

    if (!kutil::solver_toolbox.are_exprs_always_equal(chunk,
                                                      other_cast->chunk)) {
      return false;
    }

    if (!kutil::solver_toolbox.are_exprs_always_equal(length,
                                                      other_cast->length)) {
      return false;
    }

    return true;
  }

  const klee::ref<klee::Expr> &get_chunk() const { return chunk; }
  const klee::ref<klee::Expr> &get_length() const { return length; }
};
} // namespace x86_tofino
} // namespace targets
} // namespace synapse
