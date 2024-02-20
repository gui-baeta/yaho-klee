#pragma once

#include "../module.h"

namespace synapse {
namespace targets {
namespace x86_bmv2 {

class MapPut : public Module {
private:
  klee::ref<klee::Expr> map_addr;
  klee::ref<klee::Expr> key_addr;
  klee::ref<klee::Expr> key;
  klee::ref<klee::Expr> value;

public:
  MapPut()
      : Module(ModuleType::x86_BMv2_MapPut, TargetType::x86_BMv2, "MapPut") {}

  MapPut(BDD::Node_ptr node, klee::ref<klee::Expr> _map_addr,
         klee::ref<klee::Expr> _key_addr, klee::ref<klee::Expr> _key,
         klee::ref<klee::Expr> _value)
      : Module(ModuleType::x86_BMv2_MapPut, TargetType::x86_BMv2, "MapPut",
               node),
        map_addr(_map_addr), key_addr(_key_addr), key(_key), value(_value) {}

private:
  processing_result_t process(const ExecutionPlan &ep,
                              BDD::Node_ptr node) override {
    processing_result_t result;

    auto casted = BDD::cast_node<BDD::Call>(node);

    if (!casted) {
      return result;
    }

    auto call = casted->get_call();

    if (call.function_name == BDD::symbex::FN_MAP_PUT) {
      assert(!call.args[BDD::symbex::FN_MAP_ARG_MAP].expr.isNull());
      assert(!call.args[BDD::symbex::FN_MAP_ARG_KEY].expr.isNull());
      assert(!call.args[BDD::symbex::FN_MAP_ARG_KEY].in.isNull());
      assert(!call.args[BDD::symbex::FN_MAP_ARG_VALUE].expr.isNull());

      auto _map_addr = call.args[BDD::symbex::FN_MAP_ARG_MAP].expr;
      auto _key_addr = call.args[BDD::symbex::FN_MAP_ARG_KEY].expr;
      auto _key = call.args[BDD::symbex::FN_MAP_ARG_KEY].in;
      auto _value = call.args[BDD::symbex::FN_MAP_ARG_VALUE].expr;

      auto new_module =
          std::make_shared<MapPut>(node, _map_addr, _key_addr, _key, _value);
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
    auto cloned = new MapPut(node, map_addr, key_addr, key, value);
    return std::shared_ptr<Module>(cloned);
  }

  virtual bool equals(const Module *other) const override {
    if (other->get_type() != type) {
      return false;
    }

    auto other_cast = static_cast<const MapPut *>(other);

    if (!kutil::solver_toolbox.are_exprs_always_equal(
            map_addr, other_cast->get_map_addr())) {
      return false;
    }

    if (!kutil::solver_toolbox.are_exprs_always_equal(
            key_addr, other_cast->get_key_addr())) {
      return false;
    }

    if (!kutil::solver_toolbox.are_exprs_always_equal(key,
                                                      other_cast->get_key())) {
      return false;
    }

    if (!kutil::solver_toolbox.are_exprs_always_equal(
            value, other_cast->get_value())) {
      return false;
    }

    return true;
  }

  const klee::ref<klee::Expr> &get_map_addr() const { return map_addr; }
  const klee::ref<klee::Expr> &get_key_addr() const { return key_addr; }
  const klee::ref<klee::Expr> &get_key() const { return key; }
  const klee::ref<klee::Expr> &get_value() const { return value; }
};
} // namespace x86_bmv2
} // namespace targets
} // namespace synapse
