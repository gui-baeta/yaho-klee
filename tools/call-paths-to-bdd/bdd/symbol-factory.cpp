#include "symbol-factory.h"

#include <algorithm>
#include <iostream>

namespace BDD {

std::vector<std::string> SymbolFactory::ignored_symbols{"VIGOR_DEVICE"};
std::vector<std::string> SymbolFactory::symbols_without_translation{
    "packet_chunks"};

bool SymbolFactory::should_ignore(std::string symbol) {
  auto found_it =
      std::find(ignored_symbols.begin(), ignored_symbols.end(), symbol);
  return found_it != ignored_symbols.end();
}

bool SymbolFactory::should_not_translate(std::string symbol) {
  auto found_it = std::find(symbols_without_translation.begin(),
                            symbols_without_translation.end(), symbol);
  return found_it != symbols_without_translation.end();
}

std::unordered_set<std::string> get_call_symbols(const Call *call_node) {
  std::unordered_set<std::string> symbols;

  assert(call_node);
  auto call = call_node->get_call();

  for (auto arg : call.args) {
    auto expr_symbols = kutil::get_symbols(arg.second.expr);
    auto in_symbols = kutil::get_symbols(arg.second.in);
    auto out_symbols = kutil::get_symbols(arg.second.out);

    symbols.insert(expr_symbols.begin(), expr_symbols.end());
    symbols.insert(in_symbols.begin(), in_symbols.end());
    symbols.insert(out_symbols.begin(), out_symbols.end());
  }

  for (auto ev : call.extra_vars) {
    auto in_symbols = kutil::get_symbols(ev.second.first);
    auto out_symbols = kutil::get_symbols(ev.second.first);

    symbols.insert(in_symbols.begin(), in_symbols.end());
    symbols.insert(out_symbols.begin(), out_symbols.end());
  }

  auto ret_symbols = kutil::get_symbols(call.ret);
  symbols.insert(ret_symbols.begin(), ret_symbols.end());

  for (auto constraint : call_node->get_node_constraints()) {
    auto constraint_symbols = kutil::get_symbols(constraint);
    symbols.insert(constraint_symbols.begin(), constraint_symbols.end());
  }

  return symbols;
}

std::unordered_set<std::string> get_branch_symbols(const Branch *branch_node) {
  std::unordered_set<std::string> symbols;

  assert(branch_node);
  auto condition = branch_node->get_condition();

  auto condition_symbols = kutil::get_symbols(condition);
  symbols.insert(condition_symbols.begin(), condition_symbols.end());

  for (auto constraint : branch_node->get_node_constraints()) {
    auto constraint_symbols = kutil::get_symbols(constraint);
    symbols.insert(constraint_symbols.begin(), constraint_symbols.end());
  }

  return symbols;
}

std::unordered_set<std::string> get_all_symbols(const Node *node) {
  assert(node);

  auto symbols = std::unordered_set<std::string>();
  auto all_nodes = std::vector<const Node *>{node};

  auto prev_node = node->get_prev();
  while (prev_node) {
    all_nodes.push_back(prev_node.get());
    prev_node = prev_node->get_prev();
  }

  auto nodes = std::vector<const Node *>{node};

  while (nodes.size()) {
    auto node = nodes[0];
    nodes.erase(nodes.begin());

    if (node->get_type() == Node::NodeType::CALL) {
      auto call_node = static_cast<const Call *>(node);
      auto call_symbols = get_call_symbols(call_node);
      symbols.insert(call_symbols.begin(), call_symbols.end());

      auto next = node->get_next().get();
      all_nodes.push_back(next);
      nodes.push_back(next);
    }

    else if (node->get_type() == Node::NodeType::BRANCH) {
      auto branch_node = static_cast<const Branch *>(node);
      auto branch_symbols = get_branch_symbols(branch_node);
      symbols.insert(branch_symbols.begin(), branch_symbols.end());

      nodes.push_back(branch_node->get_on_true().get());
      nodes.push_back(branch_node->get_on_false().get());

      auto on_true = branch_node->get_on_true().get();
      auto on_false = branch_node->get_on_false().get();

      all_nodes.push_back(on_true);
      all_nodes.push_back(on_false);

      nodes.push_back(on_true);
      nodes.push_back(on_false);
    }
  }

  for (auto node : all_nodes) {
    for (auto constraint : node->get_node_constraints()) {
      auto constraint_symbols = kutil::get_symbols(constraint);
      symbols.insert(constraint_symbols.begin(), constraint_symbols.end());
    }

    if (node->get_type() == Node::NodeType::CALL) {
      auto call_node = static_cast<const Call *>(node);
      auto call_symbols = get_call_symbols(call_node);
      symbols.insert(call_symbols.begin(), call_symbols.end());
    }

    else if (node->get_type() == Node::NodeType::BRANCH) {
      auto branch_node = static_cast<const Branch *>(node);
      auto branch_symbols = get_branch_symbols(branch_node);
      symbols.insert(branch_symbols.begin(), branch_symbols.end());
    }
  }

  return symbols;
}

std::vector<std::string>
filter_symbols_by_base(const std::unordered_set<std::string> &symbols,
                       const std::string &base) {
  auto base_finder = [&](const std::string &symbol) {
    if (symbol == base) {
      return true;
    }

    auto delim = symbol.find(base);

    if (delim == std::string::npos || delim > 0) {
      return false;
    }

    auto remainder = symbol.substr(base.size());

    if (remainder.find("_") == std::string::npos) {
      return false;
    }

    return true;
  };

  auto symbol_sorter = [&](const std::string &s1, const std::string &s2) {
    assert(s1 != s2);

    auto s1_suffix = s1.substr(base.size());
    auto s2_suffix = s2.substr(base.size());

    if (s1_suffix.size() == 0 || s2_suffix.size() == 0) {
      return s1_suffix.size() < s2_suffix.size();
    }

    assert(s1_suffix.size() > 1);
    assert(s2_suffix.size() > 1);

    assert(s1_suffix[0] == '_');
    assert(s2_suffix[0] == '_');

    auto s1_new_suffix_delim = s1_suffix.find("__");
    auto s2_new_suffix_delim = s2_suffix.find("__");

    auto s1_has_new_suffix = s1_new_suffix_delim != std::string::npos;
    auto s2_has_new_suffix = s2_new_suffix_delim != std::string::npos;

    if (s1_has_new_suffix && !s2_has_new_suffix) {
      return true;
    }

    if (!s1_has_new_suffix && s2_has_new_suffix) {
      return false;
    }

    s1_suffix = s1_suffix.substr(s1_has_new_suffix ? 2 : 1);
    s2_suffix = s2_suffix.substr(s2_has_new_suffix ? 2 : 1);

    auto s1_n = std::stoi(s1_suffix);
    auto s2_n = std::stoi(s2_suffix);

    return s1_n < s2_n;
  };

  std::vector<std::string> found;

  std::copy_if(symbols.begin(), symbols.end(), std::back_inserter(found),
               base_finder);
  std::sort(found.begin(), found.end(), symbol_sorter);

  return found;
}

std::string SymbolFactory::build_label(std::string base, const Node *node,
                                       bool save) {
  assert(node);

  if (!save) {
    return translate_label(base, node);
  }

  auto all_symbols = get_all_symbols(node);
  auto counter = count_labels(base);

  auto labels = filter_symbols_by_base(all_symbols, base);

  if (counter >= static_cast<int>(labels.size())) {
    // Apparently this symbol will not be used in future nodes
    return base;
  }

  auto label = labels[counter];

  stack.back().emplace_back(base, label);

  return label;
}

std::string SymbolFactory::build_label(klee::ref<klee::Expr> expr,
                                       std::string base, bool save) {
  kutil::RetrieveSymbols retriever;
  retriever.visit(expr);

  auto symbols = retriever.get_retrieved_strings();

  for (auto symbol : symbols) {
    auto delim = symbol.find(base);

    if (delim != std::string::npos) {
      if (save) {
        stack.back().emplace_back(base, symbol);
      }

      return symbol;
    }
  }

  std::cerr << "expr   " << kutil::expr_to_string(expr, true) << "\n";
  std::cerr << "symbol " << base << "\n";
  assert(false && "Symbol not found");

  std::cerr << "Symbol not found\n";
  exit(1);
}

symbols_t SymbolFactory::no_process(call_t call, const Node *node, bool save) {
  symbols_t symbols;
  return symbols;
}

symbols_t SymbolFactory::sketch_fetch(call_t call, const Node *node,
                                      bool save) {
  symbols_t symbols;

  assert(!call.ret.isNull());

  auto overflow = call.ret;
  auto overflow_label = build_label(overflow, "overflow", save);

  symbols.emplace(overflow_label, "overflow", overflow);

  return symbols;
}

symbols_t SymbolFactory::sketch_touch_buckets(call_t call, const Node *node,
                                              bool save) {
  symbols_t symbols;

  assert(!call.ret.isNull());

  auto success = call.ret;
  auto success_label = build_label(success, "success", save);

  symbols.emplace(success_label, "success", success);

  return symbols;
}

symbols_t SymbolFactory::cht_fill_cht(call_t call, const Node *node,
                                      bool save) {
  symbols_t symbols;

  assert(!call.ret.isNull());

  auto cht_fill_cht_successful = call.ret;
  auto cht_fill_cht_successful_label =
      build_label(cht_fill_cht_successful, "cht_fill_cht_successful", save);

  symbols.emplace(cht_fill_cht_successful_label, "cht_fill_cht_successful",
                  cht_fill_cht_successful);

  return symbols;
}

symbols_t SymbolFactory::LoadBalancedFlow_hash(call_t call, const Node *node,
                                               bool save) {
  symbols_t symbols;

  assert(!call.ret.isNull());

  auto hash = call.ret;
  auto hash_label = build_label(hash, "LoadBalancedFlow_hash", save);

  symbols.emplace(hash_label, "LoadBalancedFlow_hash", hash);

  return symbols;
}

symbols_t SymbolFactory::cht_find_preferred_available_backend(call_t call,
                                                              const Node *node,
                                                              bool save) {
  symbols_t symbols;

  assert(!call.ret.isNull());
  assert(!call.args["chosen_backend"].out.isNull());

  auto prefered_backend_found = call.ret;
  auto prefered_backend_found_label =
      build_label("prefered_backend_found", node, save);

  auto chosen_backend = call.args["chosen_backend"].out;
  auto chosen_backend_label =
      build_label(chosen_backend, "chosen_backend", save);

  symbols.emplace(prefered_backend_found_label, "prefered_backend_found",
                  prefered_backend_found);
  symbols.emplace(chosen_backend_label, "chosen_backend", chosen_backend);

  return symbols;
}

symbols_t SymbolFactory::map_get(call_t call, const Node *node, bool save) {
  symbols_t symbols;

  assert(has_arg(call, "value_out"));

  assert(!call.ret.isNull());
  assert(!call.args["value_out"].out.isNull());

  auto map_has_this_key = call.ret;
  auto map_has_this_key_label = build_label("map_has_this_key", node, save);

  symbols.emplace(map_has_this_key_label, "map_has_this_key", map_has_this_key);

  auto has_this_key = kutil::solver_toolbox.exprBuilder->Constant(
      1, map_has_this_key->getWidth());

  if (kutil::solver_toolbox.are_exprs_always_equal(map_has_this_key,
                                                   has_this_key)) {
    auto value_out = call.args["value_out"].out;
    auto value_out_label = build_label(value_out, "allocated_index", save);
    symbols.emplace(value_out_label, "allocated_index", value_out);
  }

  return symbols;
}

symbols_t SymbolFactory::dchain_is_index_allocated(call_t call,
                                                   const Node *node,
                                                   bool save) {
  symbols_t symbols;

  assert(!call.ret.isNull());
  auto is_index_allocated = call.ret;
  auto is_index_allocated_label =
      build_label("dchain_is_index_allocated", node, save);

  symbols.emplace(is_index_allocated_label, "dchain_is_index_allocated",
                  is_index_allocated);

  return symbols;
}

symbols_t SymbolFactory::dchain_allocate_new_index(call_t call,
                                                   const Node *node,
                                                   bool save) {
  symbols_t symbols;

  assert(has_arg(call, "index_out"));

  assert(!call.args["index_out"].out.isNull());
  assert(!call.ret.isNull());

  auto index_out = call.args["index_out"].out;
  auto index_out_label = build_label("out_of_space", node, save);

  auto success = call.ret;
  auto success_label = build_label(index_out, "new_index", save);

  symbols.emplace(index_out_label, "out_of_space", success);
  symbols.emplace(success_label, "new_index", index_out);

  return symbols;
}

symbols_t SymbolFactory::packet_borrow_next_secret(call_t call, const Node *node,
                                                  bool save) {
  symbols_t symbols;

  assert(has_arg(call, "chunk"));
  assert(has_extra_var(call, "the_chunk"));

  assert(!call.args["chunk"].out.isNull());
  assert(!call.extra_vars["the_chunk"].second.isNull());

  auto chunk = call.extra_vars["the_chunk"].second;
  auto chunk_addr = call.args["chunk"].out;

  symbols.emplace("packet_chunks", "packet_chunks", chunk, chunk_addr);

  return symbols;
}

symbols_t SymbolFactory::packet_borrow_next_chunk(call_t call, const Node *node,
                                                  bool save) {
  symbols_t symbols;

  assert(has_arg(call, "chunk"));
  assert(has_extra_var(call, "the_chunk"));

  assert(!call.args["chunk"].out.isNull());
  assert(!call.extra_vars["the_chunk"].second.isNull());

  auto chunk = call.extra_vars["the_chunk"].second;
  auto chunk_addr = call.args["chunk"].out;

  symbols.emplace("packet_chunks", "packet_chunks", chunk, chunk_addr);

  return symbols;
}

symbols_t SymbolFactory::expire_items_single_map(call_t call, const Node *node,
                                                 bool save) {
  symbols_t symbols;

  assert(!call.ret.isNull());
  auto number_of_freed_flows = call.ret;
  auto number_of_freed_flows_label =
      build_label(number_of_freed_flows, "number_of_freed_flows", save);

  symbols.emplace(number_of_freed_flows_label, "number_of_freed_flows",
                  number_of_freed_flows);

  return symbols;
}

symbols_t SymbolFactory::expire_items_single_map_iteratively(call_t call,
                                                             const Node *node,
                                                             bool save) {
  symbols_t symbols;

  assert(!call.ret.isNull());
  auto number_of_freed_flows = call.ret;
  auto number_of_freed_flows_label =
      build_label("number_of_freed_flows", node, save);

  symbols.emplace(number_of_freed_flows_label, "number_of_freed_flows",
                  number_of_freed_flows);

  return symbols;
}

symbols_t SymbolFactory::rte_ether_addr_hash(call_t call, const Node *node,
                                             bool save) {
  symbols_t symbols;

  assert(!call.ret.isNull());
  auto hash = call.ret;
  auto hash_label = build_label("rte_ether_addr_hash", node, save);
  symbols.emplace(hash_label, "rte_ether_addr_hash", hash);

  return symbols;
}

symbols_t SymbolFactory::vector_borrow(call_t call, const Node *node,
                                       bool save) {
  symbols_t symbols;

  assert(has_arg(call, "val_out"));
  assert(has_extra_var(call, "borrowed_cell"));

  assert(!call.args["val_out"].out.isNull());
  assert(!call.extra_vars["borrowed_cell"].second.isNull());

  auto value_out = call.args["val_out"].out;
  auto borrowed_cell = call.extra_vars["borrowed_cell"].second;
  auto borrowed_cell_label =
      build_label(borrowed_cell, "vector_data_reset", save);

  symbols.emplace(borrowed_cell_label, "vector_data_reset", borrowed_cell,
                  value_out);

  return symbols;
}

symbols_t SymbolFactory::sketch_allocate(call_t call, const Node *node,
                                         bool save) {
  symbols_t symbols;

  assert(!call.ret.isNull());
  auto sketch_allocation_succeeded = call.ret;
  auto sketch_allocation_succeeded_label =
      build_label("sketch_allocation_succeeded", node, save);

  symbols.emplace(sketch_allocation_succeeded_label,
                  "sketch_allocation_succeeded", sketch_allocation_succeeded);

  return symbols;
}

symbols_t SymbolFactory::map_allocate(call_t call, const Node *node,
                                      bool save) {
  symbols_t symbols;

  assert(!call.ret.isNull());
  auto map_allocation_succeeded = call.ret;
  auto map_allocation_succeeded_label =
      build_label("map_allocation_succeeded", node, save);

  symbols.emplace(map_allocation_succeeded_label, "map_allocation_succeeded",
                  map_allocation_succeeded);

  return symbols;
}

symbols_t SymbolFactory::vector_allocate(call_t call, const Node *node,
                                         bool save) {
  symbols_t symbols;

  assert(!call.ret.isNull());
  auto vector_alloc_success = call.ret;
  auto vector_alloc_success_label =
      build_label("vector_alloc_success", node, save);

  symbols.emplace(vector_alloc_success_label, "vector_alloc_success",
                  vector_alloc_success);

  return symbols;
}

symbols_t SymbolFactory::current_time(call_t call, const Node *node,
                                      bool save) {
  symbols_t symbols;

  assert(!call.ret.isNull());
  auto next_time = call.ret;
  auto next_time_label = build_label("next_time", node, save);

  symbols.emplace(next_time_label, "next_time", next_time);

  return symbols;
}

symbols_t SymbolFactory::rte_lcore_count(call_t call, const Node *node,
                                         bool save) {
  symbols_t symbols;

  assert(!call.ret.isNull());
  auto lcores = call.ret;
  auto lcores_label = build_label("lcores", node, save);
  symbols.emplace(lcores_label, "lcores", lcores);

  return symbols;
}

symbols_t SymbolFactory::nf_set_rte_ipv4_udptcp_checksum(call_t call,
                                                         const Node *node,
                                                         bool save) {
  symbols_t symbols;

  klee::ref<klee::Expr> none;
  auto checksum_label = build_label("checksum", node, save);
  symbols.emplace(checksum_label, "checksum", none);

  return symbols;
}

symbols_t SymbolFactory::dchain_allocate(call_t call, const Node *node,
                                         bool save) {
  symbols_t symbols;

  assert(!call.ret.isNull());
  auto is_dchain_allocated = call.ret;
  auto is_dchain_allocated_label =
      build_label("is_dchain_allocated", node, save);

  symbols.emplace(is_dchain_allocated_label, "is_dchain_allocated",
                  is_dchain_allocated);

  return symbols;
}

symbols_t SymbolFactory::hash_obj(call_t call, const Node *node, bool save) {
  symbols_t symbols;

  klee::ref<klee::Expr> none;
  auto hash_label = build_label("hash", node, save);
  symbols.emplace(hash_label, "hash", none);

  return symbols;
}

void SymbolFactory::translate(Node *current, Node *translation_source,
                              kutil::RenameSymbols renamer) {
  assert(current);
  std::vector<Node *> nodes{current};

  while (nodes.size()) {
    auto node = nodes[0];
    nodes.erase(nodes.begin());
    assert(node);

    if (node->get_type() == Node::NodeType::BRANCH) {
      auto branch_node = static_cast<Branch *>(node);

      auto condition = branch_node->get_condition();
      auto renamed_condition = renamer.rename(condition);

      branch_node->set_condition(renamed_condition);

      assert(branch_node->get_on_true());
      assert(branch_node->get_on_false());

      nodes.push_back(branch_node->get_on_true().get());
      nodes.push_back(branch_node->get_on_false().get());
    } else if (node->get_type() == Node::NodeType::CALL) {
      auto call_node = static_cast<Call *>(node);
      auto call = call_node->get_call();

      for (auto &arg_pair : call.args) {
        auto &arg = call.args[arg_pair.first];

        arg.expr = renamer.rename(arg.expr);
        arg.in = renamer.rename(arg.in);
        arg.out = renamer.rename(arg.out);
      }

      for (auto &extra_var_pair : call.extra_vars) {
        auto &extra_var = call.extra_vars[extra_var_pair.first];

        extra_var.first = renamer.rename(extra_var.first);
        extra_var.second = renamer.rename(extra_var.second);
      }

      call.ret = renamer.rename(call.ret);

      call_node->set_call(call);
      assert(node->get_next());

      nodes.push_back(node->get_next().get());
    }

    auto constraints = node->get_node_constraints();
    auto renamed_constraints = renamer.rename(constraints);

    node->set_node_constraints(renamed_constraints);
  }
}

void SymbolFactory::translate(call_t call, Node_ptr node) {
  assert(node);
  auto found_it = call_processor_lookup_table.find(call.function_name);

  if (found_it == call_processor_lookup_table.end()) {
    std::cerr << "\nSYMBOL FACTORY ERROR: " << call.function_name
              << " not found in lookup table.\n";
    exit(1);
  }

  auto call_processor = found_it->second;
  auto symbols = (this->*call_processor)(call, node.get(), true);

  kutil::RenameSymbols renamer;

  for (auto symbol : symbols) {
    auto new_label = translate_label(symbol.label_base, node);

    if (new_label == symbol.label) {
      continue;
    }

    renamer.add_translation(symbol.label, new_label);
  }

  translate(node.get(), node.get(), renamer);
}

symbols_t SymbolFactory::get_symbols(const Node *node) {
  if (node->get_type() != Node::NodeType::CALL) {
    return symbols_t();
  }

  auto call_node = static_cast<const Call *>(node);
  auto call = call_node->get_call();

  auto found_it = call_processor_lookup_table.find(call.function_name);

  if (found_it == call_processor_lookup_table.end()) {
    std::cerr << "\nSYMBOL FACTORY ERROR: " << call.function_name
              << " not found in lookup table.\n";
    exit(1);
  }

  auto call_processor = found_it->second;
  auto symbols = (this->*call_processor)(call, node, false);

  auto translated_symbols = symbols_t();

  for (auto &symbol : symbols) {
    auto translated_label = translate_label(symbol.label_base, node);
    translated_symbols.emplace(translated_label, symbol.label_base, symbol.expr,
                               symbol.addr);
  }

  return translated_symbols;
}

} // namespace BDD