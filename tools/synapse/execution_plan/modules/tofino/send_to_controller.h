#pragma once

#include "int_allocator_rejuvenate.h"
#include "tofino_module.h"

namespace synapse {
namespace targets {
namespace tofino {

typedef uint16_t cpu_code_path_t;

class SendToController : public TofinoModule {
private:
  cpu_code_path_t cpu_code_path;
  BDD::symbols_t dataplane_state;

public:
  SendToController()
      : TofinoModule(ModuleType::Tofino_SendToController, "SendToController") {
    next_target = TargetType::x86_Tofino;
  }

  SendToController(BDD::Node_ptr node, cpu_code_path_t _cpu_code_path,
                   const BDD::symbols_t &_dataplane_state)
      : TofinoModule(ModuleType::Tofino_SendToController, "SendToController",
                     node),
        cpu_code_path(_cpu_code_path), dataplane_state(_dataplane_state) {
    next_target = TargetType::x86_Tofino;
  }

private:
  bool get_dchain_rejuvenate_obj(BDD::Node_ptr node, addr_t &obj) const {
    if (node->get_type() != BDD::Node::NodeType::CALL) {
      return false;
    }

    auto call_node = BDD_CAST_CALL(node);
    auto call = call_node->get_call();

    if (call.function_name != BDD::symbex::FN_DCHAIN_REJUVENATE) {
      return false;
    }

    assert(!call.args[BDD::symbex::FN_DCHAIN_ARG_CHAIN].expr.isNull());
    auto _dchain = call.args[BDD::symbex::FN_DCHAIN_ARG_CHAIN].expr;
    obj = kutil::expr_addr_to_obj_addr(_dchain);

    return true;
  }

  bool control_plane_rejuvenation_with_integer_allocator(
      const ExecutionPlan &ep) const {
    auto tmb = ep.get_memory_bank<TofinoMemoryBank>(Tofino);
    auto impls =
        tmb->get_implementations(DataStructure::Type::INTEGER_ALLOCATOR);

    if (impls.size() == 0) {
      return false;
    }

    auto incoming = ep.get_incoming_bdd_nodes();

    for (auto int_allocator : impls) {
      auto objs = int_allocator->get_objs();
      assert(objs.size() == 1);
      auto obj = *objs.begin();

      for (auto node : incoming) {
        addr_t dchain;

        if (!get_dchain_rejuvenate_obj(node, dchain)) {
          continue;
        }

        if (obj == dchain) {
          return true;
        }
      }
    }

    return false;
  }

  void replace_next(BDD::Node_ptr prev, BDD::Node_ptr old_next,
                    BDD::Node_ptr new_next) const {
    assert(prev);
    assert(old_next);
    assert(new_next);

    if (prev->get_type() == BDD::Node::NodeType::BRANCH) {
      auto branch_node = static_cast<BDD::Branch *>(prev.get());

      if (branch_node->get_on_true()->get_id() == old_next->get_id()) {
        branch_node->replace_on_true(new_next);
      } else {
        branch_node->replace_on_false(new_next);
      }

      new_next->replace_prev(prev);
      return;
    }

    prev->replace_next(new_next);
    new_next->replace_prev(prev);
  }

  BDD::Node_ptr clone_packet_parsing(ExecutionPlan &ep,
                                     BDD::Node_ptr current) const {
    assert(current);

    if (!current->get_prev()) {
      return current;
    }

    auto node = current;
    auto root = current;
    auto next = current->get_next();
    auto prev = current->get_prev();

    auto &bdd = ep.get_bdd();

    while (node->get_prev()) {
      node = node->get_prev();

      if (node->get_type() == BDD::Node::NodeType::CALL) {
        auto call_node = BDD::cast_node<BDD::Call>(node);
        auto call = call_node->get_call();

        if (call.function_name != BDD::symbex::FN_BORROW_CHUNK &&
            call.function_name != BDD::symbex::FN_RETURN_CHUNK &&
            call.function_name != BDD::symbex::FN_CURRENT_TIME) {
          continue;
        }

        auto clone = node->clone();

        clone->disconnect();
        clone->add_next(root);

        root->replace_prev(clone);

        auto id = bdd.get_id();
        clone->update_id(id);
        bdd.set_id(id + 1);

        root = clone;
      }
    }

    replace_next(prev, current, root);

    return root;
  }

