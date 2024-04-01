#include "score.h"
#include "../execution_plan/execution_plan.h"
#include "../execution_plan/modules/modules.h"
#include "../log.h"

namespace synapse {

Score::score_value_t Score::get_nr_nodes(const ExecutionPlan &ep) const {
  const auto &meta = ep.get_meta();
  return meta.nodes;
}

std::vector<ExecutionPlanNode_ptr>
Score::get_nodes_with_type(const ExecutionPlan &ep,
                           const std::vector<Module::ModuleType> &types) const {
  std::vector<ExecutionPlanNode_ptr> found;

  auto root = ep.get_root();

  if (!root) {
    return found;
  }

  auto nodes = std::vector<ExecutionPlanNode_ptr>{root};

  while (nodes.size()) {
    auto node = nodes[0];
    nodes.erase(nodes.begin());
    assert(node);

    auto module = node->get_module();
    assert(module);

    auto found_it = std::find(types.begin(), types.end(), module->get_type());

    if (found_it != types.end()) {
      found.push_back(node);
    }

    for (auto branch : node->get_next()) {
      nodes.push_back(branch);
    }
  }

  return found;
}

Score::score_value_t Score::get_nr_counters(const ExecutionPlan &ep) const {
  auto nodes =
      get_nodes_with_type(ep, {Module::ModuleType::Tofino_CounterRead,
                               Module::ModuleType::Tofino_CounterIncrement});
  return nodes.size();
}

Score::score_value_t
Score::get_nr_simple_tables(const ExecutionPlan &ep) const {
  auto nodes =
      get_nodes_with_type(ep, {Module::ModuleType::Tofino_TableLookup});
  return nodes.size();
}

Score::score_value_t
Score::get_nr_int_allocator_ops(const ExecutionPlan &ep) const {
  auto nodes = get_nodes_with_type(
      ep, {
              Module::ModuleType::Tofino_IntegerAllocatorAllocate,
              Module::ModuleType::Tofino_IntegerAllocatorQuery,
              Module::ModuleType::Tofino_IntegerAllocatorRejuvenate,
          });

  return nodes.size();
}

Score::score_value_t Score::get_depth(const ExecutionPlan &ep) const {
  const auto &meta = ep.get_meta();
  return meta.depth;
}

Score::score_value_t Score::get_nr_switch_nodes(const ExecutionPlan &ep) const {
  auto switch_nodes = 0;
  const auto &meta = ep.get_meta();
  const auto &nodes_per_target = meta.nodes_per_target;

  auto bmv2_nodes_it = nodes_per_target.find(TargetType::BMv2);
  auto tofino_nodes_it = nodes_per_target.find(TargetType::Tofino);

  if (bmv2_nodes_it != nodes_per_target.end()) {
    switch_nodes += bmv2_nodes_it->second;
  }

  if (tofino_nodes_it != nodes_per_target.end()) {
    switch_nodes += tofino_nodes_it->second;
  }

  auto send_to_controller =
      get_nodes_with_type(ep, {
                                  Module::ModuleType::Tofino_SendToController,
                              });

  // Let's ignore the SendToController nodes
  return switch_nodes - send_to_controller.size();
}

Score::score_value_t
Score::get_nr_controller_nodes(const ExecutionPlan &ep) const {
  auto controller_nodes = 0;
  const auto &meta = ep.get_meta();
  const auto &nodes_per_target = meta.nodes_per_target;

  auto bmv2_controller_nodes_it = nodes_per_target.find(TargetType::x86_BMv2);
  auto tofino_controller_nodes_it =
      nodes_per_target.find(TargetType::x86_Tofino);

  if (bmv2_controller_nodes_it != nodes_per_target.end()) {
    controller_nodes += bmv2_controller_nodes_it->second;
  }

  if (tofino_controller_nodes_it != nodes_per_target.end()) {
    controller_nodes += tofino_controller_nodes_it->second;
  }

  return controller_nodes;
}

Score::score_value_t
Score::get_nr_reordered_nodes(const ExecutionPlan &ep) const {
  const auto &meta = ep.get_meta();
  return meta.reordered_nodes;
}

Score::score_value_t
Score::get_nr_switch_leaves(const ExecutionPlan &ep) const {
  int switch_leaves = 0;
  auto leaves = ep.get_leaves();
  auto switch_types =
      std::vector<TargetType>{TargetType::BMv2, TargetType::Tofino};

  for (auto leaf : leaves) {
    if (!leaf.current_platform.first) {
      continue;
    }

    auto found_it = std::find(switch_types.begin(), switch_types.end(),
                              leaf.current_platform.second);

    if (found_it != switch_types.end()) {
      switch_leaves++;
    }
  }

  return switch_leaves;
}

Score::score_value_t
Score::next_op_same_obj_in_switch(const ExecutionPlan &ep) const {
  auto target = ep.get_current_platform();

  if (target != TargetType::BMv2 && target != TargetType::Tofino) {
    return 0;
  }

  auto next = ep.get_next_node();

  if (!next) {
    return 0;
  }

  auto prev = next->get_prev();

  if (!prev) {
    return 0;
  }

  if (next->get_type() != BDD::Node::CALL ||
      prev->get_type() != BDD::Node::CALL) {
    return 0;
  }

  auto next_call = static_cast<const BDD::Call *>(next.get());
  auto prev_call = static_cast<const BDD::Call *>(prev.get());

  auto next_obj = BDD::symbex::get_obj_from_call(next_call);
  auto prev_obj = BDD::symbex::get_obj_from_call(prev_call);

  if (!next_obj.first || !prev_obj.first) {
    return 0;
  }

  if (next_obj.second == prev_obj.second) {
    return 1;
  }

  return 0;
}

Score::score_value_t
Score::next_op_is_stateful_in_switch(const ExecutionPlan &ep) const {
  auto target = ep.get_current_platform();

  if (target != TargetType::BMv2 && target != TargetType::Tofino) {
    return 0;
  }

  auto next = ep.get_next_node();

  if (!next) {
    return 0;
  }

  if (next->get_type() != BDD::Node::CALL) {
    return 0;
  }

  auto next_call = static_cast<const BDD::Call *>(next.get());
  auto call = next_call->get_call();

  auto stateful_ops = std::vector<std::string>{
      BDD::symbex::FN_MAP_GET,
      BDD::symbex::FN_MAP_PUT,
      BDD::symbex::FN_VECTOR_BORROW,
      BDD::symbex::FN_VECTOR_RETURN,
      BDD::symbex::FN_DCHAIN_ALLOCATE_NEW_INDEX,
      BDD::symbex::FN_DCHAIN_REJUVENATE,
      BDD::symbex::DCHAIN_IS_INDEX_ALLOCATED,
  };

  auto found_it =
      std::find(stateful_ops.begin(), stateful_ops.end(), call.function_name);

  if (found_it != stateful_ops.end()) {
    return 1;
  }

  return 0;
}

Score::score_value_t
Score::get_percentage_of_processed_bdd(const ExecutionPlan &ep) const {
  return 100 * ep.get_bdd_processing_progress();
}

Score::score_value_t
Score::get_cost_bivariate_pbs(const ExecutionPlan &ep) const {
  auto nodes = get_nodes_with_type(ep, {Module::ModuleType::tfhe_BivariatePBS});
  // TODO Add bivariatePBS
  return nodes.size();
}


Score::score_value_t
Score::get_cost_univariate_pbs(const ExecutionPlan &ep) const {
  auto nodes = get_nodes_with_type(ep, {Module::ModuleType::tfhe_UnivariatePBS});
  // Multiplied by 2 since one bivariate PBS does what two univariate PBS do but for the same price
  return nodes.size() * 2;
}

} // namespace synapse
