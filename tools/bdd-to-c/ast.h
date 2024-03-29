#pragma once

#include <algorithm>
#include <dlfcn.h>
#include <expr/Parser.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <regex>
#include <stack>
#include <vector>

#include "call-paths-to-bdd.h"
#include "klee-util.h"
#include "klee_transpiler.h"
#include "load-call-paths.h"
#include "nodes.h"

class AST {
private:
  enum Context { INIT, PROCESS, DONE };

  typedef std::pair<Variable_ptr, klee::ref<klee::Expr>> local_variable_t;
  typedef std::vector<std::vector<local_variable_t>> stack_t;

private:
  std::string output_path;
  std::map<std::string, std::string> callpath_var_translation;
  std::map<std::pair<std::string, TargetOption>, std::string> fname_translation;
  std::map<std::pair<std::string, TargetOption>, std::string>
      struct_translation;

  std::vector<unsigned int> layer;
  std::stack<std::stack<Expr_ptr>> pkt_buffer_offset;

  Context context;
  std::map<Context, std::string> context_markers;

private:
  std::vector<Variable_ptr> state;
  stack_t local_variables;

  std::vector<Node_ptr> global_code;
  Node_ptr nf_init;
  Node_ptr nf_process;

public:
  static constexpr char CHUNK_LAYER_2[] = "ether_header";
  static constexpr char CHUNK_LAYER_3[] = "ipv4_header";
  static constexpr char CHUNK_LAYER_4[] = "tcpudp_header";

  struct chunk_t {
    Variable_ptr var;
    unsigned int start_index;
  };

  chunk_t get_chunk_from_local(unsigned int idx);
  klee::ref<klee::Expr> get_expr_from_local_by_addr(unsigned int addr);

  Variable_ptr get_from_local_by_addr(const std::string &symbol,
                                      unsigned int addr);
  Variable_ptr get_from_local(const std::string &symbol, bool partial = false);

  Expr_ptr get_from_local(klee::ref<klee::Expr> expr);

  void associate_expr_to_local(const std::string &symbol,
                               klee::ref<klee::Expr> expr);

  bool is_in_state(const std::string &symbol) const;
  Variable_ptr get_from_state(unsigned int addr);
  Variable_ptr get_from_state(const std::string &symbol);
  const std::vector<Variable_ptr> &get_state() const { return state; }

  std::string from_cp_symbol(std::string name);

private:
  Variable_ptr generate_new_symbol(klee::ref<klee::Expr> expr,
                                   bool is_signed = false);
  Variable_ptr generate_new_symbol(std::string symbol, Type_ptr type,
                                   unsigned int ptr_lvl,
                                   unsigned int counter_begins);
  Variable_ptr generate_new_symbol(const std::string &symbol, Type_ptr type);

  Node_ptr init_state_node_from_call(const BDD::Call *bdd_call,
                                     TargetOption target);
  Node_ptr process_state_node_from_call(const BDD::Call *bdd_call,
                                        TargetOption target);

  std::string translate_fname(std::string fname, TargetOption target) {
    if (fname_translation.count(std::make_pair(fname, target))) {
      return fname_translation[std::make_pair(fname, target)];
    }

    return fname;
  }