  // Preemptive pruning
  bool should_prune(const ExecutionPlan &ep, BDD::Node_ptr node) {
    auto cases = {
        &SendToController::control_plane_rejuvenation_with_integer_allocator,
    };

    for (auto &c : cases) {
      auto should_prune = (this->*c)(ep);

      if (should_prune) {
        return true;
      }
    }

    return false;
  }

  BDD::symbols_t get_dataplane_state(const ExecutionPlan &ep,
                                     BDD::Node_ptr node) const {
    auto symbols_to_ignore = std::vector<std::string>{
        BDD::symbex::TIME,
        BDD::symbex::EXPIRE_MAP_FREED_FLOWS,
        BDD::symbex::RECEIVED_PACKET,
        BDD::symbex::BUFFER_LENGTH,
        BDD::symbex::PACKET_LENGTH,
        BDD::symbex::PORT,
        BDD::symbex::CHUNK,
    };

    auto should_ignore = [&](const BDD::symbol_t &symbol) {
      for (auto s : symbols_to_ignore)
        if (symbol.label_base == s)
          return true;
      return false;
    };

    auto prev = node->get_prev();

    if (!prev) {
      return BDD::symbols_t();
    }

    auto symbols = prev->get_generated_symbols();
    auto filtered = BDD::symbols_t();

    for (const auto &symbol : symbols) {
      if (should_ignore(symbol)) {
        continue;
      }

      filtered.insert(symbol);
    }

    return filtered;
  }

  void remember_dataplane_state(const ExecutionPlan &ep,
                                const BDD::symbols_t &symbols) const {
    auto tmb = ep.get_memory_bank<TofinoMemoryBank>(Tofino);
    tmb->add_dataplane_state(symbols);
  }

  processing_result_t process(const ExecutionPlan &ep,
                              BDD::Node_ptr node) override {
    processing_result_t result;

    if (!ep.has_target(TargetType::x86_Tofino)) {
      return result;
    }

    if (should_prune(ep, node)) {
      return result;
    }

    auto _dataplane_state = get_dataplane_state(ep, node);
    remember_dataplane_state(ep, _dataplane_state);

    auto ep_cloned = ep.clone(true);
    auto &bdd = ep_cloned.get_bdd();
    auto node_cloned = bdd.get_node_by_id(node->get_id());

    auto next_node = clone_packet_parsing(ep_cloned, node_cloned);
    auto _code_path = node->get_id();

    auto send_to_controller = std::make_shared<SendToController>(
        node_cloned, _code_path, _dataplane_state);

    auto new_ep =
        ep_cloned.add_leaf(send_to_controller, next_node, false, false);
    new_ep.replace_active_leaf_node(next_node, false);

    auto with_postponed = apply_postponed(new_ep, next_node, next_node);

    result.module = send_to_controller;
    result.next_eps.push_back(with_postponed);

    return result;
  }

public:
  virtual void visit(ExecutionPlanVisitor &visitor,
                     const ExecutionPlanNode *ep_node) const override {
    visitor.visit(ep_node, this);
  }

  virtual Module_ptr clone() const override {
    auto cloned = new SendToController(node, cpu_code_path, dataplane_state);
    return std::shared_ptr<Module>(cloned);
  }

  virtual bool equals(const Module *other) const override {
    if (other->get_type() != type) {
      return false;
    }

    auto other_cast = static_cast<const SendToController *>(other);

    if (cpu_code_path != other_cast->cpu_code_path) {
      return false;
    }

    return true;
  }

  cpu_code_path_t get_cpu_code_path() const { return cpu_code_path; }
  const BDD::symbols_t &get_dataplane_state() const { return dataplane_state; }
};
} // namespace tofino
} // namespace targets
} // namespace synapse
