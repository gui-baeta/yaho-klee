#pragma once

#include "tfhe_module.h"

namespace synapse {
namespace targets {
namespace tfhe {

class PacketGetUnreadLength : public tfheModule {
private:
  addr_t p_addr;
  klee::ref<klee::Expr> unread_length;

  BDD::symbols_t generated_symbols;

public:
  PacketGetUnreadLength()
      : tfheModule(ModuleType::tfhe_PacketGetUnreadLength,
                  "PacketGetUnreadLength") {}

  PacketGetUnreadLength(BDD::Node_ptr node, addr_t _p_addr,
                        klee::ref<klee::Expr> _unread_length,
                        BDD::symbols_t _generated_symbols)
      : tfheModule(ModuleType::tfhe_PacketGetUnreadLength,
                  "PacketGetUnreadLength", node),
        p_addr(_p_addr), unread_length(_unread_length),
        generated_symbols(_generated_symbols) {}

private:
  processing_result_t process(const ExecutionPlan &ep,
                              BDD::Node_ptr node) override {
    processing_result_t result;

    auto casted = BDD::cast_node<BDD::Call>(node);

    if (!casted) {
      return result;
    }

    auto call = casted->get_call();

    if (call.function_name == BDD::symbex::FN_GET_UNREAD_LEN) {
      assert(!call.ret.isNull());
      assert(!call.args[BDD::symbex::FN_BORROW_ARG_PACKET].expr.isNull());

      auto _p = call.args[BDD::symbex::FN_BORROW_ARG_PACKET].expr;
      auto _unread_length = call.ret;

      auto _generated_symbols = casted->get_local_generated_symbols();
      auto _p_addr = kutil::expr_addr_to_obj_addr(_p);

      auto new_module = std::make_shared<PacketGetUnreadLength>(
          node, _p_addr, _unread_length, _generated_symbols);
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
    auto cloned = new PacketGetUnreadLength(node, p_addr, unread_length,
                                            generated_symbols);
    return std::shared_ptr<Module>(cloned);
  }

  virtual bool equals(const Module *other) const override {
    if (other->get_type() != type) {
      return false;
    }

    auto other_cast = static_cast<const PacketGetUnreadLength *>(other);

    if (p_addr != other_cast->get_p_addr()) {
      return false;
    }

    if (!kutil::solver_toolbox.are_exprs_always_equal(
            unread_length, other_cast->get_unread_length())) {
      return false;
    }

    if (generated_symbols != other_cast->generated_symbols) {
      return false;
    }

    return true;
  }

  const addr_t &get_p_addr() const { return p_addr; }
  const klee::ref<klee::Expr> &get_unread_length() const {
    return unread_length;
  }

  const BDD::symbols_t &get_generated_symbols() const {
    return generated_symbols;
  }
};
} // namespace tfhe
} // namespace targets
} // namespace synapse
