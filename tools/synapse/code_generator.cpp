#include "code_generator.h"
#include "execution_plan/execution_plan.h"
#include "execution_plan/execution_plan_node.h"
#include "execution_plan/modules/modules.h"
#include "execution_plan/visitors/graphviz/graphviz.h"

namespace synapse {
using targets::tofino::TofinoMemoryBank;
using targets::x86_tofino::x86TofinoMemoryBank;

bool all_x86_no_controller(const ExecutionPlan &execution_plan) {
  auto nodes = std::vector<ExecutionPlanNode_ptr>{execution_plan.get_root()};

  while (nodes.size()) {
    auto node = nodes[0];
    nodes.erase(nodes.begin());

    assert(node);
    auto module = node->get_module();

    if (module->get_target() != TargetType::x86_BMv2) {
      return false;
    }

    auto next = node->get_next();
    nodes.insert(nodes.end(), next.begin(), next.end());
  }

  return true;
}

bool only_has_modules_from_target(const ExecutionPlan &execution_plan,
                                  TargetType type) {
  auto nodes = std::vector<ExecutionPlanNode_ptr>{execution_plan.get_root()};

  while (nodes.size()) {
    auto node = nodes[0];
    nodes.erase(nodes.begin());

    assert(node);
    auto module = node->get_module();

    if (module->get_target() != type) {
      return false;
    }

    auto next = node->get_next();
    nodes.insert(nodes.end(), next.begin(), next.end());
  }

  return true;
}

struct annotated_node_t {
  ExecutionPlanNode_ptr node;
  bool save;
  BDD::node_id_t path_id;

  annotated_node_t(ExecutionPlanNode_ptr _node)
      : node(_node), save(false), path_id(0) {}

  annotated_node_t(ExecutionPlanNode_ptr _node, bool _save,
                   BDD::node_id_t _path_id)
      : node(_node), save(_save), path_id(_path_id) {}

  annotated_node_t clone() const {
    auto cloned_node = ExecutionPlanNode::build(node.get());

    // the constructor increments the ID, let's fix that
    cloned_node->set_id(node->get_id());

    return annotated_node_t(cloned_node, save, path_id);
  }

