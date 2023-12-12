#pragma once

#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "nodes/branch.h"
#include "nodes/call.h"

namespace BDD {

class SymbolFactory {
public:
  static std::vector<std::string> ignored_symbols;
  static std::vector<std::string> symbols_without_translation;

  static bool should_ignore(std::string symbol);
  static bool should_not_translate(std::string symbol);

private:
  struct label_t {
    std::string base;
    std::string used;

    label_t(const std::string &_base, const std::string &_used)
        : base(_base), used(_used) {}
  };

  std::vector<std::vector<label_t>> stack;

  typedef symbols_t (SymbolFactory::*CallProcessorPtr)(call_t call,
                                                       const Node *node,
                                                       bool save);
  std::map<std::string, CallProcessorPtr> call_processor_lookup_table;

private:
  bool has_arg(call_t call, std::string arg) {
    auto found = call.args.find(arg);
    return found != call.args.end();
  }

  bool has_extra_var(call_t call, std::string arg) {
    auto found = call.extra_vars.find(arg);
    return found != call.extra_vars.end();
  }

  int count_labels(std::string base) {
    int counter = 0;

    for (auto frame : stack) {
      for (auto label : frame) {
        if (label.base == base) {
          counter++;
        }
      }
    }

    return counter;
  }

  std::string build_label(std::string base, const Node *node, bool save);
  std::string build_label(klee::ref<klee::Expr> expr, std::string base,
                          bool save);
  symbols_t no_process(call_t call, const Node *node, bool save);
  symbols_t sketch_fetch(call_t call, const Node *node, bool save);
  symbols_t sketch_touch_buckets(call_t call, const Node *node, bool save);
  symbols_t cht_fill_cht(call_t call, const Node *node, bool save);
  symbols_t LoadBalancedFlow_hash(call_t call, const Node *node, bool save);
  symbols_t cht_find_preferred_available_backend(call_t call, const Node *node,
                                                 bool save);
  symbols_t map_get(call_t call, const Node *node, bool save);
  symbols_t dchain_is_index_allocated(call_t call, const Node *node, bool save);
  symbols_t dchain_allocate_new_index(call_t call, const Node *node, bool save);
  symbols_t packet_borrow_next_secret(call_t call, const Node *node, bool save);
  symbols_t packet_borrow_next_chunk(call_t call, const Node *node, bool save);
  symbols_t expire_items_single_map(call_t call, const Node *node, bool save);
  symbols_t expire_items_single_map_iteratively(call_t call, const Node *node,
                                                bool save);
  symbols_t rte_ether_addr_hash(call_t call, const Node *node, bool save);
  symbols_t vector_borrow(call_t call, const Node *node, bool save);
  symbols_t sketch_allocate(call_t call, const Node *node, bool save);
  symbols_t map_allocate(call_t call, const Node *node, bool save);
  symbols_t vector_allocate(call_t call, const Node *node, bool save);
  symbols_t current_time(call_t call, const Node *node, bool save);
  symbols_t rte_lcore_count(call_t call, const Node *node, bool save);
  symbols_t nf_set_rte_ipv4_udptcp_checksum(call_t call, const Node *node,
                                            bool save);
  symbols_t dchain_allocate(call_t call, const Node *node, bool save);
  symbols_t hash_obj(call_t call, const Node *node, bool save);

public:
  SymbolFactory() {
    call_processor_lookup_table = {
        {"start_time", &SymbolFactory::no_process},
        {"current_time", &SymbolFactory::current_time},
        {"rte_lcore_count", &SymbolFactory::rte_lcore_count},
        {"loop_invariant_consume", &SymbolFactory::no_process},
        {"loop_invariant_produce", &SymbolFactory::no_process},
        {"packet_receive", &SymbolFactory::no_process},
        {"packet_borrow_next_chunk", &SymbolFactory::packet_borrow_next_chunk},
        {"packet_borrow_next_secret", &SymbolFactory::packet_borrow_next_secret},
        {"packet_insert_new_chunk", &SymbolFactory::no_process},
        {"packet_shrink_chunk", &SymbolFactory::no_process},
        {"packet_get_unread_length", &SymbolFactory::no_process},
        {"packet_state_total_length", &SymbolFactory::no_process},
        {"packet_return_chunk", &SymbolFactory::no_process},
        {"packet_send", &SymbolFactory::no_process},
        {"packet_free", &SymbolFactory::no_process},
        {"map_allocate", &SymbolFactory::map_allocate},
        {"map_get", &SymbolFactory::map_get},
        {"map_put", &SymbolFactory::no_process},
        {"vector_allocate", &SymbolFactory::vector_allocate},
        {"vector_borrow", &SymbolFactory::vector_borrow},
        {"vector_return", &SymbolFactory::no_process},
        {"map_erase", &SymbolFactory::no_process},
        {"dchain_allocate", &SymbolFactory::dchain_allocate},
        {"dchain_allocate_new_index",
         &SymbolFactory::dchain_allocate_new_index},
        {"dchain_is_index_allocated",
         &SymbolFactory::dchain_is_index_allocated},
        {"dchain_rejuvenate_index", &SymbolFactory::no_process},
        {"dchain_free_index", &SymbolFactory::no_process},
        {"expire_items_single_map", &SymbolFactory::expire_items_single_map},
        {"expire_items_single_map_iteratively",
         &SymbolFactory::expire_items_single_map_iteratively},
        {"sketch_allocate", &SymbolFactory::sketch_allocate},
        {"sketch_compute_hashes", &SymbolFactory::no_process},
        {"sketch_refresh", &SymbolFactory::no_process},
        {"sketch_fetch", &SymbolFactory::sketch_fetch},
        {"sketch_touch_buckets", &SymbolFactory::sketch_touch_buckets},
        {"sketch_expire", &SymbolFactory::no_process},
        {"cht_fill_cht", &SymbolFactory::cht_fill_cht},
        {"LoadBalancedFlow_hash", &SymbolFactory::LoadBalancedFlow_hash},
        {"cht_find_preferred_available_backend",
         &SymbolFactory::cht_find_preferred_available_backend},
        {"rte_ether_addr_hash", &SymbolFactory::rte_ether_addr_hash},
        {"nf_set_rte_ipv4_udptcp_checksum",
         &SymbolFactory::nf_set_rte_ipv4_udptcp_checksum},
        {"hash_obj", &SymbolFactory::hash_obj},
    };

    stack.emplace_back();
  }

public:
  std::string translate_label(std::string base, const Node *node) {
    if (should_not_translate(base)) {
      return base;
    }

    std::stringstream new_label;
    new_label << base << "__" << node->get_id();
    return new_label.str();
  }

  std::string translate_label(std::string base, Node_ptr node) {
    return translate_label(base, node.get());
  }

  void translate(Node *current, Node *translation_source,
                 kutil::RenameSymbols renamer);

  void translate(call_t call, Node_ptr node);

  void push() { stack.emplace_back(); }
  void pop() { stack.pop_back(); }

  symbols_t get_symbols(const Node *node);
};
} // namespace BDD
