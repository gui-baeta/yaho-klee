#include "klee/ExprBuilder.h"
#include "klee/perf-contracts.h"
#include "klee/util/ArrayCache.h"
#include "klee/util/ExprSMTLIBPrinter.h"
#include "klee/util/ExprVisitor.h"
#include "llvm/Support/CommandLine.h"
#include <klee/Constraints.h>
#include <klee/Solver.h>

#include <algorithm>
#include <dlfcn.h>
#include <expr/Parser.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <regex>
#include <stack>
#include <utility>
#include <vector>

#include "ast.h"
#include "call-paths-to-bdd.h"
#include "load-call-paths.h"
#include "nodes.h"

namespace {
llvm::cl::list<std::string> InputCallPathFiles(llvm::cl::desc("<call paths>"),
                                               llvm::cl::Positional);

llvm::cl::OptionCategory SynthesizerCat("Synthesizer specific options");

llvm::cl::opt<std::string>
    InputBDDFile("in", llvm::cl::desc("Input file for BDD deserialization."),
                 llvm::cl::cat(SynthesizerCat));

llvm::cl::opt<std::string>
    Out("out",
        llvm::cl::desc("Output C++ file of the syntethized code. If omited, "
                       "code will be dumped to stdout."),
        llvm::cl::cat(SynthesizerCat));

llvm::cl::opt<std::string>
    XML("xml",
        llvm::cl::desc("Output file of the syntethized code's XML. If omited, "
                       "XML will not be dumped."),
        llvm::cl::cat(SynthesizerCat));

llvm::cl::opt<TargetOption>
    Target("target", llvm::cl::desc("Output file's target."),
           llvm::cl::cat(SynthesizerCat),
           llvm::cl::values(clEnumValN(SEQUENTIAL, "seq", "Sequential"),
                            clEnumValN(SHARED_NOTHING, "sn", "Shared-nothing"),
                            clEnumValN(LOCKS, "locks", "Lock based"),
                            clEnumValN(TM, "tm", "Transactional memory"),
                            clEnumValN(BDD_NODE_HIT_RATE, "bdd-analyzer",
                                       "BDD node hit rate"),
                            clEnumValEnd),
           llvm::cl::Required);
} // namespace

Node_ptr update_bdd_node_hit_rate(AST &ast, uint64_t node_id) {
  auto bdd_node_hit_counter = ast.get_from_state("bdd_node_hit_counter");
  assert(bdd_node_hit_counter);

  auto byte = PrimitiveType::build(PrimitiveType::PrimitiveKind::UINT8_T);
  auto idx = Constant::build(PrimitiveType::PrimitiveKind::INT, node_id);
  auto bdd_node_hit_counter_byte = Read::build(bdd_node_hit_counter, byte, idx);
  auto assignment = Assignment::build(
      bdd_node_hit_counter_byte,
      Add::build(bdd_node_hit_counter_byte,
                 Constant::build(PrimitiveType::PrimitiveKind::INT, 1)));
  assignment->set_terminate_line(true);

  return assignment;
}

