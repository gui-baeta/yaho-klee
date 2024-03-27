#pragma once

#include "execution_plan/execution_plan.h"
#include "execution_plan/visitors/graphviz/graphviz.h"
#include "heuristics/heuristic.h"
#include "log.h"
#include "search_space.h"

namespace synapse {

class SearchEngine {
private:
  std::vector<Target_ptr> targets;
  BDD::BDD bdd;

  // Maximum number of reordered nodes on the BDD
  // -1 => unlimited
  int max_reordered;

  SearchSpace search_space;

  // Internal use only
  struct report_t {
    int available_execution_plans;
    const ExecutionPlan &chosen;
    BDD::Node_ptr current;

    std::vector<std::string> target_name;
    std::vector<std::string> name;
    std::vector<unsigned> generated_contexts;
    std::vector<std::vector<unsigned>> generated_exec_plans_ids;

    report_t(int _available_execution_plans, const ExecutionPlan &_chosen,
             BDD::Node_ptr _current)
        : available_execution_plans(_available_execution_plans),
          chosen(_chosen), current(_current) {}
  };

public:
  SearchEngine(BDD::BDD _bdd, int _max_reordered)
      : bdd(_bdd), max_reordered(_max_reordered) {}

  SearchEngine(const SearchEngine &se)
      : SearchEngine(se.bdd, se.max_reordered) {
    targets = se.targets;
  }

  void add_target(TargetType target) {
    switch (target) {
    case TargetType::x86_BMv2:
      targets.push_back(targets::x86_bmv2::x86BMv2Target::build());
      break;
    case TargetType::x86_Tofino:
      targets.push_back(targets::x86_tofino::x86TofinoTarget::build());
      break;
    case TargetType::Tofino:
      targets.push_back(targets::tofino::TofinoTarget::build());
      break;
    case TargetType::BMv2:
      targets.push_back(targets::bmv2::BMv2Target::build());
      break;
    case TargetType::x86:
      targets.push_back(targets::x86::x86Target::build());
      break;
    }
  }

  template <class T> ExecutionPlan search(Heuristic<T> h, BDD::node_id_t peek) {
    auto first_execution_plan = ExecutionPlan(bdd);

    for (auto target : targets) {
      first_execution_plan.add_target(target->type, target->memory_bank);
    }

    search_space.init(h.get_cfg(), first_execution_plan);
    h.add(std::vector<ExecutionPlan>{first_execution_plan});

    while (!h.finished()) {
      auto available = h.size();
      auto next_ep = h.pop();
      auto next_node = next_ep.get_next_node();
      assert(next_node);

      search_space.set_winner(next_ep);

      report_t report(available, next_ep, next_node);

      for (auto target : targets) {
        for (auto module : target->modules) {
          auto result = module->process_node(next_ep, next_node, max_reordered);

          if (result.next_eps.size()) {
            report.target_name.push_back(module->get_target_name());
            report.name.push_back(module->get_name());
            report.generated_contexts.push_back(result.next_eps.size());
            report.generated_exec_plans_ids.emplace_back();

            for (const auto &ep : result.next_eps) {
              report.generated_exec_plans_ids.back().push_back(ep.get_id());
            }

            h.add(result.next_eps);
            search_space.add_leaves(next_ep, next_node, result.module,
                                    result.next_eps);
          }
        }
      }

      search_space.submit_leaves();

      log_search_iteration(report);

      if (next_node->get_id() == peek) {
        Graphviz::visualize(search_space);
      }
    }

    Log::log() << "Solutions:      " << h.get_all().size() << "\n";
    Log::log() << "Winner:         " << h.get_score(h.get()) << "\n";

    return h.get();
  }

  const SearchSpace &get_search_space() const { return search_space; }

private:
  void log_search_iteration(const report_t &report) {
    auto platform = report.chosen.get_current_platform();
    auto leaf = report.chosen.get_active_leaf();

    Log::dbg() << "\n";
    Log::dbg() << "=======================================================\n";

    Log::dbg() << "Available      " << report.available_execution_plans << "\n";
    Log::dbg() << "Execution Plan " << report.chosen.get_id() << "\n";
    Log::dbg() << "BDD progress   " << std::fixed << std::setprecision(2)
               << 100 * report.chosen.get_bdd_processing_progress() << " %\n";
    Log::dbg() << "Current target " << Module::target_to_string(platform)
               << "\n";
    if (leaf && leaf->get_module()) {
      Log::dbg() << "Leaf           " << leaf->get_module()->get_name() << "\n";
    }
    Log::dbg() << "Node           " << report.current->dump(true) << "\n";

    if (report.target_name.size()) {
      for (unsigned i = 0; i < report.target_name.size(); i++) {
        std::stringstream exec_plans_ids;
        exec_plans_ids << "[";
        for (auto j = 0u; j < report.generated_exec_plans_ids[i].size(); j++) {
          if (j != 0) {
            exec_plans_ids << ",";
          }
          exec_plans_ids << report.generated_exec_plans_ids[i][j];
        }
        exec_plans_ids << "]";

        Log::dbg() << "MATCH          " << report.target_name[i]
                   << "::" << report.name[i] << " -> " << exec_plans_ids.str()
                   << " (" << report.generated_contexts[i] << ") exec plans "
                   << "\n";
      }
    } else {
      Log::dbg() << "\n";
      Log::dbg() << "\n";
      Log::dbg() << "**DEAD END**: No module can handle this BDD node"
                    " in the current context.\n";
      Log::dbg() << "Deleting solution from search space.\n";
      Log::dbg() << "\n";
    }

    Log::dbg() << "=======================================================\n";
  }
};

} // namespace synapse
