#pragma once

#include "../module.h"

namespace synapse {
namespace targets {
namespace x86_bmv2 {

class MapGet : public Module {
private:
  klee::ref<klee::Expr> map_addr;
  klee::ref<klee::Expr> key;
  klee::ref<klee::Expr> map_has_this_key;
  klee::ref<klee::Expr> value_out;

  BDD::symbols_t generated_symbols;

public:
  MapGet()
      : Module(ModuleType::x86_BMv2_MapGet, TargetType::x86_BMv2, "MapGet") {}

  MapGet(BDD::Node_ptr node, klee::ref<klee::Expr> _map_addr,
         klee::ref<klee::Expr> _key, klee::ref<klee::Expr> _map_has_this_key,
         klee::ref<klee::Expr> _value_out, BDD::symbols_t _generated_symbols)
      : Module(ModuleType::x86_BMv2_MapGet, TargetType::x86_BMv2, "MapGet",
               node),
        map_addr(_map_addr), key(_key), map_has_this_key(_map_has_this_key),
        value_out(_value_out), generated_symbols(_generated_symbols) {}

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
      assert(!call.args[BDD::symbex::FN_MAP_ARG_KEY].in.isNull());
      assert(!call.ret.isNull());
      assert(!call.args[BDD::symbex::FN_MAP_ARG_OUT].out.isNull());

      auto _map_addr = call.args[BDD::symbex::FN_MAP_ARG_MAP].expr;
      auto _key = call.args[BDD::symbex::FN_MAP_ARG_KEY].in;
      auto _map_has_this_key = call.ret;
      auto _value_out = call.args[BDD::symbex::FN_MAP_ARG_OUT].out;

      auto _generated_symbols = casted->get_local_generated_symbols();

      auto new_module =
          std::make_shared<MapGet>(node, _map_addr, _key, _map_has_this_key,
                                   _value_out, _generated_symbols);
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
    auto cloned = new MapGet(node, map_addr, key, map_has_this_key, value_out,
                             generated_symbols);
    return std::shared_ptr<Module>(cloned);
  }

  virtual bool equals(const Module *other) const override {
    if (other->get_type() != type) {
      return false;
    }

    auto other_cast = static_cast<const MapGet *>(other);

    if (!kutil::solver_toolbox.are_exprs_always_equal(
            map_addr, other_cast->get_map_addr())) {
      return false;
    }

    if (!kutil::solver_toolbox.are_exprs_always_equal(key,
                                                      other_cast->get_key())) {
      return false;
    }

    if (!kutil::solver_toolbox.are_exprs_always_equal(
            map_has_this_key, other_cast->get_map_has_this_key())) {
      return false;
    }

    if (!kutil::solver_toolbox.are_exprs_always_equal(
            value_out, other_cast->get_value_out())) {
      return false;
    }

    if (generated_symbols != other_cast->generated_symbols) {
      return false;
    }

    return true;
  }

  const klee::ref<klee::Expr> &get_map_addr() const { return map_addr; }
  const klee::ref<klee::Expr> &get_key() const { return key; }
  const klee::ref<klee::Expr> &get_map_has_this_key() const {
    return map_has_this_key;
  }
  const klee::ref<klee::Expr> &get_value_out() const { return value_out; }

  const BDD::symbols_t &get_generated_symbols() const {
    return generated_symbols;
  }
};
} // namespace x86_bmv2
} // namespace targets
} // namespace synapse
