#pragma once

#include "../module.h"
#include "../tofino/memory_bank.h"
#include "memory_bank.h"

namespace synapse {
namespace targets {
namespace x86_tofino {

class MapErase : public Module {
private:
  addr_t map_addr;
  klee::ref<klee::Expr> key;
  addr_t trash;

public:
  MapErase()
      : Module(ModuleType::x86_Tofino_MapErase, TargetType::x86_Tofino, "MapErase") {}

  MapErase(BDD::Node_ptr node, addr_t _map_addr, klee::ref<klee::Expr> _key,
           addr_t _trash)
      : Module(ModuleType::x86_Tofino_MapErase, TargetType::x86_Tofino, "MapErase",
               node),
        map_addr(_map_addr), key(_key), trash(_trash) {}

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

  processing_result_t process(const ExecutionPlan &ep,
                              BDD::Node_ptr node) override {
    processing_result_t result;

    auto casted = BDD::cast_node<BDD::Call>(node);

    if (!casted) {
      return result;
    }

    auto call = casted->get_call();

    if (call.function_name == BDD::symbex::FN_MAP_ERASE) {
      assert(!call.args[BDD::symbex::FN_MAP_ARG_MAP].expr.isNull());
      assert(!call.args[BDD::symbex::FN_MAP_ARG_KEY].in.isNull());
      assert(!call.args[BDD::symbex::FN_MAP_ARG_TRASH].out.isNull());

      auto _map = call.args[BDD::symbex::FN_MAP_ARG_MAP].expr;
      auto _key = call.args[BDD::symbex::FN_MAP_ARG_KEY].in;
      auto _trash = call.args[BDD::symbex::FN_MAP_ARG_TRASH].out;

      auto _map_addr = kutil::expr_addr_to_obj_addr(_map);
      auto _trash_addr = kutil::expr_addr_to_obj_addr(_trash);

      if (!check_compatible_placements_decisions(ep, _map_addr)) {
        return result;
      }

      auto mb = ep.get_memory_bank<x86TofinoMemoryBank>(x86_Tofino);
      auto saved = mb->has_data_structure(_map_addr);
      assert(saved);

      auto new_module =
          std::make_shared<MapErase>(node, _map_addr, _key, _trash_addr);
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
    auto cloned = new MapErase(node, map_addr, key, trash);
    return std::shared_ptr<Module>(cloned);
  }

  virtual bool equals(const Module *other) const override {
    if (other->get_type() != type) {
      return false;
    }

    auto other_cast = static_cast<const MapErase *>(other);

    if (map_addr != other_cast->get_map_addr()) {
      return false;
    }

    if (!kutil::solver_toolbox.are_exprs_always_equal(key,
                                                      other_cast->get_key())) {
      return false;
    }

    if (trash != other_cast->get_trash()) {
      return false;
    }

    return true;
  }

  addr_t get_map_addr() const { return map_addr; }
  klee::ref<klee::Expr> get_key() const { return key; }
  addr_t get_trash() const { return trash; }
};
} // namespace x86_tofino
} // namespace targets
} // namespace synapse
