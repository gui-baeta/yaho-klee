#include "tofino_module.h"

#include "ignore.h"

namespace synapse {
namespace targets {
namespace tofino {

processing_result_t TofinoModule::postpone(const ExecutionPlan &ep,
                                           BDD::Node_ptr node,
                                           Module_ptr new_module) const {
  auto tmb = ep.get_memory_bank<TofinoMemoryBank>(Tofino);
  tmb->postpone(node->get_id(), new_module);
  return ignore(ep, node);
}

ExecutionPlan TofinoModule::apply_postponed(ExecutionPlan ep,
                                            BDD::Node_ptr current_node,
                                            BDD::Node_ptr next_node) const {
  assert(current_node);
  processing_result_t result;

  auto tmb = ep.get_memory_bank<TofinoMemoryBank>(Tofino);
  auto postponed = tmb->get_postponed();

  for (auto p : postponed) {
    auto reachable = current_node->is_reachable_by_node(p.first);

    if (!reachable) {
      continue;
    }

    ep = ep.add_leaf(p.second, current_node, false, false);
    ep.replace_active_leaf_node(current_node, false);
  }

  if (!next_node) {
    ep.force_termination();
  }

  return ep;
}

processing_result_t TofinoModule::ignore(const ExecutionPlan &ep,
                                         BDD::Node_ptr node) const {
  processing_result_t result;

  auto new_module = std::make_shared<Ignore>(node);
  auto new_ep = ep.ignore_leaf(node->get_next(), TargetType::Tofino);

  result.module = new_module;
  result.next_eps.push_back(new_ep);

  return result;
}

} // namespace tofino
} // namespace targets
} // namespace synapse