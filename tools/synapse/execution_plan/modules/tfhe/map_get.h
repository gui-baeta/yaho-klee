#pragma once

#include "tfhe_module.h"

namespace synapse {
namespace targets {
namespace tfhe {

class MapGet : public tfheModule {
private:
  addr_t map_addr;
  addr_t key_addr;
  klee::ref<klee::Expr> key;
  klee::ref<klee::Expr> value_out;
  klee::ref<klee::Expr> success;
  BDD::symbol_t map_has_this_key;

public:
  MapGet() : tfheModule(ModuleType::tfhe_MapGet, "MapGet") {}

  MapGet(BDD::Node_ptr node, addr_t _map_addr, addr_t _key_addr,
         klee::ref<klee::Expr> _key, klee::ref<klee::Expr> _value_out,
         klee::ref<klee::Expr> _success, const BDD::symbol_t &_map_has_this_key)
      : tfheModule(ModuleType::tfhe_MapGet, "MapGet", node), map_addr(_map_addr),
        key_addr(_key_addr), key(_key), value_out(_value_out),
        success(_success), map_has_this_key(_map_has_this_key) {}

private:
  processing_result_t process(const ExecutionPlan &ep,
                              BDD::Node_ptr node) override {
    processing_result_t result;

    auto casted = BDD::cast_node<BDD::Call>(node);

    if (!casted) {
      return result;
    }

    auto call = casted->get_call();

    if (call.function_name == BDD::symbex::FN_MAP_GET) {
      assert(!call.args[BDD::symbex::FN_MAP_ARG_MAP].expr.isNull());
      assert(!call.args[BDD::symbex::FN_MAP_ARG_KEY].expr.isNull());
      assert(!call.args[BDD::symbex::FN_MAP_ARG_KEY].in.isNull());
      assert(!call.ret.isNull());
      assert(!call.args[BDD::symbex::FN_MAP_ARG_OUT].out.isNull());

      auto _map = call.args[BDD::symbex::FN_MAP_ARG_MAP].expr;
      auto _key_addr = call.args[BDD::symbex::FN_MAP_ARG_KEY].expr;
      auto _key = call.args[BDD::symbex::FN_MAP_ARG_KEY].in;
      auto _success = call.ret;
      auto _value_out = call.args[BDD::symbex::FN_MAP_ARG_OUT].out;

      auto _generated_symbols = casted->get_local_generated_symbols();
      auto _map_has_this_key =
          BDD::get_symbol(_generated_symbols, BDD::symbex::MAP_HAS_THIS_KEY);

      auto _map_addr = kutil::expr_addr_to_obj_addr(_map);
      auto _key_addr_value = kutil::expr_addr_to_obj_addr(_key_addr);

      save_map(ep, _map_addr);

      auto new_module =
          std::make_shared<MapGet>(node, _map_addr, _key_addr_value, _key,
                                   _value_out, _success, _map_has_this_key);
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
    auto cloned = new MapGet(node, map_addr, key_addr, key, value_out, success,
                             map_has_this_key);
    return std::shared_ptr<Module>(cloned);
  }

  virtual bool equals(const Module *other) const override {
    if (other->get_type() != type) {
      return false;
    }

    auto other_cast = static_cast<const MapGet *>(other);

    if (map_addr != other_cast->get_map_addr()) {
      return false;
    }

    if (key_addr != other_cast->get_key_addr()) {
      return false;
    }

    if (!kutil::solver_toolbox.are_exprs_always_equal(key,
                                                      other_cast->get_key())) {
      return false;
    }

    if (map_has_this_key.label != other_cast->get_map_has_this_key().label) {
      return false;
    }

    if (!kutil::solver_toolbox.are_exprs_always_equal(
            value_out, other_cast->get_value_out())) {
      return false;
    }

    if (!kutil::solver_toolbox.are_exprs_always_equal(
            success, other_cast->get_success())) {
      return false;
    }

    return true;
  }

  addr_t get_map_addr() const { return map_addr; }
  addr_t get_key_addr() const { return key_addr; }
  klee::ref<klee::Expr> get_key() const { return key; }
  klee::ref<klee::Expr> get_value_out() const { return value_out; }
  klee::ref<klee::Expr> get_success() const { return success; }
  const BDD::symbol_t &get_map_has_this_key() const { return map_has_this_key; }
};
} // namespace tfhe
} // namespace targets
} // namespace synapse
