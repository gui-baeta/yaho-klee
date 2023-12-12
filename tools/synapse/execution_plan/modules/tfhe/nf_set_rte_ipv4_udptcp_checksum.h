#pragma once

#include "tfhe_module.h"

namespace synapse {
namespace targets {
namespace tfhe {

class SetIpv4UdpTcpChecksum : public tfheModule {
private:
  addr_t ip_header_addr;
  addr_t l4_header_addr;
  BDD::symbol_t checksum;

public:
  SetIpv4UdpTcpChecksum()
      : tfheModule(ModuleType::tfhe_SetIpv4UdpTcpChecksum, "SetIpChecksum") {}

  SetIpv4UdpTcpChecksum(BDD::Node_ptr node, addr_t _ip_header_addr,
                        addr_t _l4_header_addr, BDD::symbol_t _checksum)
      : tfheModule(ModuleType::tfhe_SetIpv4UdpTcpChecksum, "SetIpChecksum", node),
        ip_header_addr(_ip_header_addr), l4_header_addr(_l4_header_addr),
        checksum(_checksum) {}

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

      auto _ip_header = call.args[BDD::symbex::FN_SET_CHECKSUM_ARG_IP].expr;
      auto _l4_header = call.args[BDD::symbex::FN_SET_CHECKSUM_ARG_L4].expr;
      auto _p = call.args[BDD::symbex::FN_SET_CHECKSUM_ARG_PACKET].expr;

      auto _generated_symbols = casted->get_local_generated_symbols();
      auto _checksum = BDD::get_symbol(_generated_symbols, BDD::symbex::CHECKSUM);

      auto _ip_header_addr = kutil::expr_addr_to_obj_addr(_ip_header);
      auto _l4_header_addr = kutil::expr_addr_to_obj_addr(_l4_header);

      auto new_module = std::make_shared<SetIpv4UdpTcpChecksum>(
          node, _ip_header_addr, _l4_header_addr, _checksum);
      auto new_ep = ep.add_leaves(new_module, node->get_next());

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
    auto cloned = new SetIpv4UdpTcpChecksum(node, ip_header_addr,
                                            l4_header_addr, checksum);
    return std::shared_ptr<Module>(cloned);
  }

  virtual bool equals(const Module *other) const override {
    if (other->get_type() != type) {
      return false;
    }

    auto other_cast = static_cast<const SetIpv4UdpTcpChecksum *>(other);

    if (ip_header_addr != other_cast->get_ip_header_addr()) {
      return false;
    }

    if (l4_header_addr != other_cast->get_l4_header_addr()) {
      return false;
    }

    if (checksum.label != other_cast->get_checksum().label) {
      return false;
    }

    return true;
  }

  addr_t get_ip_header_addr() const { return ip_header_addr; }
  addr_t get_l4_header_addr() const { return l4_header_addr; }
  const BDD::symbol_t &get_checksum() const { return checksum; }
};
} // namespace tfhe
} // namespace targets
} // namespace synapse
