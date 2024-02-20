#pragma once

#include "../module.h"
#include <netinet/in.h>

namespace synapse {
namespace targets {
namespace x86_tofino {

class PacketParseTCPUDP : public Module {
private:
  klee::ref<klee::Expr> chunk;

public:
  PacketParseTCPUDP()
      : Module(ModuleType::x86_Tofino_PacketParseTCPUDP, TargetType::x86_Tofino,
               "PacketParseTCPUDP") {}

  PacketParseTCPUDP(BDD::Node_ptr node, klee::ref<klee::Expr> _chunk)
      : Module(ModuleType::x86_Tofino_PacketParseTCPUDP, TargetType::x86_Tofino,
               "PacketParseTCPUDP", node),
        chunk(_chunk) {}

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

  bool is_valid_tcpudp(const BDD::Node *ipv4_node, klee::ref<klee::Expr> len,
                       const klee::ConstraintManager &constraints) {
    assert(ipv4_node);
    assert(ipv4_node->get_type() == BDD::Node::NodeType::CALL);

    auto call_node = static_cast<const BDD::Call *>(ipv4_node);
    auto call = call_node->get_call();

    auto ipv4_chunk = call.extra_vars[BDD::symbex::FN_BORROW_CHUNK_EXTRA].second;

    assert(!ipv4_chunk.isNull());
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

    if (len->getKind() != klee::Expr::Kind::Constant) {
      return false;
    }

    auto _4 = kutil::solver_toolbox.exprBuilder->Constant(4, 4 * 8);
    auto len_eq_4 = kutil::solver_toolbox.exprBuilder->Eq(len, _4);

    if (!kutil::solver_toolbox.is_expr_always_true(constraints, len_eq_4)) {
      return false;
    }

    auto next_proto_id_expr =
        kutil::solver_toolbox.exprBuilder->Extract(ipv4_chunk, 9 * 8, 8);

    auto next_proto_id_expr_tcp =
        kutil::solver_toolbox.exprBuilder->Constant(IPPROTO_TCP, 8);
    auto next_proto_id_expr_udp =
        kutil::solver_toolbox.exprBuilder->Constant(IPPROTO_UDP, 8);

    auto eq = kutil::solver_toolbox.exprBuilder->Or(
        kutil::solver_toolbox.exprBuilder->Eq(next_proto_id_expr,
                                              next_proto_id_expr_tcp),
        kutil::solver_toolbox.exprBuilder->Eq(next_proto_id_expr,
                                              next_proto_id_expr_udp));

    return kutil::solver_toolbox.is_expr_always_true(constraints, eq);
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

    // TCP/UDP should come after IPv4Consume
    auto all_prev_packet_borrow_next_chunk =
        get_prev_fn(ep, node, BDD::symbex::FN_BORROW_CHUNK);

    if (all_prev_packet_borrow_next_chunk.size() < 2) {
      return result;
    }

    assert(!call.args[BDD::symbex::FN_BORROW_CHUNK_ARG_LEN].expr.isNull());
    assert(!call.extra_vars[BDD::symbex::FN_BORROW_CHUNK_EXTRA].second.isNull());

    auto _length = call.args[BDD::symbex::FN_BORROW_CHUNK_ARG_LEN].expr;
    auto _chunk = call.extra_vars[BDD::symbex::FN_BORROW_CHUNK_EXTRA].second;

    auto valid = true;

    auto ethernet_chunk = all_prev_packet_borrow_next_chunk.rbegin()[0].get();
    auto ipv4_chunk = all_prev_packet_borrow_next_chunk.rbegin()[1].get();

    valid &= is_valid_ipv4(ethernet_chunk, node->get_constraints());
    valid &= is_valid_tcpudp(ipv4_chunk, _length, node->get_constraints());

    if (!valid) {
      return result;
    }

    auto new_module = std::make_shared<PacketParseTCPUDP>(node, _chunk);
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
    auto cloned = new PacketParseTCPUDP(node, chunk);
    return std::shared_ptr<Module>(cloned);
  }

  virtual bool equals(const Module *other) const override {
    return other->get_type() == type;
  }

  const klee::ref<klee::Expr> &get_chunk() const { return chunk; }
};
} // namespace x86_tofino
} // namespace targets
} // namespace synapse
