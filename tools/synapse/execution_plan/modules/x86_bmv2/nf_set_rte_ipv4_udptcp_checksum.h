#pragma once

#include "../module.h"

namespace synapse {
namespace targets {
namespace x86_bmv2 {

class SetIpv4UdpTcpChecksum : public Module {
private:
  klee::ref<klee::Expr> ip_header_addr;
  klee::ref<klee::Expr> l4_header_addr;
  klee::ref<klee::Expr> p_addr;

  BDD::symbols_t generated_symbols;

public:
  SetIpv4UdpTcpChecksum()
      : Module(ModuleType::x86_BMv2_SetIpv4UdpTcpChecksum, TargetType::x86_BMv2,
               "SetIpChecksum") {}

  SetIpv4UdpTcpChecksum(BDD::Node_ptr node,
                        klee::ref<klee::Expr> _ip_header_addr,
                        klee::ref<klee::Expr> _l4_header_addr,
                        klee::ref<klee::Expr> _p_addr,
                        BDD::symbols_t _generated_symbols)
      : Module(ModuleType::x86_BMv2_SetIpv4UdpTcpChecksum, TargetType::x86_BMv2,
               "SetIpChecksum", node),
        ip_header_addr(_ip_header_addr), l4_header_addr(_l4_header_addr),
        p_addr(_p_addr), generated_symbols(_generated_symbols) {}

private:
  processing_result_t process(const ExecutionPlan &ep,
                              BDD::Node_ptr node) override {
    processing_result_t result;

    auto casted = BDD::cast_node<BDD::Call>(node);

    if (!casted) {
      return result;
    }

    auto call = casted->get_call();

    if (call.function_name == BDD::symbex::FN_SET_CHECKSUM) {
      assert(!call.args[BDD::symbex::FN_SET_CHECKSUM_ARG_IP].expr.isNull());
      assert(!call.args[BDD::symbex::FN_SET_CHECKSUM_ARG_L4].expr.isNull());
      assert(!call.args[BDD::symbex::FN_SET_CHECKSUM_ARG_PACKET].expr.isNull());

      auto _ip_header_addr = call.args[BDD::symbex::FN_SET_CHECKSUM_ARG_IP].expr;
      auto _l4_header_addr = call.args[BDD::symbex::FN_SET_CHECKSUM_ARG_L4].expr;
      auto _p_addr = call.args[BDD::symbex::FN_SET_CHECKSUM_ARG_PACKET].expr;
      auto _generated_symbols = casted->get_local_generated_symbols();

      auto new_module = std::make_shared<SetIpv4UdpTcpChecksum>(
          node, _ip_header_addr, _l4_header_addr, _p_addr, _generated_symbols);
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
    auto cloned = new SetIpv4UdpTcpChecksum(
        node, ip_header_addr, l4_header_addr, p_addr, generated_symbols);
    return std::shared_ptr<Module>(cloned);
  }

  virtual bool equals(const Module *other) const override {
    if (other->get_type() != type) {
      return false;
    }

    auto other_cast = static_cast<const SetIpv4UdpTcpChecksum *>(other);

    if (!kutil::solver_toolbox.are_exprs_always_equal(
            ip_header_addr, other_cast->get_ip_header_addr())) {
      return false;
    }

    if (!kutil::solver_toolbox.are_exprs_always_equal(
            l4_header_addr, other_cast->get_l4_header_addr())) {
      return false;
    }

    if (!kutil::solver_toolbox.are_exprs_always_equal(
            p_addr, other_cast->get_p_addr())) {
      return false;
    }

    if (generated_symbols != other_cast->generated_symbols) {
      return false;
    }

    return true;
  }

  const klee::ref<klee::Expr> &get_ip_header_addr() const {
    return ip_header_addr;
  }
  const klee::ref<klee::Expr> &get_l4_header_addr() const {
    return l4_header_addr;
  }
  const klee::ref<klee::Expr> &get_p_addr() const { return p_addr; }

  const BDD::symbols_t &get_generated_symbols() const {
    return generated_symbols;
  }
};
} // namespace x86_bmv2
} // namespace targets
} // namespace synapse