  std::string translate_struct(std::string struct_name, TargetOption target) {
    if (struct_translation.count(std::make_pair(struct_name, target))) {
      return struct_translation[std::make_pair(struct_name, target)];
    }

    return struct_name;
  }

public:
  AST() {
    layer.push_back(2);

    context_switch(INIT);

    callpath_var_translation = {
        {"src_devices", "device"},
        {"p", "packet"},
        {"pkt_len", "packet_length"},
        {"unmber_of_freed_flows", "number_of_freed_flows"},
        {"value_out", "map_value_out"},
        {"val_out", "vector_value_out"},
    };

    fname_translation = {

        /****************************************************************************
         *                                double chain
         ****************************************************************************/
        {{"dchain_allocate", TargetOption::LOCKS}, "dchain_locks_allocate"},
        {{"dchain_allocate", TargetOption::TM}, "dchain_tm_allocate"},
        {{"dchain_allocate_new_index", TargetOption::LOCKS},
         "dchain_locks_allocate_new_index"},
        {{"dchain_allocate_new_index", TargetOption::TM},
         "dchain_tm_allocate_new_index"},
        {{"dchain_rejuvenate_index", TargetOption::LOCKS},
         "dchain_locks_rejuvenate_index"},
        {{"dchain_rejuvenate_index", TargetOption::TM},
         "dchain_tm_rejuvenate_index"},
        {{"dchain_expire_one_index", TargetOption::LOCKS},
         "dchain_locks_expire_one_index"},
        {{"dchain_expire_one_index", TargetOption::TM},
         "dchain_tm_expire_one_index"},
        {{"dchain_is_index_allocated", TargetOption::LOCKS},
         "dchain_locks_is_index_allocated"},
        {{"dchain_is_index_allocated", TargetOption::TM},
         "dchain_tm_is_index_allocated"},
        {{"dchain_free_index", TargetOption::LOCKS}, "dchain_locks_free_index"},
        {{"dchain_free_index", TargetOption::TM}, "dchain_tm_free_index"},

        /****************************************************************************
         *                                map
         ****************************************************************************/
        {{"map_allocate", TargetOption::LOCKS}, "map_locks_allocate"},
        {{"map_get", TargetOption::LOCKS}, "map_locks_get"},
        {{"map_put", TargetOption::LOCKS}, "map_locks_put"},
        {{"map_erase", TargetOption::LOCKS}, "map_locks_erase"},
        {{"map_size", TargetOption::LOCKS}, "map_locks_size"},

        /****************************************************************************
         *                                vector
         ****************************************************************************/
        {{"vector_allocate", TargetOption::LOCKS}, "vector_locks_allocate"},
        {{"vector_borrow", TargetOption::LOCKS}, "vector_locks_borrow"},
        {{"vector_return", TargetOption::LOCKS}, "vector_locks_return"},

        /****************************************************************************
         *                                sketch
         ****************************************************************************/
        {{"sketch_allocate", TargetOption::LOCKS}, "sketch_locks_allocate"},
        {{"sketch_compute_hashes", TargetOption::LOCKS},
         "sketch_locks_compute_hashes"},
        {{"sketch_refresh", TargetOption::LOCKS}, "sketch_locks_refresh"},
        {{"sketch_fetch", TargetOption::LOCKS}, "sketch_locks_fetch"},
        {{"sketch_touch_buckets", TargetOption::LOCKS},
         "sketch_locks_touch_buckets"},
        {{"sketch_expire", TargetOption::LOCKS}, "sketch_locks_expire"},
        {{"sketch_allocate", TargetOption::TM}, "sketch_tm_allocate"},
        {{"sketch_compute_hashes", TargetOption::TM},
         "sketch_tm_compute_hashes"},
        {{"sketch_refresh", TargetOption::TM}, "sketch_tm_refresh"},
        {{"sketch_fetch", TargetOption::TM}, "sketch_tm_fetch"},
        {{"sketch_touch_buckets", TargetOption::TM}, "sketch_tm_touch_buckets"},
        {{"sketch_expire", TargetOption::TM}, "sketch_tm_expire"},

        /****************************************************************************
         *                                expirator
         ****************************************************************************/
        {{"expire_items", TargetOption::LOCKS}, "expire_items_locks"},
        {{"expire_items_single_map", TargetOption::LOCKS},
         "expire_items_single_map_locks"},
        {{"expire_items_single_map_offseted", TargetOption::LOCKS},
         "expire_items_single_map_offseted_locks"},
        {{"expire_items_single_map_iteratively", TargetOption::LOCKS},
         "expire_items_single_map_iteratively_locks"},
        {{"expire_items", TargetOption::TM}, "expire_items_tm"},
        {{"expire_items_single_map", TargetOption::TM},
         "expire_items_single_map_tm"},
        {{"expire_items_single_map_offseted", TargetOption::TM},
         "expire_items_single_map_offseted_tm"},
        {{"expire_items_single_map_iteratively", TargetOption::TM},
         "expire_items_single_map_iteratively_tm"},

        /****************************************************************************
         *                                double map
         ****************************************************************************/
        {{"dmap_allocate", TargetOption::LOCKS}, "dmap_locks_allocate"},
        {{"dmap_get_a", TargetOption::LOCKS}, "dmap_locks_get_a"},
        {{"dmap_get_b", TargetOption::LOCKS}, "dmap_locks_get_b"},
        {{"dmap_put", TargetOption::LOCKS}, "dmap_locks_put"},
        {{"dmap_get_value", TargetOption::LOCKS}, "dmap_locks_get_value"},
        {{"dmap_erase", TargetOption::LOCKS}, "dmap_locks_erase"},
        {{"dmap_size", TargetOption::LOCKS}, "dmap_locks_size"},

        /****************************************************************************
         *                                cht
         ****************************************************************************/
        {{"cht_fill_cht", TargetOption::LOCKS}, "cht_locks_fill_cht"},
        {{"cht_find_preferred_available_backend", TargetOption::LOCKS},
         "cht_locks_find_preferred_available_backend"},
        {{"cht_fill_cht", TargetOption::TM}, "cht_tm_fill_cht"},
        {{"cht_find_preferred_available_backend", TargetOption::TM},
         "cht_tm_find_preferred_available_backend"},
    };

    struct_translation = {
        {{"DoubleChain", TargetOption::LOCKS}, "DoubleChainLocks"},
        {{"DoubleChain", TargetOption::TM}, "DoubleChainTM"},
        {{"Map", TargetOption::LOCKS}, "MapLocks"},
        {{"Vector", TargetOption::LOCKS}, "VectorLocks"},
        {{"DoubleMap", TargetOption::LOCKS}, "DoubleMapLocks"},
        {{"Sketch", TargetOption::LOCKS}, "SketchLocks"},
        {{"Sketch", TargetOption::TM}, "SketchTM"},
    };
  }