Node_ptr build_ast(AST &ast, const BDD::Node *root, TargetOption target,
                   bool processing_packets) {
  std::vector<Node_ptr> nodes;
  assert(root);

  while (root != nullptr) {
    BDD::PrinterDebug::debug(root);
    std::cerr << "\n";

    switch (root->get_type()) {
    case BDD::Node::NodeType::BRANCH: {
      auto branch_node = static_cast<const BDD::Branch *>(root);

      if (processing_packets && processing_packets &&
          target == BDD_NODE_HIT_RATE) {
        auto hit_rate_update =
            update_bdd_node_hit_rate(ast, branch_node->get_id());
        nodes.push_back(hit_rate_update);
      }

      auto on_true_bdd = branch_node->get_on_true();
      auto on_false_bdd = branch_node->get_on_false();

      auto cond = branch_node->get_condition();

      ast.push();
      auto then_node =
          build_ast(ast, on_true_bdd.get(), target, processing_packets);
      ast.pop();

      ast.push();
      auto else_node =
          build_ast(ast, on_false_bdd.get(), target, processing_packets);
      ast.pop();

      auto cond_node = transpile(&ast, cond, true);

      auto on_true_term = on_true_bdd ? on_true_bdd->get_terminating_node_ids()
                                      : std::vector<uint64_t>();
      auto on_false_term = on_false_bdd
                               ? on_false_bdd->get_terminating_node_ids()
                               : std::vector<uint64_t>();

      Branch_ptr branch = Branch::build(cond_node, then_node, else_node,
                                        on_true_term, on_false_term);
      nodes.push_back(branch);

      root = nullptr;
      break;
    };

    case BDD::Node::NodeType::CALL: {
      auto bdd_call = static_cast<const BDD::Call *>(root);

      if (processing_packets && target == BDD_NODE_HIT_RATE) {
        auto hit_rate_update =
            update_bdd_node_hit_rate(ast, bdd_call->get_id());
        nodes.push_back(hit_rate_update);
      }

      auto call_node = ast.node_from_call(bdd_call, target);

      if (call_node) {
        nodes.push_back(call_node);
      }

      root = root->get_next().get();
      break;
    };

    case BDD::Node::NodeType::RETURN_INIT: {
      auto return_init = static_cast<const BDD::ReturnInit *>(root);
      Expr_ptr ret_value;

      switch (return_init->get_return_value()) {
      case BDD::ReturnInit::ReturnType::SUCCESS: {
        ret_value = Constant::build(PrimitiveType::PrimitiveKind::INT, 1);
        break;
      };
      case BDD::ReturnInit::ReturnType::FAILURE: {
        ret_value = Constant::build(PrimitiveType::PrimitiveKind::INT, 0);
        break;
      };
      }

      Node_ptr ret_node = Return::build(ret_value);

      if (processing_packets && target == BDD_NODE_HIT_RATE) {
        auto hit_rate_update =
            update_bdd_node_hit_rate(ast, return_init->get_id());
        nodes.push_back(hit_rate_update);
      }

      nodes.push_back(ret_node);

      root = nullptr;
      break;
    };

    case BDD::Node::NodeType::RETURN_PROCESS: {
      auto return_process = static_cast<const BDD::ReturnProcess *>(root);
      Node_ptr new_node;

      switch (return_process->get_return_operation()) {
      case BDD::ReturnProcess::Operation::FWD:
      case BDD::ReturnProcess::Operation::BCAST: {
        Expr_ptr ret_value =
            Constant::build(PrimitiveType::PrimitiveKind::INT,
                            return_process->get_return_value());
        new_node = Return::build(ret_value);
        break;
      };
      case BDD::ReturnProcess::Operation::DROP: {
        Node_ptr ret = Return::build(ast.get_from_local("device"));
        Comment_ptr comm = Comment::build("dropping");
        new_node = Block::build(std::vector<Node_ptr>{comm, ret}, false);
        break;
      };
      default: {
        assert(false && "Unknown return operation");
        std::cerr << "ERROR: unknown return operation\n";
        exit(1);
      }
      }

      if (processing_packets && target == BDD_NODE_HIT_RATE) {
        auto hit_rate_update =
            update_bdd_node_hit_rate(ast, return_process->get_id());
        nodes.push_back(hit_rate_update);
      }

      nodes.push_back(new_node);

      root = nullptr;
      break;
    };

    default: {
      assert(false);
    }
    }
  }

  assert(nodes.size());
  return Block::build(nodes);
}

