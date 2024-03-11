#pragma once


#include "call-paths-to-bdd.h"
#include "meta.h"
#include "target.h"

#include <klee/Constraints.h>

#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <vector>

namespace synapse {

class Module;
typedef std::shared_ptr<Module> Module_ptr;

class MemoryBank;
typedef std::shared_ptr<MemoryBank> MemoryBank_ptr;

class ExecutionPlanNode;
typedef std::shared_ptr<ExecutionPlanNode> ExecutionPlanNode_ptr;

class ExecutionPlanVisitor;

typedef uint64_t ep_id_t;

class ExecutionPlan {
  friend class ExecutionPlanNode;

public:
  struct leaf_t {
    ExecutionPlanNode_ptr leaf;
    BDD::Node_ptr next;
    std::pair<bool, TargetType> current_platform;

    leaf_t(BDD::Node_ptr _next);
    leaf_t(Module_ptr _module, BDD::Node_ptr _next);
    leaf_t(const leaf_t &_leaf);

    leaf_t &operator=(const leaf_t &) = default;
  };

private:
  ep_id_t id;

  ExecutionPlanNode_ptr root;
  /// "leaves" stores the current node(s) being processed
  /// It's a staged EP node
  std::vector<leaf_t> leaves;
  BDD::BDD bdd;

  std::unordered_set<TargetType> targets;
  TargetType initial_target;

  MemoryBank_ptr shared_memory_bank;
  std::unordered_map<TargetType, TargetMemoryBank_ptr> memory_banks;

  ep_meta_t meta;

  /// The Next value to be taken care off, respective to a BDD Node.
  /// If it's -1, it means that it's not set
  /// If its equal to the total number of values, it means that all values have
  /// been taken care off
  std::unordered_map<BDD::node_id_t, int> next_value;

public:
  ExecutionPlan(const BDD::BDD &_bdd);
  ExecutionPlan(const ExecutionPlan &ep);
  ExecutionPlan(const ExecutionPlan &ep, ExecutionPlanNode_ptr _root);

  ExecutionPlan &operator=(const ExecutionPlan &) = default;

  const ep_meta_t &get_meta() const;
  BDD::Node_ptr get_bdd_root(BDD::Node_ptr node) const;

  ep_id_t get_id() const;
  const std::vector<leaf_t> &get_leaves() const;
  const BDD::BDD &get_bdd() const;
  BDD::BDD &get_bdd();

  std::vector<ExecutionPlanNode_ptr> get_prev_nodes() const;
  std::vector<ExecutionPlanNode_ptr> get_prev_nodes_of_current_target() const;

  int get_next_value_of_node(BDD::node_id_t id);

  void mark_value_as_done_for_node(BDD::node_id_t id);

  bool all_values_marked_for_node(BDD::node_id_t id, int number_of_values);

  std::vector<BDD::Node_ptr> get_incoming_bdd_nodes() const;

  void inc_reordered_nodes();
  const ExecutionPlanNode_ptr &get_root() const;

  void add_target(TargetType type, TargetMemoryBank_ptr mb);
  bool has_target(TargetType type) const;

  MemoryBank_ptr get_memory_bank() const;

  template <class MB> MB *get_memory_bank(TargetType type) const {
    static_assert(std::is_base_of<TargetMemoryBank, MB>::value,
                  "MB not derived from TargetMemoryBank");
    assert(memory_banks.find(type) != memory_banks.end());
    return static_cast<MB *>(memory_banks.at(type).get());
  }

  BDD::Node_ptr get_next_node() const;

  /// Returns the node selected for processing.
  ExecutionPlanNode_ptr get_active_leaf() const;
  ExecutionPlanNode_ptr get_last_developed_node() const;

  TargetType get_current_platform() const;

  ExecutionPlan replace_leaf(Module_ptr new_module, const BDD::Node_ptr &next,
                             bool process_bdd_node = true) const;

  ExecutionPlan ignore_leaf(const BDD::Node_ptr &next, TargetType next_target,
                            bool process_bdd_node = true) const;

  ExecutionPlan add_leaf(Module_ptr new_module, const BDD::Node_ptr &next,
                           bool is_terminal = false,
                           bool process_bdd_node = true) const;

  // Order matters!
  // The active leaf will correspond to the first branch in the branches
  ExecutionPlan add_leaves(std::vector<leaf_t> _leaves,
                           bool is_terminal = false,
                           bool process_bdd_node = true) const;

  void replace_active_leaf_node(BDD::Node_ptr next,
                                bool process_bdd_node = true);

  void force_termination();

  float get_bdd_processing_progress() const;
  void remove_from_processed_bdd_nodes(BDD::node_id_t id);
  void add_processed_bdd_node(BDD::node_id_t id, int number_of_values);
  void add_processed_bdd_node(BDD::node_id_t id);
  void replace_roots(BDD::node_id_t _old, BDD::node_id_t _new);

  void visit(ExecutionPlanVisitor &visitor) const;

  ExecutionPlan clone(BDD::BDD new_bdd) const;
  ExecutionPlan clone(bool deep = false) const;

private:
  void update_roots(const std::vector<leaf_t> &new_leaves);
  void update_leaves(const std::vector<leaf_t> &_leaves, bool is_terminal);
  void update_processed_nodes();

  ExecutionPlanNode_ptr clone_nodes(ExecutionPlan &ep,
                                    const ExecutionPlanNode *node) const;

  static ep_id_t counter;
};

bool operator==(const ExecutionPlan &lhs, const ExecutionPlan &rhs);


class value_conditions_t {
public:
    ExecutionPlanNode_ptr condition = nullptr;

    // These represent possible subsequent conditions for the value
    std::shared_ptr<value_conditions_t> then_branch = nullptr;
    std::shared_ptr<value_conditions_t> else_branch = nullptr;

    // Modification of value given by the packet return chunk - end nodes of
    // some arbitrary branching
    klee::ref<klee::Expr> modification = nullptr;

    // For the vector constructor
    value_conditions_t() {}

    value_conditions_t(ExecutionPlanNode_ptr _condition) {
        this->condition = _condition;
    }

    void set_condition(ExecutionPlanNode_ptr _condition) {
        this->condition = _condition;
    }

    bool has_changes() {
        return this->modification != 0;
    }

    std::shared_ptr<value_conditions_t> get_then_branch() {
        return this->then_branch;
    }

    void set_then_branch(value_conditions_t value) {
        this->then_branch = std::make_shared<value_conditions_t>(value);
    }

    std::shared_ptr<value_conditions_t> get_else_branch() {
        return this->else_branch;
    }

    void set_else_branch(value_conditions_t value) {
        this->else_branch = std::make_shared<value_conditions_t>(value);
    }

    ~value_conditions_t() {
    }
};

} // namespace synapse