  void context_switch(Context ctx);
  void commit(Node_ptr body);

  void push_global_code(Node_ptr _global_code) {
    global_code.push_back(_global_code);
  }

  bool function_in_global_code(const std::string &fname) {
    for (const auto &node : global_code) {
      if (node->get_kind() != Node::NodeKind::FUNCTION) {
        continue;
      }

      Function *fn = static_cast<Function *>(node.get());

      if (fn->get_name() == fname) {
        return true;
      }
    }

    return false;
  }

  void push();
  void pop();

  void branch_pkt_offset();
  std::pair<bool, Expr_ptr> inc_pkt_offset(Expr_ptr offset);
  void dec_pkt_offset();

  void push_to_state(Variable_ptr var);
  void push_to_local(Variable_ptr var);
  void push_to_local(Variable_ptr var, klee::ref<klee::Expr> expr);

  Node_ptr node_from_call(const BDD::Call *bdd_call, TargetOption target);

  bool is_done() { return context == DONE; }

  void dump_stack() const {
    std::cerr << "\n";

    std::cerr << "Global variables"
              << "\n";
    for (const auto &gv : state) {
      gv->debug(std::cerr, 2);
    }
    std::cerr << "\n";

    std::cerr << "Stack variables"
              << "\n";
    for (const auto &stack : local_variables) {
      std::cerr << "  ==================================="
                << "\n";
      for (const auto var : stack) {
        var.first->debug(std::cerr, 2);
        if (!var.second.isNull()) {
          std::cerr << "  expr: " << kutil::expr_to_string(var.second) << "\n";
        }
      }
    }
    std::cerr << "\n";
  }

  void print(std::ostream &os) const {
    if (global_code.size()) {
      os << "\n";
      auto block = Block::build(global_code, false);
      block->synthesize(os);
      os << "\n";
    }

    if (nf_init) {
      os << "\n";
      nf_init->synthesize(os);
      os << "\n";
    }

    if (nf_process) {
      os << "\n";
      nf_process->synthesize(os);
      os << "\n";
    }
  }

  void print_xml(std::ostream &os) const {
    if (nf_init) {
      nf_init->debug(os);
      os << "\n";
    }

    if (nf_process) {
      nf_process->debug(os);
      os << "\n";
    }
  }

