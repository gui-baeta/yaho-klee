#pragma once

#include "../module.h"
#include "../tofino/memory_bank.h"
#include "memory_bank.h"

namespace synapse {
namespace targets {
namespace x86_tofino {

class MapGet : public Module {
private:
  addr_t map_addr;
  klee::ref<klee::Expr> key;
  klee::ref<klee::Expr> map_has_this_key;
  klee::ref<klee::Expr> value_out;
  BDD::symbols_t generated_symbols;

public:
  MapGet()
      : Module(ModuleType::x86_Tofino_MapGet, TargetType::x86_Tofino,
               "MapGet") {}

  MapGet(BDD::Node_ptr node, addr_t _map_addr, klee::ref<klee::Expr> _key,
         klee::ref<klee::Expr> _map_has_this_key,
         klee::ref<klee::Expr> _value_out, BDD::symbols_t _generated_symbols)
      : Module(ModuleType::x86_Tofino_MapGet, TargetType::x86_Tofino, "MapGet",
               node),
        map_addr(_map_addr), key(_key), map_has_this_key(_map_has_this_key),
        value_out(_value_out), generated_symbols(_generated_symbols) {}

private:
  bool check_compatible_placements_decisions(const ExecutionPlan &ep,
                                             addr_t obj) const {
    auto mb = ep.get_memory_bank();

    if (!mb->has_placement_decision(obj)) {
      return true;
    }

    if (mb->check_placement_decision(obj, PlacementDecision::TofinoTable)) {
      return true;
    }

    return false;
  }

  processing_result_t process_map_get(const ExecutionPlan &ep,
                                      BDD::Node_ptr node) {
    processing_result_t result;

    auto casted = BDD::cast_node<BDD::Call>(node);

    if (!casted) {
      return result;
    }

    auto call = casted->get_call();

    assert(!call.args[BDD::symbex::FN_MAP_ARG_MAP].expr.isNull());
    assert(!call.args[BDD::symbex::FN_MAP_ARG_KEY].in.isNull());
    assert(!call.ret.isNull());
    assert(!call.args[BDD::symbex::FN_MAP_ARG_OUT].out.isNull());

    auto _map = call.args[BDD::symbex::FN_MAP_ARG_MAP].expr;
    auto _key = call.args[BDD::symbex::FN_MAP_ARG_KEY].in;
    auto _map_has_this_key = call.ret;
    auto _value_out = call.args[BDD::symbex::FN_MAP_ARG_OUT].out;

    auto _generated_symbols = casted->get_local_generated_symbols();

    auto _map_addr = kutil::expr_addr_to_obj_addr(_map);

    if (!check_compatible_placements_decisions(ep, _map_addr)) {
      return result;
    }

    auto mb = ep.get_memory_bank<x86TofinoMemoryBank>(x86_Tofino);
    auto saved = mb->has_data_structure(_map_addr);

    if (!saved) {
      auto map_ds = std::shared_ptr<x86TofinoMemoryBank::ds_t>(
          new x86TofinoMemoryBank::map_t(_map_addr, _value_out->getWidth(),
                                         node->get_id()));
      mb->add_data_structure(map_ds);
    }

    auto new_module =
        std::make_shared<MapGet>(node, _map_addr, _key, _map_has_this_key,
                                 _value_out, _generated_symbols);
    auto new_ep = ep.add_leaf(new_module, node->get_next());

    result.module = new_module;
    result.next_eps.push_back(new_ep);

    return result;
  }

  processing_result_t process_vector_borrow(const ExecutionPlan &ep,
                                            BDD::Node_ptr node) {
    processing_result_t result;

    auto casted = BDD::cast_node<BDD::Call>(node);

    if (!casted) {
      return result;
    }

    auto call = casted->get_call();

    assert(!call.args[BDD::symbex::FN_VECTOR_ARG_VECTOR].expr.isNull());
    assert(!call.args[BDD::symbex::FN_VECTOR_ARG_INDEX].expr.isNull());
    assert(!call.args[BDD::symbex::FN_VECTOR_ARG_OUT].out.isNull());
    assert(!call.extra_vars[BDD::symbex::FN_VECTOR_EXTRA].second.isNull());

    auto _vector = call.args[BDD::symbex::FN_VECTOR_ARG_VECTOR].expr;
    auto _index = call.args[BDD::symbex::FN_VECTOR_ARG_INDEX].expr;
    auto _borrowed_cell = call.extra_vars[BDD::symbex::FN_VECTOR_EXTRA].second;

    auto _generated_symbols = casted->get_local_generated_symbols();

    auto _vector_addr = kutil::expr_addr_to_obj_addr(_vector);

    if (!check_compatible_placements_decisions(ep, _vector_addr)) {
      return result;
    }

    auto vector_borrow_data =
        x86TofinoMemoryBank::vector_borrow_t{_vector_addr, _borrowed_cell};

    auto mb = ep.get_memory_bank<x86TofinoMemoryBank>(x86_Tofino);
    auto saved = mb->has_data_structure(_vector_addr);

    if (!saved) {
      auto map_ds = std::shared_ptr<x86TofinoMemoryBank::ds_t>(
          new x86TofinoMemoryBank::map_t(
              _vector_addr, _borrowed_cell->getWidth(), node->get_id()));
      mb->add_data_structure(map_ds);
    }

    auto new_module =
        std::make_shared<MapGet>(node, _vector_addr, _index, nullptr,
                                 _borrowed_cell, _generated_symbols);
    auto new_ep = ep.add_leaf(new_module, node->get_next());

    result.module = new_module;
    result.next_eps.push_back(new_ep);

    return result;
  }

  processing_result_t process(const ExecutionPlan &ep,
                              BDD::Node_ptr node) override {
    processing_result_t result;

    auto casted = BDD::cast_node<BDD::Call>(node);

    if (!casted) {
      return result;
    }

    auto call = casted->get_call();

    if (call.function_name == BDD::symbex::FN_MAP_GET) {
      return process_map_get(ep, node);
    }

    if (call.function_name == BDD::symbex::FN_VECTOR_BORROW) {
      return process_vector_borrow(ep, node);
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

    if (map_addr != other_cast->get_map_addr()) {
      return false;
    }

    if (!kutil::solver_toolbox.are_exprs_always_equal(key,
                                                      other_cast->get_key())) {
      return false;
    }

    if (map_has_this_key.isNull() !=
        other_cast->get_map_has_this_key().isNull()) {
      return false;
    }

    if (!kutil::solver_toolbox.are_exprs_always_equal(
            value_out, other_cast->get_value_out())) {
      return false;
    }

    return true;
  }

  const addr_t &get_map_addr() const { return map_addr; }
  const klee::ref<klee::Expr> &get_key() const { return key; }
  const klee::ref<klee::Expr> &get_map_has_this_key() const {
    return map_has_this_key;
  }
  const klee::ref<klee::Expr> &get_value_out() const { return value_out; }

  const BDD::symbols_t &get_generated_symbols() const {
    return generated_symbols;
  }
};
} // namespace x86_tofino
} // namespace targets
} // namespace synapse