void build_ast(AST &ast, const BDD::BDD &bdd, TargetOption target) {
  auto init = bdd.get_init().get();
  auto process = bdd.get_process().get();

  assert(init);
  assert(process);

  // The node IDs start from 0
  auto total_processing_nodes = bdd.get_max_node_id() + 1;

  auto init_root = build_ast(ast, bdd.get_init().get(), target, false);
  std::vector<Node_ptr> intro_nodes;

  switch (target) {
  case BDD_NODE_HIT_RATE: {
    auto u64 = PrimitiveType::build(PrimitiveType::PrimitiveKind::UINT64_T);
    auto bdd_node_hit_counter_type = Array::build(u64, total_processing_nodes);
    auto bdd_node_hit_counter =
        Variable::build("bdd_node_hit_counter", bdd_node_hit_counter_type);
    ast.push_to_state(bdd_node_hit_counter);
    [[fallthrough]];
  }
  case LOCKS:
  case TM:
  case SEQUENTIAL: {
    auto state = ast.get_state();
    for (auto gv : state) {
      VariableDecl_ptr decl = VariableDecl::build(gv);
      decl->set_terminate_line(true);
      ast.push_global_code(decl);
    }

    break;
  }
  case SHARED_NOTHING: {
    auto state = ast.get_state();
    for (auto &var : state) {
      // global
      std::string name = var->get_symbol();

      auto type = var->get_type();
      auto renamed = Variable::build("_" + name, type);
      auto ret = PrimitiveType::build(PrimitiveType::PrimitiveKind::VOID);

      std::vector<ExpressionType_ptr> define_args = {type, renamed};
      std::vector<ExpressionType_ptr> args = {renamed};

      auto def = FunctionCall::build("RTE_DEFINE_PER_LCORE", define_args, ret);
      def->set_terminate_line(true);
      ast.push_global_code(def);

      // intro
      auto grab = FunctionCall::build("RTE_PER_LCORE", args, ret);
      grab->set_terminate_line(true);

      auto var_ptr = Variable::build(name + "_ptr", Pointer::build(type));
      auto decl = VariableDecl::build(var_ptr);
      auto assignment = Assignment::build(decl, AddressOf::build(grab));
      assignment->set_terminate_line(true);

      // hack
      var->change_symbol("(*" + name + "_ptr)");

      intro_nodes.push_back(assignment);
    }

    break;
  }
  }

  assert(init_root->get_kind() == Node::NodeKind::BLOCK);
  std::vector<Node_ptr> intro_nodes_init;

  if (target == TM) {
    auto void_ret = PrimitiveType::build(PrimitiveType::PrimitiveKind::VOID);
    std::vector<ExpressionType_ptr> no_args;

    auto rte_lcore_id = FunctionCall::build("rte_lcore_id", no_args, void_ret);

    std::vector<ExpressionType_ptr> args = {rte_lcore_id};
    auto HTM_thr_init = FunctionCall::build("HTM_thr_init", args, void_ret);
    HTM_thr_init->set_terminate_line(true);

    intro_nodes_init.push_back(HTM_thr_init);
  }

  if (target == BDD_NODE_HIT_RATE) {
    auto void_ret = PrimitiveType::build(PrimitiveType::PrimitiveKind::VOID);
    auto void_ptr_ret = Pointer::build(void_ret);

    auto u64 = PrimitiveType::build(PrimitiveType::PrimitiveKind::UINT64_T);
    auto bdd_node_hit_counter_type = Array::build(u64, total_processing_nodes);
    auto u32 = PrimitiveType::build(PrimitiveType::PrimitiveKind::UINT32_T);

    for (unsigned i = 0; i < total_processing_nodes; i++) {
      auto bdd_node_hit_counter = ast.get_from_state("bdd_node_hit_counter");
      auto byte = PrimitiveType::build(PrimitiveType::PrimitiveKind::UINT8_T);
      auto idx = Constant::build(PrimitiveType::PrimitiveKind::INT, i);
      auto bdd_node_hit_counter_byte =
          Read::build(bdd_node_hit_counter, byte, idx);
      auto assignment = Assignment::build(
          bdd_node_hit_counter_byte,
          Constant::build(PrimitiveType::PrimitiveKind::INT, 0));
      assignment->set_terminate_line(true);
      intro_nodes_init.push_back(assignment);
    }

    auto u64_ptr = Pointer::build(u64);

    auto bdd_node_hit_counter = ast.get_from_state("bdd_node_hit_counter");
    auto bdd_node_hit_counter_ptr =
        Variable::build("bdd_node_hit_counter_ptr", u64_ptr);
    auto bdd_node_hit_counter_sz =
        Variable::build("bdd_node_hit_counter_sz", u32);
    auto call_paths_sz = Constant::build(PrimitiveType::PrimitiveKind::UINT32_T,
                                         total_processing_nodes);

    auto bdd_node_hit_counter_ptr_val =
        Assignment::build(bdd_node_hit_counter_ptr, bdd_node_hit_counter);
    auto bdd_node_hit_counter_sz_val =
        Assignment::build(bdd_node_hit_counter_sz, call_paths_sz);

    bdd_node_hit_counter_ptr_val->set_terminate_line(true);
    bdd_node_hit_counter_sz_val->set_terminate_line(true);

    intro_nodes_init.push_back(bdd_node_hit_counter_ptr_val);
    intro_nodes_init.push_back(bdd_node_hit_counter_sz_val);
  }

  if (target == LOCKS || target == TM) {
    auto ret = PrimitiveType::build(PrimitiveType::PrimitiveKind::INT);
    std::vector<ExpressionType_ptr> args;

    auto rte_get_master_lcore =
        FunctionCall::build("rte_get_master_lcore", args, ret);
    auto rte_lcore_id = FunctionCall::build("rte_lcore_id", args, ret);

    auto cond = Not::build(Equals::build(rte_get_master_lcore, rte_lcore_id));

    auto one = Constant::build(PrimitiveType::PrimitiveKind::INT, 1);
    auto on_true = Return::build(one);

    intro_nodes_init.push_back(Branch::build(cond, on_true));
  }

  intro_nodes_init.insert(intro_nodes_init.end(), intro_nodes.begin(),
                          intro_nodes.end());
  intro_nodes_init.push_back(Block::build(init_root, false));

  init_root = Block::build(intro_nodes_init);
  ast.commit(init_root);

  auto process_root = build_ast(ast, bdd.get_process().get(), target, true);

  assert(process_root->get_kind() == Node::NodeKind::BLOCK);
  std::vector<Node_ptr> intro_nodes_process = intro_nodes;

  if (target == LOCKS) {
    intro_nodes_process.push_back(AST::grab_locks());
  }

  intro_nodes_process.push_back(Block::build(process_root, false));
  process_root = Block::build(intro_nodes_process);

  ast.commit(process_root);
}