  static Node_ptr grab_locks() {
    std::vector<Node_ptr> nodes;

    std::string attempt_flag_name = "write_attempt";
    std::string state_flag_name = "write_state";

    auto type = PrimitiveType::build(PrimitiveType::PrimitiveKind::BOOL);
    auto ret = PrimitiveType::build(PrimitiveType::PrimitiveKind::VOID);

    auto attempt_flag_renamed = Variable::build(attempt_flag_name, type);
    auto state_flag_renamed = Variable::build(state_flag_name, type);

    std::vector<ExpressionType_ptr> attempt_flag_args = {attempt_flag_renamed};
    std::vector<ExpressionType_ptr> state_flag_args = {state_flag_renamed};

    auto grab_attempt_flag =
        FunctionCall::build("RTE_PER_LCORE", attempt_flag_args, ret);
    auto grab_state_flag =
        FunctionCall::build("RTE_PER_LCORE", state_flag_args, ret);

    grab_attempt_flag->set_terminate_line(true);
    grab_state_flag->set_terminate_line(true);

    auto pointer_to_bool_type = Pointer::build(
        PrimitiveType::build(PrimitiveType::PrimitiveKind::BOOL));

    auto attempt_flag =
        Variable::build(attempt_flag_name + "_ptr", pointer_to_bool_type);
    auto state_flag =
        Variable::build(state_flag_name + "_ptr", pointer_to_bool_type);

    auto attempt_flag_decl = VariableDecl::build(attempt_flag);
    auto state_flag_decl = VariableDecl::build(state_flag);

    auto attempt_flag_assignment = Assignment::build(
        attempt_flag_decl, AddressOf::build(grab_attempt_flag));
    auto state_flag_assignment =
        Assignment::build(state_flag_decl, AddressOf::build(grab_state_flag));

    attempt_flag_assignment->set_terminate_line(true);
    state_flag_assignment->set_terminate_line(true);

    nodes.push_back(attempt_flag_assignment);
    nodes.push_back(state_flag_assignment);

    return Block::build(nodes, false);
  }

  static Node_ptr check_write_attempt() {
    std::string attempt_flag_name = "write_attempt";
    std::string state_flag_name = "write_state";

    auto pointer_to_bool_type = Pointer::build(
        PrimitiveType::build(PrimitiveType::PrimitiveKind::BOOL));
    auto ret =
        Return::build(Constant::build(PrimitiveType::PrimitiveKind::INT, 1));

    auto attempt_flag =
        Variable::build(attempt_flag_name + "_ptr", pointer_to_bool_type);
    auto state_flag =
        Variable::build(state_flag_name + "_ptr", pointer_to_bool_type);

    auto read_attempt_flag = Read::build(attempt_flag);
    auto read_state_flag = Read::build(state_flag);

    auto cond =
        LogicalAnd::build(read_attempt_flag, Not::build(read_state_flag));

    return Branch::build(cond, ret);
  }

  static Node_ptr write_attempt() {
    std::string attempt_flag_name = "write_attempt";
    std::string state_flag_name = "write_state";

    auto pointer_to_bool_type = Pointer::build(
        PrimitiveType::build(PrimitiveType::PrimitiveKind::BOOL));
    auto one = Constant::build(PrimitiveType::PrimitiveKind::INT, 1);
    auto ret = Return::build(one);

    auto attempt_flag =
        Variable::build(attempt_flag_name + "_ptr", pointer_to_bool_type);
    auto state_flag =
        Variable::build(state_flag_name + "_ptr", pointer_to_bool_type);

    auto read_attempt_flag = Read::build(attempt_flag);
    auto read_state_flag = Read::build(state_flag);

    auto cond = Not::build(read_state_flag);

    auto assign_attempt_flag = Assignment::build(read_attempt_flag, one);
    assign_attempt_flag->set_terminate_line(true);

    std::vector<Node_ptr> on_true_nodes = {assign_attempt_flag, ret};
    auto on_true = Block::build(on_true_nodes);

    return Branch::build(cond, on_true);
  }
};
