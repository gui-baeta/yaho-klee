#pragma once

#include "../internals/internals.h"

namespace BDD {
namespace emulation {

inline void __packet_borrow_next_secret(const BDD& bdd, const Call *call_node, pkt_t &pkt,
                                       time_ns_t time, state_t &state,
                                       meta_t &meta, context_t &ctx,
                                       const cfg_t &cfg) {
  auto call = call_node->get_call();

  assert(!call.extra_vars[symbex::FN_BORROW_CHUNK_EXTRA].second.isNull());
  assert(!call.args[symbex::FN_BORROW_CHUNK_ARG_LEN].expr.isNull());

  auto chunk_expr = call.extra_vars[symbex::FN_BORROW_CHUNK_EXTRA].second;
  auto length = call.args[symbex::FN_BORROW_CHUNK_ARG_LEN].expr;

  assert(length->getKind() == klee::Expr::Kind::Constant);
  auto chunk_size_bytes = kutil::solver_toolbox.value_from_expr(length);

  concretize(ctx, chunk_expr, pkt.data);
  pkt.data += chunk_size_bytes;
}

inline std::pair<std::string, operation_ptr> packet_borrow_next_secret() {
  return {symbex::FN_BORROW_SECRET, __packet_borrow_next_secret};
}

} // namespace emulation
} // namespace BDD