BDD::BDD build_bdd() {
  assert((InputBDDFile.size() != 0 || InputCallPathFiles.size() != 0) &&
         "Please provide either at least 1 call path file, or a bdd file");

  if (InputBDDFile.size() > 0) {
    return BDD::BDD(InputBDDFile);
  }

  std::vector<call_path_t *> call_paths;

  for (auto file : InputCallPathFiles) {
    std::cerr << "Loading: " << file << std::endl;

    call_path_t *call_path = load_call_path(file);
    call_paths.push_back(call_path);
  }

  return BDD::BDD(call_paths);
}

void synthesize(const AST &ast, TargetOption target, std::ostream &out) {
  // Not very OS friendly, but oh well...
  auto source_file = std::string(__FILE__);
  auto boilerplate_path =
      source_file.substr(0, source_file.rfind("/")) + "/boilerplates/";

  switch (target) {
  case SEQUENTIAL: {
    boilerplate_path += "sequential.template.cpp";
    break;
  }
  case BDD_NODE_HIT_RATE: {
    boilerplate_path += "bdd-analyzer.template.cpp";
    break;
  }
  default: {
    assert(false && "No boilerplate for this target");
  }
  }

  auto boilerplate = std::ifstream(boilerplate_path, std::ios::in);
  assert(!boilerplate.fail() && "Boilerplate file not found");

  out << boilerplate.rdbuf();
  ast.print(out);
}

int main(int argc, char **argv) {
  llvm::cl::ParseCommandLineOptions(argc, argv);

  auto bdd = build_bdd();

  AST ast;
  build_ast(ast, bdd, Target);

  if (Out.size()) {
    auto file = std::ofstream(Out);
    assert(file.is_open());
    synthesize(ast, Target, file);
  } else {
    synthesize(ast, Target, std::cout);
  }

  if (XML.size()) {
    auto file = std::ofstream(XML);
    assert(file.is_open());
    ast.print_xml(file);
  }

  return 0;
}
