#pragma once

#include "../execution_plan/execution_plan.h"
#include "../execution_plan/execution_plan_node.h"
#include "../execution_plan/modules/modules.h"
#include "../log.h"

#include <iostream>
#include <map>
#include <vector>

namespace synapse {

class Score {
public:
  enum Category {
    NumberOfReorderedNodes,
    NumberOfSwitchNodes,
    NumberOfSwitchLeaves,
    NumberOfNodes,
    NumberOfControllerNodes,
    NumberOfCounters,
    NumberOfSimpleTables,
    NumberOfIntAllocatorOps,
    Depth,
    ConsecutiveObjectOperationsInSwitch,
    HasNextStatefulOperationInSwitch,
    ProcessedBDDPercentage,
    CostOfPBSs,
    CostOfAidedPBSs,
  };

  enum Objective { MIN, MAX };
  typedef int score_value_t;

private:
  typedef score_value_t (Score::*ComputerPtr)(const ExecutionPlan &ep) const;

  // Responsible for calculating the score value for a given category.
  std::map<Category, ComputerPtr> computers;

  // The order of the elements in this vector matters.
  // It defines a lexicographic order.
  std::vector<std::pair<Category, Objective>> categories;

  // The actual score values.
  std::vector<score_value_t> values;

public:
  Score(const Score &score)
      : computers(score.computers), categories(score.categories),
        values(score.values) {}

  Score(const ExecutionPlan &ep,
        const std::vector<std::pair<Category, Objective>>
            &categories_objectives) {
    computers = {
        {NumberOfReorderedNodes, &Score::get_nr_reordered_nodes},
        {NumberOfNodes, &Score::get_nr_nodes},
        {NumberOfCounters, &Score::get_nr_counters},
        {NumberOfSimpleTables, &Score::get_nr_simple_tables},
        {NumberOfSwitchNodes, &Score::get_nr_switch_nodes},
        {NumberOfSwitchLeaves, &Score::get_nr_switch_leaves},
        {NumberOfControllerNodes, &Score::get_nr_controller_nodes},
        {Depth, &Score::get_depth},
        {ConsecutiveObjectOperationsInSwitch,
         &Score::next_op_same_obj_in_switch},
        {HasNextStatefulOperationInSwitch,
         &Score::next_op_is_stateful_in_switch},
        {NumberOfIntAllocatorOps, &Score::get_nr_int_allocator_ops},
        {ProcessedBDDPercentage, &Score::get_percentage_of_processed_bdd},
        {CostOfPBSs, &Score::get_cost_pbs},
        {CostOfAidedPBSs, &Score::get_cost_aided_pbs}
    };

    for (const auto &category_objective : categories_objectives) {
      auto category = category_objective.first;
      auto objective = category_objective.second;

      add(category, objective);

      auto value = compute(ep, category, objective);
      values.push_back(value);
    }
  }

  const std::vector<score_value_t> &get() const { return values; }

  inline bool operator<(const Score &other) {
    assert(values.size() == other.values.size());

    for (auto i = 0u; i < values.size(); i++) {
      auto this_score = values[i];
      auto other_score = other.values[i];

      if (this_score > other_score) {
        return false;
      }

      if (this_score < other_score) {
        return true;
      }
    }

    return false;
  }

  inline bool operator==(const Score &other) {
    assert(values.size() == other.values.size());

    for (auto i = 0u; i < values.size(); i++) {
      auto this_score = values[i];
      auto other_score = other.values[i];

      if (this_score != other_score) {
        return false;
      }
    }

    return true;
  }

  inline bool operator>(const Score &other) {
    return !((*this) < other) && !((*this) == other);
  }

  inline bool operator<=(const Score &other) { return !((*this) > other); }
  inline bool operator>=(const Score &other) { return !((*this) < other); }
  inline bool operator!=(const Score &other) { return !((*this) == other); }

  friend std::ostream &operator<<(std::ostream &os, const Score &dt);

private:
  score_value_t compute(const ExecutionPlan &ep, Category category,
                        Objective objective) const {
    auto found_it = computers.find(category);

    if (found_it == computers.end()) {
      Log::err() << "\nScore error: " << category
                 << " not found in lookup table.\n";
      exit(1);
    }

    auto computer = found_it->second;
    auto value = (this->*computer)(ep);

    if (objective == Objective::MIN) {
      value *= -1;
    }

    return value;
  }

  void add(Category category, Objective objective) {
    auto found_it =
        std::find_if(categories.begin(), categories.end(),
                     [&](const std::pair<Category, Objective> &saved) {
                       return saved.first == category;
                     });

    assert(found_it == categories.end() && "Category already inserted");

    categories.emplace_back(category, objective);
  }

  std::vector<ExecutionPlanNode_ptr>
  get_nodes_with_type(const ExecutionPlan &ep,
                      const std::vector<Module::ModuleType> &types) const;

  score_value_t get_nr_nodes(const ExecutionPlan &ep) const;
  score_value_t get_nr_counters(const ExecutionPlan &ep) const;
  score_value_t get_nr_int_allocator_ops(const ExecutionPlan &ep) const;
  score_value_t get_nr_simple_tables(const ExecutionPlan &ep) const;
  score_value_t get_depth(const ExecutionPlan &ep) const;
  score_value_t get_nr_switch_nodes(const ExecutionPlan &ep) const;
  score_value_t get_nr_controller_nodes(const ExecutionPlan &ep) const;
  score_value_t get_nr_reordered_nodes(const ExecutionPlan &ep) const;
  score_value_t get_nr_switch_leaves(const ExecutionPlan &ep) const;
  score_value_t next_op_same_obj_in_switch(const ExecutionPlan &ep) const;
  score_value_t next_op_is_stateful_in_switch(const ExecutionPlan &ep) const;
  score_value_t get_percentage_of_processed_bdd(const ExecutionPlan &ep) const;

  score_value_t get_cost_pbs(const ExecutionPlan &ep) const;
  score_value_t get_cost_aided_pbs(const ExecutionPlan &ep) const;
};

inline std::ostream &operator<<(std::ostream &os, const Score &score) {
  os << "<";

  bool first = true;
  for (auto i = 0u; i < score.values.size(); i++) {
    auto value = score.values[i];

    if (!first) {
      os << ",";
    }

    os << value;

    first &= false;
  }

  os << ">";
  return os;
}
} // namespace synapse