  std::vector<annotated_node_t> next() {
    std::vector<annotated_node_t> nodes;

    auto next = node->get_next();

    if (next.size() == 0) {
      node = nullptr;
    }

    bool first = true;
    for (auto next_node : next) {
      if (first) {
        node = next_node;
        first = false;
        continue;
      }

      nodes.emplace_back(next_node, save, path_id);
    }

    return nodes;
  }
};

ExecutionPlan
CodeGenerator::x86_bmv2_extractor(const ExecutionPlan &execution_plan) const {
  if (only_has_modules_from_target(execution_plan, TargetType::x86_BMv2)) {
    return execution_plan;
  }

  assert(execution_plan.get_root());

  auto roots = std::vector<annotated_node_t>();
  auto leaves = std::vector<annotated_node_t>();
  auto branches = std::vector<annotated_node_t>{execution_plan.get_root()};

  while (branches.size()) {
    auto &annotated_node = branches[0];

    if (!annotated_node.node) {
      branches.erase(branches.begin());
      continue;
    }

    auto module = annotated_node.node->get_module();
    assert(module);

    if (annotated_node.save) {
      auto leaf_it = std::find_if(
          leaves.begin(), leaves.end(),
          [&](const annotated_node_t &annotated_leaf) {
            auto leaf_node = annotated_leaf.node;
            auto current_node = annotated_node.node;

            return leaf_node->get_id() == current_node->get_prev()->get_id() &&
                   annotated_leaf.path_id == annotated_node.path_id;
          });

      assert(leaf_it != leaves.end());

      auto clone = annotated_node.clone();
      auto leaf_next = leaf_it->node->get_next();
      auto current_next = annotated_node.node->get_next();

      leaf_next.push_back(clone.node);

      leaf_it->node->set_next(leaf_next);
      clone.node->set_prev(leaf_it->node);

      if (current_next.size() == 0) {
        leaves.erase(leaf_it);
      } else {
        for (auto i = 0u; i < current_next.size(); i++) {
          if (i == 0) {
            *leaf_it = clone;
            continue;
          }

          leaves.push_back(clone);
        }
      }
    }

    if (module->get_type() == Module::ModuleType::BMv2_SendToController) {
      auto send_to_controller =
          static_cast<targets::bmv2::SendToController *>(module.get());

      auto path_id = send_to_controller->get_metadata_code_path();

      annotated_node.save = true;
      annotated_node.path_id = path_id;

      auto next = annotated_node.next();
      assert(next.size() == 0);

      auto clone = annotated_node.clone();

      roots.push_back(clone);
      leaves.push_back(clone);
    }

    auto next_branches = annotated_node.next();
    branches.insert(branches.end(), next_branches.begin(), next_branches.end());
  }

  if (roots.size() == 0) {
    auto extracted = ExecutionPlan(execution_plan, ExecutionPlanNode_ptr());
    return extracted;
  }

  auto metadata = kutil::solver_toolbox.create_new_symbol("metadata", 32);
  auto packet_get_metadata =
      std::make_shared<targets::x86_bmv2::PacketGetMetadata>(nullptr, metadata);

  auto new_root = ExecutionPlanNode::build(packet_get_metadata);
  auto new_leaf = new_root;

  for (auto i = 0u; i < roots.size(); i++) {
    auto root = roots[i];

    assert(root.node);
    assert(new_leaf);

    auto path_id = kutil::solver_toolbox.exprBuilder->Constant(
        root.path_id, metadata->getWidth());
    auto meta_eq_path_id =
        kutil::solver_toolbox.exprBuilder->Eq(metadata, path_id);

    auto if_meta_eq_path_id =
        std::make_shared<targets::x86_bmv2::If>(nullptr, meta_eq_path_id);
    auto if_ep_node = ExecutionPlanNode::build(if_meta_eq_path_id);

    auto then_module = std::make_shared<targets::x86_bmv2::Then>(nullptr);
    auto then_ep_node = ExecutionPlanNode::build(then_module);

    auto else_module = std::make_shared<targets::x86_bmv2::Else>(nullptr);
    auto else_ep_node = ExecutionPlanNode::build(else_module);

    Branches then_else_ep_nodes{then_ep_node, else_ep_node};

    if_ep_node->set_next(then_else_ep_nodes);

    then_ep_node->set_prev(if_ep_node);
    else_ep_node->set_prev(if_ep_node);

    new_leaf->set_next(if_ep_node);
    if_ep_node->set_prev(new_leaf);

    then_ep_node->set_next(root.node);
    root.node->set_prev(then_ep_node);

    if (i == roots.size() - 1) {
      auto drop_module = std::make_shared<targets::x86_bmv2::Drop>(nullptr);
      auto drop_ep_node = ExecutionPlanNode::build(drop_module);

      else_ep_node->set_next(drop_ep_node);
      drop_ep_node->set_prev(else_ep_node);

      new_leaf = nullptr;
    } else {
      new_leaf = else_ep_node;
    }
  }

  auto extracted = ExecutionPlan(execution_plan, new_root);
  // Graphviz::visualize(extracted);

  return extracted;
}

ExecutionPlan
CodeGenerator::bmv2_extractor(const ExecutionPlan &execution_plan) const {
  auto extracted = execution_plan.clone(true);
  auto nodes = std::vector<ExecutionPlanNode_ptr>{extracted.get_root()};

  while (nodes.size()) {
    auto node = nodes[0];
    assert(node);

    nodes.erase(nodes.begin());

    auto module = node->get_module();
    assert(module);
    assert(module->get_target() == TargetType::BMv2);

    if (module->get_type() == Module::ModuleType::BMv2_SendToController) {
      auto no_next = Branches();
      node->set_next(no_next);
    }

    auto next = node->get_next();
    nodes.insert(nodes.end(), next.begin(), next.end());
  }

  // Graphviz::visualize(extracted);

  return extracted;
}

struct x86_tofino_root_info_t {
  targets::tofino::cpu_code_path_t cpu_code_path;
  BDD::symbols_t dataplane_state;
};

typedef std::vector<std::pair<ExecutionPlanNode_ptr, x86_tofino_root_info_t>>
    x86_tofino_roots_t;

x86_tofino_roots_t get_roots(const ExecutionPlan &execution_plan) {
  assert(execution_plan.get_root());

  auto roots = x86_tofino_roots_t();
  auto nodes = std::vector<ExecutionPlanNode_ptr>{execution_plan.get_root()};

  while (nodes.size()) {
    auto node = nodes[0];
    assert(node);
    nodes.erase(nodes.begin());

    auto module = node->get_module();
    assert(module);

    auto next = node->get_next();

    if (module->get_type() == Module::ModuleType::Tofino_SendToController) {
      auto send_to_controller =
          static_cast<targets::tofino::SendToController *>(module.get());

      auto cpu_code_path = send_to_controller->get_cpu_code_path();
      auto dataplane_state = send_to_controller->get_dataplane_state();
      auto info = x86_tofino_root_info_t{cpu_code_path, dataplane_state};

      assert(next.size() == 1);
      auto cloned_node = next[0]->clone(true);

      roots.emplace_back(cloned_node, info);
      continue;
    }

    nodes.insert(nodes.end(), next.begin(), next.end());
  }

  return roots;
}

ExecutionPlan
CodeGenerator::x86_tofino_extractor(const ExecutionPlan &execution_plan) const {
  if (only_has_modules_from_target(execution_plan, TargetType::x86_Tofino)) {
    return execution_plan;
  }

  auto roots = get_roots(execution_plan);

  if (roots.size() == 0) {
    auto extracted = ExecutionPlan(execution_plan, ExecutionPlanNode_ptr());
    return extracted;
  }

  auto code_path = kutil::solver_toolbox.create_new_symbol(
      BDD::symbex::CPU_CODE_PATH, sizeof(targets::tofino::cpu_code_path_t) * 8);

  ExecutionPlanNode_ptr new_root;
  ExecutionPlanNode_ptr new_leaf;

  for (auto i = 0u; i < roots.size(); i++) {
    auto root = roots[i];

    auto path_id = kutil::solver_toolbox.exprBuilder->Constant(
        root.second.cpu_code_path, code_path->getWidth());

    auto path_eq_path_id =
        kutil::solver_toolbox.exprBuilder->Eq(code_path, path_id);

    auto if_path_eq_path_id =
        std::make_shared<targets::x86_tofino::If>(nullptr, path_eq_path_id);
    auto if_ep_node = ExecutionPlanNode::build(if_path_eq_path_id);

    auto then_module = std::make_shared<targets::x86_tofino::Then>(nullptr);
    auto then_ep_node = ExecutionPlanNode::build(then_module);

    auto else_module = std::make_shared<targets::x86_tofino::Else>(nullptr);
    auto else_ep_node = ExecutionPlanNode::build(else_module);

    auto then_else_ep_nodes = Branches{then_ep_node, else_ep_node};
    if_ep_node->set_next(then_else_ep_nodes);

    then_ep_node->set_prev(if_ep_node);
    else_ep_node->set_prev(if_ep_node);

    if (new_leaf) {
      new_leaf->set_next(if_ep_node);
      if_ep_node->set_prev(new_leaf);
    } else {
      new_leaf = else_ep_node;
      new_root = if_ep_node;
    }

    then_ep_node->set_next(root.first);
    root.first->set_prev(then_ep_node);

    if (i == roots.size() - 1) {
      auto drop_module = std::make_shared<targets::x86_tofino::Drop>(nullptr);
      auto drop_ep_node = ExecutionPlanNode::build(drop_module);

      else_ep_node->set_next(drop_ep_node);
      drop_ep_node->set_prev(else_ep_node);

      new_leaf = nullptr;
    } else {
      new_leaf = else_ep_node;
    }
  }

  auto tmb = execution_plan.get_memory_bank<TofinoMemoryBank>(Tofino);
  auto dp_state = tmb->get_dataplane_state();

  auto parse_cpu_module =
      std::make_shared<targets::x86_tofino::PacketParseCPU>(dp_state);
  auto parse_cpu_ep_node = ExecutionPlanNode::build(parse_cpu_module);

  parse_cpu_ep_node->set_next(new_root);
  new_root->set_prev(parse_cpu_ep_node);

  new_root = parse_cpu_ep_node;

  auto extracted = ExecutionPlan(execution_plan, new_root);
  // Graphviz::visualize(extracted);

  return extracted;
}

ExecutionPlan
CodeGenerator::tofino_extractor(const ExecutionPlan &execution_plan) const {
  auto extracted = execution_plan.clone(true);
  auto nodes = std::vector<ExecutionPlanNode_ptr>{extracted.get_root()};

  while (nodes.size()) {
    auto node = nodes[0];
    assert(node);

    nodes.erase(nodes.begin());

    auto module = node->get_module();
    assert(module);
    assert(module->get_target() == TargetType::Tofino);

    if (module->get_type() == Module::ModuleType::Tofino_SendToController) {
      auto no_next = Branches();
      node->set_next(no_next);
    }

    auto next = node->get_next();
    nodes.insert(nodes.end(), next.begin(), next.end());
  }

  // Graphviz::visualize(extracted);

  return extracted;
}

ExecutionPlan
CodeGenerator::x86_extractor(const ExecutionPlan &execution_plan) const {
  // No extraction at all, just asserting that this targets contains only x86
  // nodes.

  auto nodes = std::vector<ExecutionPlanNode_ptr>{execution_plan.get_root()};

  while (nodes.size()) {
    auto node = nodes[0];
    assert(node);

    nodes.erase(nodes.begin());

    auto module = node->get_module();
    assert(module);
    assert(module->get_target() == TargetType::x86);

    auto next = node->get_next();
    nodes.insert(nodes.end(), next.begin(), next.end());
  }

  return execution_plan;
}

} // namespace synapse