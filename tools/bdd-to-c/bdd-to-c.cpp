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
        llvm::cl::desc("Output file of the syntethized code. If omited, code "
                       "will be dumped to stdout."),
        llvm::cl::cat(SynthesizerCat));

llvm::cl::opt<std::string>
    XML("xml",
        llvm::cl::desc("Output file of the syntethized code's XML. If omited, "
                       "XML will not be dumped."),
        llvm::cl::cat(SynthesizerCat));

llvm::cl::opt<TargetOption> Target(
    "target", llvm::cl::desc("Output file's target."),
    llvm::cl::cat(SynthesizerCat),
    llvm::cl::values(clEnumValN(SEQUENTIAL, "seq", "Sequential"),
                     clEnumValN(SHARED_NOTHING, "sn", "Shared-nothing"),
                     clEnumValN(LOCKS, "locks", "Lock based"),
                     clEnumValN(TM, "tm", "Transactional memory"),
                     clEnumValN(CALL_PATH_HITTER, "cph", "Call path hitter"),
                     clEnumValEnd),
    llvm::cl::Required);
} // namespace

Node_ptr preppend_call_path_hitter(AST &ast,
                                   const std::vector<uint64_t> &terminating_ids,
                                   Node_ptr node) {
  if (terminating_ids.size() != 1) {
    return node;
  }

  auto call_path_hit_counter = ast.get_from_state("call_path_hit_counter");
  if (!call_path_hit_counter) {
    return node;
  }

  auto terminating_id = terminating_ids[0];

  std::regex call_path_match("test(\\d+)");
  std::smatch matches;

  auto byte = PrimitiveType::build(PrimitiveType::PrimitiveKind::UINT8_T);
  auto idx = Constant::build(PrimitiveType::PrimitiveKind::INT, terminating_id);
  auto call_path_hit_counter_byte =
      Read::build(call_path_hit_counter, byte, idx);
  auto assignment = Assignment::build(
      call_path_hit_counter_byte,
      Add::build(call_path_hit_counter_byte,
                 Constant::build(PrimitiveType::PrimitiveKind::INT, 1)));
  assignment->set_terminate_line(true);

  if (node->get_kind() == Node::NodeKind::BLOCK) {
    auto block = static_cast<Block *>(node.get());
    block->set_enclose(false);
  }

  std::vector<Node_ptr> nodes = {assignment, node};
  return Block::build(nodes);
}

Node_ptr build_ast(AST &ast, const BDD::Node *root, TargetOption target) {
  std::vector<Node_ptr> nodes;
  assert(root);

  while (root != nullptr) {
    BDD::PrinterDebug::debug(root);
    std::cerr << "\n";

    switch (root->get_type()) {
    case BDD::Node::NodeType::BRANCH: {
      auto branch_node = static_cast<const BDD::Branch *>(root);

      auto on_true_bdd = branch_node->get_on_true();
      auto on_false_bdd = branch_node->get_on_false();

      auto cond = branch_node->get_condition();

      ast.push();
      auto then_node = build_ast(ast, on_true_bdd.get(), target);
      ast.pop();

      ast.push();
      auto else_node = build_ast(ast, on_false_bdd.get(), target);
      ast.pop();

      auto cond_node = transpile(&ast, cond, true);

      auto on_true_term = on_true_bdd ? on_true_bdd->get_terminating_node_ids()
                                      : std::vector<uint64_t>();
      auto on_false_term = on_false_bdd
                               ? on_false_bdd->get_terminating_node_ids()
                               : std::vector<uint64_t>();

      if (target == CALL_PATH_HITTER) {
        then_node = preppend_call_path_hitter(ast, on_true_term, then_node);
        else_node = preppend_call_path_hitter(ast, on_false_term, else_node);
      }

      Node_ptr branch = Branch::build(cond_node, then_node, else_node,
                                      on_true_term, on_false_term);
      nodes.push_back(branch);

      root = nullptr;
      break;
    };

    case BDD::Node::NodeType::CALL: {
      auto bdd_call = static_cast<const BDD::Call *>(root);
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

      nodes.push_back(Return::build(ret_value));

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
        assert(false);
      }
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

  auto code_paths = init->count_code_paths() + process->count_code_paths() - 1;

  auto init_root = build_ast(ast, bdd.get_init().get(), target);
  std::vector<Node_ptr> intro_nodes;

  switch (target) {
  case CALL_PATH_HITTER: {
    auto u64 = PrimitiveType::build(PrimitiveType::PrimitiveKind::UINT64_T);
    auto call_path_hit_counter_type = Array::build(u64, code_paths);
    auto call_path_hit_counter =
        Variable::build("call_path_hit_counter", call_path_hit_counter_type);
    ast.push_to_state(call_path_hit_counter);
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

  if (target == CALL_PATH_HITTER) {
    auto void_ret = PrimitiveType::build(PrimitiveType::PrimitiveKind::VOID);
    auto void_ptr_ret = Pointer::build(void_ret);

    auto u64 = PrimitiveType::build(PrimitiveType::PrimitiveKind::UINT64_T);
    auto call_path_hit_counter_type = Array::build(u64, code_paths);
    auto u32 = PrimitiveType::build(PrimitiveType::PrimitiveKind::UINT32_T);

    for (unsigned i = 0; i < code_paths; i++) {
      auto call_path_hit_counter = ast.get_from_state("call_path_hit_counter");
      auto byte = PrimitiveType::build(PrimitiveType::PrimitiveKind::UINT8_T);
      auto idx = Constant::build(PrimitiveType::PrimitiveKind::INT, i);
      auto call_path_hit_counter_byte =
          Read::build(call_path_hit_counter, byte, idx);
      auto assignment = Assignment::build(
          call_path_hit_counter_byte,
          Constant::build(PrimitiveType::PrimitiveKind::INT, 0));
      assignment->set_terminate_line(true);
      intro_nodes_init.push_back(assignment);
    }

    auto u64_ptr = Pointer::build(u64);

    auto call_path_hit_counter = ast.get_from_state("call_path_hit_counter");
    auto call_path_hit_counter_ptr =
        Variable::build("call_path_hit_counter_ptr", u64_ptr);
    auto call_path_hit_counter_sz =
        Variable::build("call_path_hit_counter_sz", u32);
    auto call_paths_sz =
        Constant::build(PrimitiveType::PrimitiveKind::UINT32_T, code_paths);

    auto call_path_hit_counter_ptr_val =
        Assignment::build(call_path_hit_counter_ptr, call_path_hit_counter);
    auto call_path_hit_counter_sz_val =
        Assignment::build(call_path_hit_counter_sz, call_paths_sz);

    call_path_hit_counter_ptr_val->set_terminate_line(true);
    call_path_hit_counter_sz_val->set_terminate_line(true);

    intro_nodes_init.push_back(call_path_hit_counter_ptr_val);
    intro_nodes_init.push_back(call_path_hit_counter_sz_val);
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

  auto process_root = build_ast(ast, bdd.get_process().get(), target);

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

int main(int argc, char **argv) {
  llvm::cl::ParseCommandLineOptions(argc, argv);

  auto bdd = build_bdd();

  AST ast;

  build_ast(ast, bdd, Target);

  if (Out.size()) {
    auto file = std::ofstream(Out);
    assert(file.is_open());
    ast.print(file);
  } else {
    ast.print(std::cout);
  }

  if (XML.size()) {
    auto file = std::ofstream(XML);
    assert(file.is_open());
    ast.print_xml(file);
  }

  return 0;
}
