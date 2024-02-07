/* -*- mode: c++; c-basic-offset: 2; -*- */

//===-- ktest-dehavoc.cpp ---------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

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
#include <vector>

#include "klee-util.h"
#include "load-call-paths.h"

namespace {
llvm::cl::list<std::string> InputCallPathFiles(llvm::cl::desc("<call paths>"),
                                               llvm::cl::Positional,
                                               llvm::cl::OneOrMore);
} // namespace

// term colors
// src: https://stackoverflow.com/questions/9158150/colored-output-in-c/9158263
#define RESET "\033[0m"
#define BLACK "\033[30m"              /* Black */
#define RED "\033[31m"                /* Red */
#define GREEN "\033[32m"              /* Green */
#define YELLOW "\033[33m"             /* Yellow */
#define BLUE "\033[34m"               /* Blue */
#define MAGENTA "\033[35m"            /* Magenta */
#define CYAN "\033[36m"               /* Cyan */
#define WHITE "\033[37m"              /* White */
#define BOLDBLACK "\033[1m\033[30m"   /* Bold Black */
#define BOLDRED "\033[1m\033[31m"     /* Bold Red */
#define BOLDGREEN "\033[1m\033[32m"   /* Bold Green */
#define BOLDYELLOW "\033[1m\033[33m"  /* Bold Yellow */
#define BOLDBLUE "\033[1m\033[34m"    /* Bold Blue */
#define BOLDMAGENTA "\033[1m\033[35m" /* Bold Magenta */
#define BOLDCYAN "\033[1m\033[36m"    /* Bold Cyan */
#define BOLDWHITE "\033[1m\033[37m"   /* Bold White */

#define DEBUG

#define UINT_16_SWAP_ENDIANNESS(p) ((((p)&0xff) << 8) | ((p) >> 8 & 0xff))

class KleeInterface {
private:
  std::map<std::string, klee::ConstraintManager> call_path_constraints;
  klee::Solver *solver;

  const klee::ConstraintManager &
  get_constraint(const std::string &call_path_filename) const {
    assert(call_path_constraints.count(call_path_filename) &&
           "No constraints saved for this call_path");
    return call_path_constraints.at(call_path_filename);
  }

public:
  KleeInterface() {
    solver = klee::createCoreSolver(klee::Z3_SOLVER);
    assert(solver);

    solver = createCexCachingSolver(solver);
    solver = createCachingSolver(solver);
    solver = createIndependentSolver(solver);
  }

  KleeInterface(const KleeInterface &interface) : KleeInterface() {
    call_path_constraints = interface.call_path_constraints;
  }

  std::string expr_to_smt(klee::expr::ExprHandle expr) {
    klee::ConstraintManager empty_constraints;
    klee::ExprSMTLIBPrinter smtPrinter;
    std::string expr_str;
    llvm::raw_string_ostream os(expr_str);

    // hack: constants were generating an smt query containing just "assert
    // true", which were breaking things later along the line
    if (expr->getKind() == klee::Expr::Kind::Constant) {
      klee::ExprBuilder *exprBuilder = klee::createDefaultExprBuilder();
      expr = exprBuilder->Xor(expr, exprBuilder->Constant(0, expr->getWidth()));
    }

    smtPrinter.setOutput(os);

    klee::Query sat_query(empty_constraints, expr);

    smtPrinter.setQuery(sat_query);

    smtPrinter.generateOutput();
    os.str();

    return expr_str;
  }

  void add_constraints(const std::string &call_path_filename,
                       klee::ConstraintManager constraints) {
    call_path_constraints[call_path_filename] = constraints;
  }

  bool evaluate_expr_must_be_false(klee::expr::ExprHandle expr,
                                   std::string call_path_filename) const {
    const auto &constraints = get_constraint(call_path_filename);

    klee::Query sat_query(constraints, expr);

    bool success;
    bool result;

    success = solver->mustBeFalse(sat_query, result);
    assert(success);

    return result;
  }

  bool evaluate_expr_must_be_true(klee::expr::ExprHandle expr,
                                  std::string call_path_filename) const {
    const auto &constraints = get_constraint(call_path_filename);

    klee::Query sat_query(constraints, expr);

    bool success;
    bool result;

    success = solver->mustBeTrue(sat_query, result);
    assert(success);

    return result;
  }

  std::vector<uint64_t> evaluate_expr(klee::expr::ExprHandle expr,
                                      std::string call_path_filename) const {
    klee::ExprBuilder *exprBuilder = klee::createDefaultExprBuilder();
    std::vector<uint64_t> solutions;

    auto constraints = get_constraint(call_path_filename);

    for (;;) {
      klee::Query sat_query(constraints, expr);
      klee::ref<klee::ConstantExpr> result;

      bool success = solver->getValue(sat_query, result);

      if (!success && solutions.size() > 0)
        break;

      else if (!success) {
        std::cerr << RED << "expression: " << kutil::expr_to_string(expr)
                  << "\n"
                  << RESET;
        assert(false && "Solver unable to obtain value for given expression");
      }

      auto new_solution = result.get()->getZExtValue(expr->getWidth());
      solutions.push_back(new_solution);

      constraints.addConstraint(
          exprBuilder->Not(exprBuilder->Eq(expr, result)));

      klee::expr::ExprHandle solutions_set = exprBuilder->Eq(
          expr, exprBuilder->Constant(solutions[0], expr->getWidth()));
      for (unsigned int i = 1; i < solutions.size(); i++) {
        solutions_set = exprBuilder->Or(
            solutions_set,
            exprBuilder->Eq(
                expr, exprBuilder->Constant(solutions[i], expr->getWidth())));
      }

      auto solution_set_complete =
          evaluate_expr_must_be_true(solutions_set, call_path_filename);
      if (solution_set_complete)
        break;
    }

    return solutions;
  }

  std::vector<unsigned>
  readLSB_byte_indexes(klee::ReadExpr *expr,
                       std::string call_path_filename) const {
    std::vector<unsigned> bytes;
    auto solutions = evaluate_expr(expr->index, call_path_filename);
    assert(solutions.size() == 1);

    auto index = solutions[0];
    bytes.push_back(index);
    return bytes;
  }

  std::vector<unsigned>
  readLSB_byte_indexes(klee::ConcatExpr *expr,
                       std::string call_path_filename) const {
    std::vector<unsigned> bytes;
    std::vector<unsigned> right_bytes, left_bytes;

    if (klee::ConcatExpr *right =
            dyn_cast<klee::ConcatExpr>(expr->getRight())) {
      right_bytes = readLSB_byte_indexes(right, call_path_filename);
    } else if (klee::ReadExpr *right =
                   dyn_cast<klee::ReadExpr>(expr->getRight())) {
      right_bytes = readLSB_byte_indexes(right, call_path_filename);
    } else {
      assert(false && "Unknown expression on readLSB_byte_indexes");
    }

    bytes.insert(bytes.end(), right_bytes.begin(), right_bytes.end());

    if (klee::ConcatExpr *left = dyn_cast<klee::ConcatExpr>(expr->getLeft())) {
      left_bytes = readLSB_byte_indexes(left, call_path_filename);
    } else if (klee::ReadExpr *left =
                   dyn_cast<klee::ReadExpr>(expr->getLeft())) {
      left_bytes = readLSB_byte_indexes(left, call_path_filename);
    } else {
      assert(false && "Unknown expression on readLSB_byte_indexes");
    }

    bytes.insert(bytes.end(), left_bytes.begin(), left_bytes.end());

    return bytes;
  }

  unsigned int readLSB_parse(klee::expr::ExprHandle expr,
                             std::string call_path_filename) const {
    std::vector<unsigned> bytes_read;

    if (klee::ReadExpr *read = dyn_cast<klee::ReadExpr>(expr)) {
      bytes_read = readLSB_byte_indexes(read, call_path_filename);
    } else if (klee::ConcatExpr *concat = dyn_cast<klee::ConcatExpr>(expr)) {
      bytes_read = readLSB_byte_indexes(concat, call_path_filename);
    } else {
      assert(false && "cast missing");
    }

    return *std::min_element(bytes_read.begin(), bytes_read.end());
  }

  bool has_packet(klee::expr::ExprHandle expr,
                  std::vector<unsigned> &bytes_read,
                  std::string call_path_filename) const {
    if (klee::ConcatExpr *concat = dyn_cast<klee::ConcatExpr>(expr)) {
      bool hp = false;

      hp |= has_packet(concat->getLeft(), bytes_read, call_path_filename);
      hp |= has_packet(concat->getRight(), bytes_read, call_path_filename);

      return hp;
    } else if (klee::ReadExpr *read = dyn_cast<klee::ReadExpr>(expr)) {
      if (read->updates.root == nullptr)
        return false;
      if (read->updates.root->getName() != "packet_chunks")
        return false;

      auto solutions = evaluate_expr(read->index, call_path_filename);
      assert(solutions.size() == 1);

      auto index = solutions[0];
      bytes_read.push_back(index);

      return true;
    }

    bool hp = false;
    for (unsigned i = 0; i < expr->getNumKids(); i++)
      hp = has_packet(expr->getKid(i), bytes_read, call_path_filename) || hp;

    return hp;
  }
};

class RetrieveUniqueSymbolsNames : public klee::ExprVisitor::ExprVisitor {
private:
  std::vector<std::string> retrieved;

public:
  RetrieveUniqueSymbolsNames() : ExprVisitor(true) {}

  klee::ExprVisitor::Action visitRead(const klee::ReadExpr &e) {
    klee::UpdateList ul = e.updates;
    const klee::Array *root = ul.root;

    auto found_it = std::find(retrieved.begin(), retrieved.end(), root->name);
    if (found_it == retrieved.end()) {
      retrieved.push_back(root->name);
    }

    return klee::ExprVisitor::Action::doChildren();
  }

  std::vector<std::string> get_retrieved() { return retrieved; }
};

class RetrieveReadsOnSymbol : public klee::ExprVisitor::ExprVisitor {
private:
  struct read_expr_t {
    klee::expr::ExprHandle expr;
    unsigned int index;

    read_expr_t(klee::expr::ExprHandle _expr, unsigned int _index)
        : expr(_expr), index(_index) {}
  };

  klee::ExprBuilder *builder = klee::createDefaultExprBuilder();

  std::vector<read_expr_t> retrieved;
  std::string symbol_name;

  KleeInterface klee_interface;

public:
  unsigned int size;

  RetrieveReadsOnSymbol(const std::string &_symbol_name) : ExprVisitor(true) {
    symbol_name = _symbol_name;
    klee_interface.add_constraints("", klee::ConstraintManager());
  }

  klee::ExprVisitor::Action visitRead(const klee::ReadExpr &e) {
    klee::UpdateList ul = e.updates;
    const klee::Array *root = ul.root;

    if (root->name == symbol_name) {
      size = root->getSize();

      klee::expr::ExprHandle index = e.index;
      assert(index->getKind() == klee::Expr::Kind::Constant);

      auto solutions = klee_interface.evaluate_expr(index, "");
      assert(solutions.size() == 1);

      read_expr_t read(klee::expr::ExprHandle(const_cast<klee::ReadExpr *>(&e)),
                       solutions[0]);
      retrieved.push_back(read);
    }

    return klee::ExprVisitor::Action::doChildren();
  }

  std::vector<read_expr_t> get_retrieved() { return retrieved; }
};

class RenameChunks : public klee::ExprVisitor::ExprVisitor {
private:
  int ref_counter;
  std::pair<bool, std::pair<std::string, std::string>> name_swapper;

  klee::ExprBuilder *builder = klee::createDefaultExprBuilder();
  std::map<klee::ref<klee::Expr>, klee::ref<klee::Expr>> replacements;

  klee::ArrayCache arr_cache;
  std::vector<const klee::Array *> new_arrays;
  std::vector<klee::UpdateList> new_uls;

public:
  static constexpr auto marker_signature = "__ref_";

  RenameChunks() : ExprVisitor(true) {}
  RenameChunks(const int &id) : ExprVisitor(true) { set_counter(id); }

  klee::ExprVisitor::Action visitExprPost(const klee::Expr &e) {
    std::map<klee::ref<klee::Expr>, klee::ref<klee::Expr>>::const_iterator it =
        replacements.find(klee::ref<klee::Expr>(const_cast<klee::Expr *>(&e)));

    if (it != replacements.end()) {
      return Action::changeTo(it->second);
    } else {
      return Action::doChildren();
    }
  }

  void set_name_swapper(std::string before, std::string after) {
    name_swapper = std::make_pair(true, std::make_pair(before, after));
  }

  void set_counter(int _counter) {
    ref_counter = _counter;
    name_swapper.first = false;
  }

  klee::ExprVisitor::Action visitRead(const klee::ReadExpr &e) {
    klee::UpdateList ul = e.updates;
    const klee::Array *root = ul.root;

    std::string original_name;
    std::string new_name;

    auto is_chunk = root->name.find("packet_chunks") != std::string::npos;

    if (name_swapper.first) {
      original_name = name_swapper.second.first;
      new_name = name_swapper.second.second;
    } else if (ref_counter >= 0 && is_chunk) {
      size_t marker = root->name.find(marker_signature);
      original_name = marker == std::string::npos
                          ? root->name
                          : root->name.substr(0, marker);
      new_name = original_name + marker_signature + std::to_string(ref_counter);
    } else if (is_chunk) {
      size_t marker = root->name.find(marker_signature);
      new_name = marker == std::string::npos ? root->name
                                             : root->name.substr(0, marker);
      original_name = new_name + marker_signature + std::to_string(ref_counter);
    } else {
      return Action::doChildren();
    }

    if (root->name == original_name) {
      const klee::Array *new_root = arr_cache.CreateArray(
          new_name, root->getSize(), root->constantValues.begin().base(),
          root->constantValues.end().base(), root->getDomain(),
          root->getRange());

      new_arrays.push_back(new_root);
      new_uls.emplace_back(new_root, ul.head);

      klee::expr::ExprHandle replacement =
          builder->Read(new_uls.back(), e.index);

      replacements.insert(
          {klee::expr::ExprHandle(const_cast<klee::ReadExpr *>(&e)),
           replacement});

      return Action::changeTo(replacement);
    }

    return Action::doChildren();
  }
};

klee::expr::ExprHandle get_arg_expr_from_call(const call_t &call,
                                              const std::string &arg_name) {
  klee::expr::ExprHandle target;

  if (call.extra_vars.count(arg_name)) {
    auto target_pair = call.extra_vars.at(arg_name);

    if (!target_pair.second.isNull()) {
      target = target_pair.second;
    } else if (!target_pair.first.isNull()) {
      target = target_pair.first;
    } else {
      assert(false && "Both in and out values of extra_var are null");
    }
  } else if (call.args.count(arg_name)) {
    arg_t target_arg = call.args.at(arg_name);

    if (!target_arg.out.isNull()) {
      target = target_arg.out;
    } else if (!target_arg.in.isNull()) {
      target = target_arg.in;
    } else {
      target = target_arg.expr;
    }
  } else {
    std::cerr << RED;
    std::cerr << "Argument not in function"
              << "\n";
    std::cerr << "  function:      " << call.function_name << "\n";
    std::cerr << "  requested arg: " << arg_name << "\n";
    std::cerr << "  args:          ";
    for (const auto &arg : call.args)
      std::cerr << arg.first << " ";
    std::cerr << "\n";
    std::cerr << RESET;

    assert(call.args.count(arg_name) && "Argument not present on this call");
  }

  return target;
}

struct packet_chunk_t {

  struct protocol_t {
    enum state_t { COMPLETE, INCOMPLETE, NO_INFO };

    unsigned int code;
    state_t state;
  };

  struct fragment_t {
    unsigned int offset;
    klee::expr::ExprHandle length;
    klee::expr::ExprHandle expr;

    fragment_t(unsigned int _offset, klee::expr::ExprHandle _length,
               klee::expr::ExprHandle _expr)
        : offset(_offset), length(_length), expr(_expr) {}

    fragment_t(const fragment_t &fragment)
        : fragment_t(fragment.offset, fragment.length, fragment.expr) {}
    fragment_t(const packet_chunk_t &fragment) {
      const auto &first_fragment = fragment.fragments[0];

      offset = first_fragment.offset;
      length = first_fragment.length;
      expr = first_fragment.expr;
    }
  };

  std::shared_ptr<KleeInterface> klee_interface;
  std::string call_path_filename;

  std::vector<fragment_t> fragments;
  unsigned int layer;
  protocol_t protocol;

  std::vector<unsigned int> packet_fields_dependencies;

  packet_chunk_t(unsigned int _offset, klee::expr::ExprHandle _length,
                 klee::expr::ExprHandle _expr,
                 std::shared_ptr<KleeInterface> _klee_interface,
                 const std::string &_call_path_filename) {
    fragments.emplace_back(_offset, _length, _expr);
    protocol.state = protocol_t::state_t::NO_INFO;
    klee_interface = _klee_interface;
    call_path_filename = _call_path_filename;
  }

  packet_chunk_t(const packet_chunk_t &chunk)
      : fragments(chunk.fragments), layer(chunk.layer),
        protocol(chunk.protocol),
        packet_fields_dependencies(chunk.packet_fields_dependencies) {
    klee_interface = chunk.klee_interface;
    call_path_filename = chunk.call_path_filename;
  }

  void set_and_verify_protocol(unsigned int code) {
    klee::ExprBuilder *exprBuilder = klee::createDefaultExprBuilder();

    const auto &expr = fragments[0].expr;
    protocol.code = code;

    if (layer == 3) {
      // IP
      if (protocol.code == 0x0800) {
        klee::ref<klee::Expr> ihl_le_5_expr = exprBuilder->Ule(
            exprBuilder->And(exprBuilder->Extract(expr, 0, klee::Expr::Int8),
                             exprBuilder->Constant(0b1111, klee::Expr::Int8)),
            exprBuilder->Constant(5, klee::Expr::Int8));

        bool ihl_gt_5 = klee_interface->evaluate_expr_must_be_false(
            ihl_le_5_expr, call_path_filename);
        protocol.state = !ihl_gt_5 ? protocol_t::state_t::COMPLETE
                                   : protocol_t::state_t::INCOMPLETE;

      } else {
        std::cerr << MAGENTA
                  << "[WARNING] Layer 3 protocol not in set { IP, VLAN }"
                  << RESET << std::endl;
      }
    } else if (layer == 4) {
      protocol.state = protocol_t::state_t::COMPLETE;
    } else {
      std::cerr << RED << "[WARNING] Not implemented: trying to parse layer "
                << layer << RESET << std::endl;
    }
  }

  std::vector<packet_chunk_t>
  set_protocol_from_previous_chunk(const packet_chunk_t &prev_chunk) {
    assert(klee_interface && "Trying to set protocol from previous chunk with "
                             "invalid klee interface");

    klee::ExprBuilder *exprBuilder = klee::createDefaultExprBuilder();

    // In case there are multiple solutions for the protocol value,
    // we need to fork this chunk into multiple chunks,
    // and all of them together complete the set of all
    // possible values for the protocol.
    std::vector<packet_chunk_t> forked_chunks;

    const auto &previous_chunk_expr = prev_chunk.fragments[0].expr;

    klee::ref<klee::Expr> proto_expr;

    if (layer == 3) {
      proto_expr =
          exprBuilder->Extract(previous_chunk_expr, 12 * 8, klee::Expr::Int16);
    } else if (layer == 4) {
      proto_expr =
          exprBuilder->Extract(previous_chunk_expr, 9 * 8, klee::Expr::Int8);
    } else {
      std::cerr << RED << "[WARNING] Not implemented: trying to parse layer "
                << layer << RESET << std::endl;
    }

    auto protocol_code_solutions =
        klee_interface->evaluate_expr(proto_expr, call_path_filename);

    unsigned int protocol_code = protocol_code_solutions[0];
    set_and_verify_protocol(layer == 3 ? UINT_16_SWAP_ENDIANNESS(protocol_code)
                                       : protocol_code);

    for (unsigned int i = 1; i < protocol_code_solutions.size(); i++) {
      packet_chunk_t forked_chunk(*this);

      unsigned int protocol_code = protocol_code_solutions[i];
      set_and_verify_protocol(
          layer == 3 ? UINT_16_SWAP_ENDIANNESS(protocol_code) : protocol_code);

      forked_chunks.emplace_back(forked_chunk);
    }

    return forked_chunks;
  }

  unsigned int get_fragments_size() const { return fragments.size(); }

  const klee::expr::ExprHandle &get_fragment_expr(unsigned int idx) const {
    assert(idx < fragments.size());
    return fragments[0].expr;
  }

  bool is_complete() const {
    return protocol.state != protocol_t::state_t::INCOMPLETE;
  }

  bool has_dependencies() const {
    return packet_fields_dependencies.size() > 0;
  }

  void append_fragment(const packet_chunk_t &fragment) {
    assert(protocol.state == protocol_t::state_t::INCOMPLETE &&
           "Trying to append fragment without setting the protocol first");
    fragments.emplace_back(fragment);

    // FIXME: careful...
    protocol.state = protocol_t::state_t::COMPLETE;
  }

  bool is_dependency_inside_boundaries(unsigned int dependency,
                                       const fragment_t &fragment) {
    klee::ExprBuilder *exprBuilder = klee::createDefaultExprBuilder();

    // fragment.offset <= dependency && dependency <= fragment.offset +
    // fragment.length
    klee::ref<klee::Expr> dependency_inside_boundaries = exprBuilder->And(
        exprBuilder->Ule(
            exprBuilder->Constant(fragment.offset, klee::Expr::Int32),
            exprBuilder->Constant(dependency, klee::Expr::Int32)),
        exprBuilder->Ule(
            exprBuilder->Constant(dependency, klee::Expr::Int32),
            exprBuilder->Add(
                exprBuilder->Constant(fragment.offset, klee::Expr::Int32),
                fragment.length)));

    return klee_interface->evaluate_expr_must_be_true(
        dependency_inside_boundaries, call_path_filename);
  }

  bool add_dependency(unsigned int dependency) {
    for (const auto &fragment : fragments) {
      if (is_dependency_inside_boundaries(dependency, fragment)) {
        packet_fields_dependencies.push_back(dependency - fragment.offset);
        return true;
      }
    }

    return false;
  }

  void add_dependency_by_offset(unsigned int offset) {
    packet_fields_dependencies.push_back(offset);
  }

  void print() const {
    std::cerr << "  layer        " << layer << "\n";

    if (protocol.state != protocol_t::state_t::NO_INFO) {
      std::cerr << "  protocol     " << protocol.code;
      if (protocol.state == protocol_t::state_t::INCOMPLETE)
        std::cerr << " (incomplete)";
      std::cerr << "\n";
    }

    std::cerr << "  fragments    ";
    if (fragments.size() == 0)
      std::cerr << "\n";
    for (unsigned int i = 0; i < fragments.size(); i++) {
      const auto &fragment = fragments[i];
      if (i > 0)
        std::cerr << "               ";
      std::cerr << "offset " << fragment.offset;
      std::cerr << " expression " << kutil::expr_to_string(fragment.expr);
      std::cerr << "\n";
    }

    if (packet_fields_dependencies.size() == 0)
      return;

    std::cerr << "  dependencies ";
    for (unsigned int i = 0; i < packet_fields_dependencies.size(); i++) {
      const auto &dependency = packet_fields_dependencies[i];
      if (i > 0)
        std::cerr << "               ";
      std::cerr << dependency << "\n";
    }
  }

  void report() const {
    assert(protocol.state != protocol_t::state_t::INCOMPLETE);
    if (packet_fields_dependencies.size() == 0)
      return;

    std::cout << "BEGIN CHUNK"
              << "\n";

    std::cout << "layer " << layer << "\n";

    std::cout << "protocol? ";
    if (protocol.state == protocol_t::state_t::COMPLETE)
      std::cout << protocol.code;
    std::cout << "\n";

    for (const auto &dependency : packet_fields_dependencies) {
      std::cout << "dependency " << dependency << "\n";
    }

    std::cout << "END CHUNK"
              << "\n";
  }
};

struct translation_unit_t {
  unsigned int layer;
  unsigned int offset;

  klee::expr::ExprHandle received;
  klee::expr::ExprHandle translated;
};

class PacketManager {
private:
  typedef void (PacketManager::*packet_manager_call_handler_t)(
      const call_t &call);
  typedef std::map<std::string, packet_manager_call_handler_t>
      packet_manager_call_handler_map_t;

  std::pair<bool, unsigned int> src_device;
  std::pair<bool, unsigned int> dst_device;

  std::stack<std::pair<klee::expr::ExprHandle, unsigned int>>
      borrowed_chunk_layer_pairs;
  std::vector<packet_chunk_t> borrowed_chunks_processed;
  std::vector<translation_unit_t> translations;

  std::string call_path_filename;

  std::shared_ptr<KleeInterface> klee_interface;
  packet_manager_call_handler_map_t call_handler_map;

private:
  // Handlers
  void packet_receive(const call_t &call) {
    assert(call.args.count("src_devices") &&
           "packet_receive handler without argument \"src_devices\"");
    auto src_device_expr = get_arg_expr_from_call(call, "src_devices");

    auto solutions =
        klee_interface->evaluate_expr(src_device_expr, call_path_filename);
    assert(solutions.size() == 1);

    src_device = std::make_pair(true, solutions[0]);
  }

  void packet_send(const call_t &call) {
    assert(call.args.count("dst_device") &&
           "packet_send handler without argument \"dst_device\"");
    auto dst_device_expr = get_arg_expr_from_call(call, "dst_device");

    auto solutions =
        klee_interface->evaluate_expr(dst_device_expr, call_path_filename);
    assert(solutions.size() == 1);

    dst_device = std::make_pair(true, solutions[0]);
  }

  void packet_borrow_next_chunk(const call_t &call) {
    assert(call.extra_vars.count("the_chunk") &&
           "packet_borrow_next_chunk without \"the_chunk\" extra var");
    assert(!call.extra_vars.at("the_chunk").second.isNull() &&
           "packet_borrow_next_chunk with invalid \"the_chunk\" expression");

    assert(call.args.find("length") != call.args.end() &&
           "packet_borrow_next_chunk without \"length\" variable");
    assert(!call.args.at("length").expr.isNull() &&
           "packet_borrow_next_chunk with invalid \"length\" expression");

    auto the_chunk_expr = call.extra_vars.at("the_chunk").second;
    auto length_expr = call.args.at("length").expr;
    auto offset =
        klee_interface->readLSB_parse(the_chunk_expr, call_path_filename);

    packet_chunk_t packet_chunk(offset, length_expr, the_chunk_expr,
                                klee_interface, call_path_filename);

    if (borrowed_chunks_processed.size() &&
        !borrowed_chunks_processed.back().is_complete()) {
      borrowed_chunks_processed.back().append_fragment(packet_chunk);
      borrowed_chunk_layer_pairs.push(std::make_pair(
          the_chunk_expr, borrowed_chunks_processed.back().layer));
      return;
    }

    if (borrowed_chunks_processed.size() == 0) {
      // start in layer 2 and increment from there
      packet_chunk.layer = 2;
    } else {
      const auto &previous_chunk = borrowed_chunks_processed.back();
      packet_chunk.layer = previous_chunk.layer + 1;

      auto forked_chunks =
          packet_chunk.set_protocol_from_previous_chunk(previous_chunk);
      borrowed_chunks_processed.insert(borrowed_chunks_processed.end(),
                                       forked_chunks.begin(),
                                       forked_chunks.end());
    }

    borrowed_chunk_layer_pairs.push(
        std::make_pair(the_chunk_expr, packet_chunk.layer));
    borrowed_chunks_processed.push_back(packet_chunk);
  }

  void packet_return_chunk(const call_t &call) {
    klee::ExprBuilder *exprBuilder = klee::createDefaultExprBuilder();

    assert(call.args.count("the_chunk") &&
           "packet_return_chunk handler without argument \"the_chunk\"");
    auto the_chunk_expr = get_arg_expr_from_call(call, "the_chunk");
    auto borrowed_expr = borrowed_chunk_layer_pairs.top().first;
    auto borrowed_layer = borrowed_chunk_layer_pairs.top().second;
    auto expr_width = borrowed_expr->getWidth();

    if (borrowed_layer == 2) {
      borrowed_chunk_layer_pairs.pop();
      return;
    }

    for (unsigned int w = 0; w < expr_width; w += 8) {
      auto returned_byte =
          exprBuilder->Extract(the_chunk_expr, w, klee::Expr::Int8);
      auto borrowed_byte =
          exprBuilder->Extract(borrowed_expr, w, klee::Expr::Int8);

      auto chunks_byte_eq_expr = exprBuilder->Eq(returned_byte, borrowed_byte);

      auto chunks_byte_eq = klee_interface->evaluate_expr_must_be_true(
          chunks_byte_eq_expr, call_path_filename);

      if (!chunks_byte_eq) {
        translations.push_back(translation_unit_t{
            borrowed_layer, w / 8, borrowed_byte, returned_byte});
      }
    }

    borrowed_chunk_layer_pairs.pop();
  }

  void nop(const call_t &call) {}

public:
  PacketManager() {}

  PacketManager(std::shared_ptr<KleeInterface> _klee_interface,
                const std::string &_call_path_filename) {
    src_device.first = false;
    dst_device.first = false;

    klee_interface = _klee_interface;
    call_path_filename = _call_path_filename;

    call_handler_map["packet_send"] = &PacketManager::packet_send;
    call_handler_map["packet_receive"] = &PacketManager::packet_receive;
    call_handler_map["packet_borrow_next_chunk"] =
        &PacketManager::packet_borrow_next_chunk;
    call_handler_map["packet_return_chunk"] =
        &PacketManager::packet_return_chunk;
    call_handler_map["packet_state_total_length"] = &PacketManager::nop;
    call_handler_map["packet_free"] = &PacketManager::nop;
    call_handler_map["packet_get_unread_length"] = &PacketManager::nop;
  }

  PacketManager(const PacketManager &pm)
      : src_device(pm.src_device), dst_device(pm.dst_device),
        borrowed_chunk_layer_pairs(pm.borrowed_chunk_layer_pairs),
        borrowed_chunks_processed(pm.borrowed_chunks_processed),
        translations(pm.translations),
        call_path_filename(pm.call_path_filename) {
    klee_interface = pm.klee_interface;
  }

  PacketManager &operator=(const PacketManager &other) = default;

  bool process_packet_call(const call_t &call) {
    if (!call_handler_map.count(call.function_name))
      return false;

    packet_manager_call_handler_t &handler =
        call_handler_map[call.function_name];
    (this->*handler)(call);

    return true;
  }

  bool is_src_device_set() const { return src_device.first; }

  const unsigned int &get_src_device() const {
    assert(src_device.first && "Unset source device");
    return src_device.second;
  }

  bool is_dst_device_set() const { return dst_device.first; }

  const unsigned int &get_dst_device() const {
    assert(dst_device.first && "Unset destination device");
    return dst_device.second;
  }

  const std::vector<packet_chunk_t> &get_chunks() const {
    return borrowed_chunks_processed;
  }
  const std::shared_ptr<KleeInterface> &get_klee_interface() const {
    return klee_interface;
  }
  const std::string &get_call_path_filename() const {
    return call_path_filename;
  }

  void update_devices(const PacketManager &pm) {
    if (pm.is_src_device_set()) {
      src_device = std::make_pair(true, pm.get_src_device());
    }

    if (pm.is_dst_device_set()) {
      dst_device = std::make_pair(true, pm.get_dst_device());
    }
  }

  void add_dependencies(const std::vector<unsigned int> &bytes) {
    for (const auto &byte : bytes) {
      auto found = false;

      for (auto &chunk : borrowed_chunks_processed) {
        if (chunk.add_dependency(byte)) {
          found = true;
        }
      }

      if (!found) {
        std::cerr << RED;
        std::cerr << "[ERROR] byte " << byte
                  << " not associated with any chunk."
                  << "\n";
        std::cerr << RESET;

        assert(false && "Byte dependency not associated with any chunk");
      }
    }
  }

  void add_dependency(unsigned int layer, unsigned int offset_in_layer) {
    for (auto &chunk : borrowed_chunks_processed) {
      if (chunk.layer == layer) {
        chunk.add_dependency_by_offset(offset_in_layer);
      }
    }
  }

  bool has_dependencies() const {
    for (const auto &chunk : borrowed_chunks_processed)
      if (chunk.has_dependencies())
        return true;
    return false;
  }

  std::vector<translation_unit_t> get_translation_units() const {
    return translations;
  }

  void print() const {
    for (const auto &chunk : borrowed_chunks_processed) {
      if (chunk.has_dependencies()) {
        chunk.print();
        std::cerr << "\n";
      }
    }
  }

  void report() const {
    if (!has_dependencies())
      return;

    std::cout << "BEGIN PACKET DEPENDENCIES"
              << "\n";

    for (const auto &chunk : borrowed_chunks_processed)
      chunk.report();

    std::cout << "END PACKET DEPENDENCIES"
              << "\n";
  }
};

class LibvigAccessExpressionArgument {
public:
  enum Type { READ, WRITE, RESULT };

private:
  Type type;

  std::pair<bool, std::string> name;
  std::pair<bool, klee::expr::ExprHandle> expr;

  PacketManager packet_dependencies;

public:
  LibvigAccessExpressionArgument(Type _type) {
    type = _type;
    name = std::make_pair(false, "");
    expr = std::make_pair(false, nullptr);
  }

  LibvigAccessExpressionArgument(const LibvigAccessExpressionArgument &expr_arg)
      : LibvigAccessExpressionArgument(expr_arg.type) {
    if (expr_arg.is_name_set()) {
      name = std::make_pair(true, expr_arg.get_name());
    }

    if (expr_arg.is_expr_set()) {
      expr = std::make_pair(true, expr_arg.get_expr());
    }

    packet_dependencies = expr_arg.get_packet_dependencies();
  }

  LibvigAccessExpressionArgument &
  operator=(const LibvigAccessExpressionArgument &other) = default;

  bool is_name_set() const { return name.first; }
  bool is_expr_set() const { return expr.first; }

  bool has_packet_dependencies() const {
    return packet_dependencies.has_dependencies();
  }

  const Type &get_type() const { return type; }

  const std::string &get_name() const {
    assert(name.first && "Trying to get unset name");
    return name.second;
  }

  const klee::expr::ExprHandle &get_expr() const {
    assert(expr.first && "Trying to get unset expression");
    return expr.second;
  }

  const PacketManager &get_packet_dependencies() const {
    return packet_dependencies;
  }

  void set_name(const std::string &_name) {
    name = std::make_pair(true, _name);
  }

  void set_expr(const call_t &call) {
    if (!name.first)
      return;
    expr = std::make_pair(true, get_arg_expr_from_call(call, name.second));
  }

  void set_expr(klee::expr::ExprHandle _expr) {
    expr.first = true;
    expr.second = _expr;
  }

  void
  set_packet_dependencies_force(const PacketManager &_packet_dependencies) {
    packet_dependencies = _packet_dependencies;
  }

  void set_packet_dependencies(const PacketManager &_packet_dependencies) {
    if (!name.first || !expr.first)
      return;
    packet_dependencies = _packet_dependencies;

    std::vector<unsigned int> bytes_read;

    const auto &klee_interface = packet_dependencies.get_klee_interface();
    const auto &call_path_filename =
        packet_dependencies.get_call_path_filename();

    if (klee_interface->has_packet(expr.second, bytes_read,
                                   call_path_filename)) {
      packet_dependencies.add_dependencies(bytes_read);
    }
  }

  void update_dependencies_devices(const PacketManager &pm) {
    if (!name.first)
      return;
    packet_dependencies.update_devices(pm);
  }

  void report(const unsigned int &id) const {
    if (!name.first)
      return;
    assert(expr.first);
    const auto &klee_interface = packet_dependencies.get_klee_interface();

    std::cout << "BEGIN ARGUMENT"
              << "\n";

    std::cout << "type ";
    switch (type) {
    case READ:
      std::cout << "read";
      break;
    case WRITE:
      std::cout << "write";
      break;
    case RESULT:
      std::cout << "result";
      break;
    default:
      assert(false);
    }
    std::cout << "\n";

    std::cout << "BEGIN EXPRESSION"
              << "\n";

    RenameChunks renamed(id);
    auto renamed_expr = renamed.visit(expr.second);

    std::cout << klee_interface->expr_to_smt(renamed_expr) << "\n";
    std::cout << "END EXPRESSION"
              << "\n";

    packet_dependencies.report();

    std::cout << "END ARGUMENT"
              << "\n";
  }
};

class LibvigAccess {
public:
  enum operation { READ, WRITE, NOP, INIT, CREATE, VERIFY, UPDATE, DESTROY };

private:
  std::pair<bool, unsigned int> id;

  std::pair<bool, unsigned int> src_device;
  std::pair<bool, unsigned int> dst_device;

  std::string interface;
  std::pair<std::string, unsigned int> obj;

  LibvigAccessExpressionArgument read_arg;
  LibvigAccessExpressionArgument write_arg;
  LibvigAccessExpressionArgument result_arg;

  std::pair<bool, uint64_t> success;

  operation op;

  std::string call_path_filename;
  std::shared_ptr<KleeInterface> klee_interface;

  LibvigAccess(operation _op)
      : read_arg(LibvigAccessExpressionArgument(
            LibvigAccessExpressionArgument::Type::READ)),
        write_arg(LibvigAccessExpressionArgument(
            LibvigAccessExpressionArgument::Type::WRITE)),
        result_arg(LibvigAccessExpressionArgument(
            LibvigAccessExpressionArgument::Type::RESULT)),
        op(_op) {
    reset();
  }

  void set_src_device(unsigned int _src_device) {
    if (src_device.first) {
      std::cerr << RED << "current " << src_device.second << "\n";
      assert(src_device.second == _src_device &&
             "Already set source device with different value");
    }

    src_device = std::make_pair(true, _src_device);
  }

  void set_dst_device(unsigned int _dst_device) {
    if (dst_device.first)
      assert(dst_device.second == _dst_device &&
             "Already set destination device with different value");
    dst_device = std::make_pair(true, _dst_device);
  }

public:
  LibvigAccess(const LibvigAccess &lva)
      : id(lva.id), src_device(lva.src_device), dst_device(lva.dst_device),
        interface(lva.interface), obj(lva.obj), read_arg(lva.read_arg),
        write_arg(lva.write_arg), result_arg(lva.result_arg),
        success(lva.success), op(lva.op),
        call_path_filename(lva.call_path_filename) {
    klee_interface = lva.klee_interface;
  }

  // consume, but ignore
  LibvigAccess(std::string _interface) : LibvigAccess(NOP) {
    interface = _interface;
  }

  // create INIT
  LibvigAccess(std::string _interface, std::string _obj_name, operation _op)
      : LibvigAccess(_op) {
    assert((_op == INIT) && "Wrong use of INIT constructor");
    interface = _interface;
    obj.first = _obj_name;
  }

  // create CREATE
  LibvigAccess(std::string _interface, std::string _obj_name,
               std::string _read_result_name, operation _op)
      : LibvigAccess(_op) {
    assert(
        (_op == CREATE || _op == VERIFY || _op == UPDATE || _op == DESTROY) &&
        "Wrong use of CREATE/VERIFY/DESTROY constructor");
    interface = _interface;
    obj.first = _obj_name;

    if (_op == CREATE) {
      result_arg.set_name(_read_result_name);
    } else {
      read_arg.set_name(_read_result_name);
    }
  }

  LibvigAccess(std::string _interface, std::string _obj_name,
               std::string _arg_name, std::string _second_arg_name,
               operation _op)
      : LibvigAccess(_op) {
    assert((_op == READ || _op == WRITE) &&
           "Wrong use of READ and WRITE constructor");

    interface = _interface;
    obj.first = _obj_name;
    read_arg.set_name(_arg_name);

    if (_op == READ && _second_arg_name.size()) {
      result_arg.set_name(_second_arg_name);
    } else if (_op != READ && _second_arg_name.size()) {
      write_arg.set_name(_second_arg_name);
    }
  }

  LibvigAccess &operator=(const LibvigAccess &other) = default;

  void replace_arg(LibvigAccessExpressionArgument::Type type,
                   LibvigAccessExpressionArgument arg) {
    switch (type) {
    case LibvigAccessExpressionArgument::Type::READ: {
      read_arg.set_name(arg.get_name());
      read_arg.set_expr(arg.get_expr());
      read_arg.set_packet_dependencies_force(arg.get_packet_dependencies());
      break;
    };
    case LibvigAccessExpressionArgument::Type::WRITE: {
      write_arg.set_name(arg.get_name());
      write_arg.set_expr(arg.get_expr());
      write_arg.set_packet_dependencies_force(arg.get_packet_dependencies());
      break;
    };
    case LibvigAccessExpressionArgument::Type::RESULT: {
      result_arg.set_name(arg.get_name());
      result_arg.set_expr(arg.get_expr());
      result_arg.set_packet_dependencies_force(arg.get_packet_dependencies());
      break;
    };
    }
  }

  void reset() {
    id.first = false;
    src_device.first = false;
    dst_device.first = false;
  }

  void fill_exprs(const call_t &call) {
    if (op == NOP)
      return;

    assert(klee_interface &&
           "Filling expression without setting a klee interface");
    assert(call_path_filename != "" &&
           "Filling expression without setting call_path filename");

    auto solutions = klee_interface->evaluate_expr(
        get_arg_expr_from_call(call, obj.first), call_path_filename);
    assert(solutions.size() == 1);

    obj.second = solutions[0];

    read_arg.set_expr(call);
    write_arg.set_expr(call);
    result_arg.set_expr(call);

    if (!call.ret.isNull()) {
      klee::ExprBuilder *exprBuilder = klee::createDefaultExprBuilder();
      auto ret_zero = exprBuilder->Eq(
          call.ret, exprBuilder->Constant(0, call.ret->getWidth()));
      auto ret_is_zero = klee_interface->evaluate_expr_must_be_true(
          ret_zero, call_path_filename);
      success = std::make_pair(true, !ret_is_zero);
    }
  }

  void search_packet_dependencies(const PacketManager &pm) {
    read_arg.set_packet_dependencies(pm);
    write_arg.set_packet_dependencies(pm);
    result_arg.set_packet_dependencies(pm);
  }

  void update_devices(const PacketManager &pm) {
    if (pm.is_src_device_set())
      set_src_device(pm.get_src_device());

    if (pm.is_dst_device_set())
      set_dst_device(pm.get_dst_device());

    read_arg.update_dependencies_devices(pm);
    write_arg.update_dependencies_devices(pm);
    result_arg.update_dependencies_devices(pm);
  }

  void set_call_path_filename(const std::string &_call_path_filename) {
    call_path_filename = _call_path_filename;
  }

  void set_klee_interface(std::shared_ptr<KleeInterface> _klee_interface) {
    klee_interface = _klee_interface;
  }

  void set_id(unsigned int _id) { id = std::make_pair(true, _id); }

  unsigned int get_id() const {
    assert(id.first && "Trying to get unset id");
    return id.second;
  }

  const std::string &get_call_path_filename() const {
    return call_path_filename;
  }

  const std::string &get_interface() const { return interface; }

  bool has_write_arg() const {
    return write_arg.is_name_set() && write_arg.is_expr_set();
  }

  const LibvigAccessExpressionArgument &get_write_argument() const {
    assert(write_arg.is_name_set() && write_arg.is_expr_set());
    return write_arg;
  }

  bool has_result_arg() const {
    return result_arg.is_name_set() && result_arg.is_expr_set();
  }

  const LibvigAccessExpressionArgument &get_result_argument() const {
    assert(result_arg.is_name_set() && result_arg.is_expr_set());
    return result_arg;
  }

  bool has_read_arg() const {
    return read_arg.is_name_set() && read_arg.is_expr_set();
  }

  const LibvigAccessExpressionArgument &get_read_argument() const {
    assert(read_arg.is_name_set() && read_arg.is_expr_set());
    return read_arg;
  }

  const std::pair<std::string, unsigned int> &get_obj() const { return obj; }

  void print() const {
    if (op == NOP || (!src_device.first && op == INIT))
      return;
    assert(src_device.first && "Unset source device");

    std::cerr << "\n";
    std::cerr << "========================================"
              << "\n";
    std::cerr << "Access " << get_id() << "\n";
    std::cerr << "  file         " << call_path_filename << "\n";
    std::cerr << "  src device   " << src_device.second << "\n";

    if (dst_device.first)
      std::cerr << "  dst device   " << dst_device.second << "\n";
    std::cerr << "  interface    " << interface << "\n";

    std::cerr << "  operation    ";
    switch (op) {
    case NOP:
      std::cerr << "NOP"
                << "\n";
      break;
    case INIT:
      std::cerr << "INIT"
                << "\n";
      break;
    case CREATE:
      std::cerr << "CREATE"
                << "\n";
      break;
    case VERIFY:
      std::cerr << "VERIFY"
                << "\n";
      break;
    case UPDATE:
      std::cerr << "UPDATE"
                << "\n";
      break;
    case DESTROY:
      std::cerr << "DESTROY"
                << "\n";
      break;
    case READ:
      std::cerr << "READ"
                << "\n";
      break;
    case WRITE:
      std::cerr << "WRITE"
                << "\n";
      break;
    default:
      assert(false && "Unknown operation");
    }

    std::cerr << "  object       " << obj.second << "\n";

    if (read_arg.is_name_set()) {
      std::cerr << "  read         "
                << kutil::expr_to_string(read_arg.get_expr()) << "\n";

      if (read_arg.has_packet_dependencies()) {
        std::cerr << "\n";
        read_arg.get_packet_dependencies().print();
      }
    }

    if (write_arg.is_name_set()) {
      std::cerr << "  write        "
                << kutil::expr_to_string(write_arg.get_expr()) << "\n";

      if (write_arg.has_packet_dependencies()) {
        std::cerr << "  packet dep   "
                  << "\n";
        write_arg.get_packet_dependencies().print();
      }
    }

    if (result_arg.is_name_set()) {
      std::cerr << "  result       "
                << kutil::expr_to_string(result_arg.get_expr()) << "\n";

      if (result_arg.has_packet_dependencies()) {
        std::cerr << "  packet dep   "
                  << "\n";
        result_arg.get_packet_dependencies().print();
      }
    }

    if (success.first) {
      std::cerr << "  success      " << success.second << "\n";
    }

    std::cerr << "========================================"
              << "\n";
  }

  void report() const {
    if (op == NOP || (!src_device.first && op == INIT))
      return;
    assert(src_device.first && "Unset source device");

    std::cout << "BEGIN ACCESS"
              << "\n";

    std::cout << "id " << get_id() << "\n";
    std::cout << "src_device " << src_device.second << "\n";

    if (dst_device.first) {
      std::cout << "dst_device? ";
      std::cout << dst_device.second;
      std::cout << "\n";
    }

    if (success.first) {
      std::cout << "success? ";
      std::cout << success.second;
      std::cout << "\n";
    }

    std::cout << "operation ";
    switch (op) {
    case NOP:
      std::cout << "NOP";
      break;
    case INIT:
      std::cout << "INIT";
      break;
    case CREATE:
      std::cout << "CREATE";
      break;
    case VERIFY:
      std::cout << "VERIFY";
      break;
    case UPDATE:
      std::cout << "UPDATE";
      break;
    case DESTROY:
      std::cout << "DESTROY";
      break;
    case READ:
      std::cout << "READ";
      break;
    case WRITE:
      std::cout << "WRITE";
      break;
    default:
      assert(false && "Unknown operation");
    }
    std::cout << "\n";

    std::cout << "object " << obj.second << "\n";

    read_arg.report(id.second);
    write_arg.report(id.second);
    result_arg.report(id.second);

    std::cout << "BEGIN METADATA"
              << "\n";
    std::cout << "interface " << interface << "\n";
    std::cout << "file " << call_path_filename << "\n";
    std::cout << "END METADATA"
              << "\n";

    std::cout << "END ACCESS"
              << "\n";
  }
};

class LibvigAccessesManager {
private:
  std::map<const std::string, LibvigAccess> access_lookup_table;
  std::shared_ptr<KleeInterface> klee_interface;
  std::vector<LibvigAccess> accesses;

  std::map<std::string, PacketManager> packet_manager_per_call_path;

  void add_access_lookup_table(const LibvigAccess &access) {
    access_lookup_table.emplace(std::make_pair(access.get_interface(), access));
  }

  void fill_access_lookup_table() {
    add_access_lookup_table(
        LibvigAccess("map_allocate", "map_out", LibvigAccess::INIT));
    add_access_lookup_table(
        LibvigAccess("map_get", "map", "key", "value_out", LibvigAccess::READ));
    add_access_lookup_table(
        LibvigAccess("map_put", "map", "key", "value", LibvigAccess::WRITE));
    add_access_lookup_table(
        LibvigAccess("map_erase", "map", "key", LibvigAccess::DESTROY));

    add_access_lookup_table(
        LibvigAccess("dmap_allocate", "dmap_out", LibvigAccess::INIT));
    add_access_lookup_table(
        LibvigAccess("dmap_get_a", "dmap", "key", "index", LibvigAccess::READ));
    add_access_lookup_table(
        LibvigAccess("dmap_get_b", "dmap", "key", "index", LibvigAccess::READ));
    add_access_lookup_table(LibvigAccess("dmap_put", "dmap", "index", "value",
                                         LibvigAccess::WRITE));

    add_access_lookup_table(
        LibvigAccess("dmap_erase", "dmap", "index", LibvigAccess::DESTROY));
    add_access_lookup_table(LibvigAccess("dmap_get_value", "dmap", "index",
                                         "value_out", LibvigAccess::READ));

    add_access_lookup_table(
        LibvigAccess("vector_allocate", "vector_out", LibvigAccess::INIT));
    add_access_lookup_table(LibvigAccess("vector_borrow", "vector", "index",
                                         "borrowed_cell", LibvigAccess::READ));
    add_access_lookup_table(LibvigAccess("vector_return", "vector", "index",
                                         "value", LibvigAccess::WRITE));

    add_access_lookup_table(
        LibvigAccess("dchain_allocate", "chain_out", LibvigAccess::INIT));
    add_access_lookup_table(LibvigAccess("dchain_allocate_new_index", "chain",
                                         "index_out", LibvigAccess::CREATE));
    add_access_lookup_table(LibvigAccess("dchain_rejuvenate_index", "chain",
                                         "index", LibvigAccess::UPDATE));
    add_access_lookup_table(LibvigAccess("dchain_is_index_allocated", "chain",
                                         "index", LibvigAccess::VERIFY));
    add_access_lookup_table(LibvigAccess("dchain_free_index", "chain", "index",
                                         LibvigAccess::DESTROY));

    add_access_lookup_table(
        LibvigAccess("sketch_allocate", "sketch_out", LibvigAccess::INIT));
    add_access_lookup_table(LibvigAccess("sketch_compute_hashes", "sketch",
                                         "key", "", LibvigAccess::WRITE));
    add_access_lookup_table(LibvigAccess("sketch_refresh"));
    add_access_lookup_table(LibvigAccess("sketch_fetch"));
    add_access_lookup_table(LibvigAccess("sketch_touch_buckets"));
    add_access_lookup_table(LibvigAccess("sketch_expire"));

    add_access_lookup_table(LibvigAccess("start_time"));
    add_access_lookup_table(LibvigAccess("restart_time"));
    add_access_lookup_table(LibvigAccess("current_time"));
    add_access_lookup_table(LibvigAccess("rte_lcore_count"));

    add_access_lookup_table(LibvigAccess("sketch_hash"));
    add_access_lookup_table(LibvigAccess("rte_ether_addr_hash"));

    add_access_lookup_table(LibvigAccess("cht_fill_cht"));
    add_access_lookup_table(LibvigAccess("cht_find_preferred_available_backend",
                                         "cht", "hash", "chosen_backend",
                                         LibvigAccess::WRITE));

    add_access_lookup_table(LibvigAccess("loop_invariant_consume"));
    add_access_lookup_table(LibvigAccess("loop_invariant_produce"));

    add_access_lookup_table(LibvigAccess("expire_items"));
    add_access_lookup_table(LibvigAccess("expire_items_single_map"));
    add_access_lookup_table(LibvigAccess("expire_items_single_map_offseted"));
    add_access_lookup_table(
        LibvigAccess("expire_items_single_map_iteratively"));

    add_access_lookup_table(LibvigAccess("nf_set_rte_ipv4_udptcp_checksum"));

    add_access_lookup_table(LibvigAccess("LoadBalancedFlow_hash"));
  }

public:
  LibvigAccessesManager() {
    fill_access_lookup_table();
    klee_interface = std::make_shared<KleeInterface>();
  }

  void set_accesses(std::vector<LibvigAccess> _accesses) {
    accesses = _accesses;
  }

  void analyse_call_path(const std::string &call_path_filename,
                         const call_path_t *call_path) {
    klee_interface->add_constraints(call_path_filename, call_path->constraints);

    PacketManager pm(klee_interface, call_path_filename);
    std::vector<LibvigAccess> call_path_accesses;

    for (const auto &call : call_path->calls) {

      if (pm.process_packet_call(call))
        continue;

      auto found_access_it = access_lookup_table.find(call.function_name);

      if (found_access_it == access_lookup_table.end()) {
        std::cerr << RED;
        std::cerr << "Unexpected function call"
                  << "\n";
        std::cerr << "  file:     " << call_path_filename << "\n";
        std::cerr << "  function: " << call.function_name << "\n";
        std::cerr << RESET;
        assert(false && "Unexpected function call");
      }

      auto access = found_access_it->second;

      access.set_klee_interface(klee_interface);
      access.set_call_path_filename(call_path_filename);
      access.set_id(accesses.size() + call_path_accesses.size());

      access.fill_exprs(call);
      access.search_packet_dependencies(pm);

      call_path_accesses.emplace_back(access);
      access.reset();
    }

    for (auto &access : call_path_accesses) {
      access.update_devices(pm);
    }

    accesses.insert(accesses.begin(), call_path_accesses.begin(),
                    call_path_accesses.end());
    packet_manager_per_call_path.emplace(
        std::make_pair(call_path_filename, pm));
  }

  void print() const {
    for (const auto &access : accesses)
      access.print();
  }

  void report() const {
    for (const auto &access : accesses)
      access.report();
  }

  std::vector<LibvigAccess> &get_accesses() { return accesses; }

  const PacketManager &get_packet_manager(const std::string &call_path) const {
    if (packet_manager_per_call_path.find(call_path) ==
        packet_manager_per_call_path.end()) {
      assert(false && "No packet manager associated with this call path");
    }

    return packet_manager_per_call_path.at(call_path);
  }

  std::vector<PacketManager> get_packet_managers() const {
    std::vector<PacketManager> packet_managers;
    for (const auto &packet_manager_call_path_pair :
         packet_manager_per_call_path)
      packet_managers.push_back(packet_manager_call_path_pair.second);
    return packet_managers;
  }
};

struct CallPathConstraint {
  std::vector<std::string> call_path_filenames;
  std::string write_interface;
  std::string chunks_connector;
  klee::expr::ExprHandle constraint;

  CallPathConstraint(const std::string &_call_path_filename,
                     const std::string &_write_interface,
                     const std::string &_chunks_connector,
                     const klee::expr::ExprHandle &_constraint)
      : write_interface(_write_interface), chunks_connector(_chunks_connector),
        constraint(_constraint) {
    call_path_filenames.push_back(_call_path_filename);
  }

  bool expr_has_connector(klee::expr::ExprHandle expr) const {
    auto expr_str = kutil::expr_to_string(expr);
    return expr_str.find(chunks_connector) != std::string::npos;
  }
};

struct ConstraintBetweenCallPaths {
  klee::expr::ExprHandle expression;

  std::string source_chunk_name;
  std::string pair_chunk_name;

  unsigned int source_chunk_id;
  unsigned int pair_chunk_id;

  std::string source_call_path_filename;
  std::string pair_call_path_filename;

  PacketManager source_call_path_packet_manager;
  PacketManager pair_call_path_packet_manager;

  RenameChunks renamer_source;

  ConstraintBetweenCallPaths(
      const klee::expr::ExprHandle &_expression,
      const std::string &_source_chunk_name,
      const std::string &_pair_chunk_name, unsigned int _source_chunk_id,
      unsigned int _pair_chunk_id,
      const std::string &_source_call_path_filename,
      const std::string &_pair_call_path_filename,
      const PacketManager &_source_call_path_packet_manager,
      const PacketManager &_pair_call_path_packet_manager)
      : source_chunk_name(_source_chunk_name),
        pair_chunk_name(_pair_chunk_name), source_chunk_id(_source_chunk_id),
        pair_chunk_id(_pair_chunk_id),
        source_call_path_filename(_source_call_path_filename),
        pair_call_path_filename(_pair_call_path_filename),
        source_call_path_packet_manager(_source_call_path_packet_manager),
        pair_call_path_packet_manager(_pair_call_path_packet_manager) {

    renamer_source.set_name_swapper("packet_chunks", _source_chunk_name);
    expression = renamer_source.visit(_expression);
  }

  void print() const {

    std::cerr << "\n";
    std::cerr << CYAN;
    std::cerr << "========================================"
              << "\n";
    std::cerr << "Constraint between call paths"
              << "\n";
    std::cerr << "  source     " << source_call_path_filename << "\n";
    std::cerr << "  pair      " << pair_call_path_filename << "\n";

    std::cerr << "  constraint " << kutil::expr_to_string(expression) << "\n";

    std::cerr << "  source dependencies"
              << "\n";
    source_call_path_packet_manager.print();

    std::cerr << "  pair dependencies"
              << "\n";
    pair_call_path_packet_manager.print();

    std::cerr << "========================================"
              << "\n";
    std::cerr << RESET;
  }

  void report() const {
    const auto &klee_interface =
        source_call_path_packet_manager.get_klee_interface();

    std::cout << "BEGIN CALL PATHS CONSTRAINT"
              << "\n";

    std::cout << "BEGIN EXPRESSION"
              << "\n";
    std::cout << klee_interface->expr_to_smt(expression);
    std::cout << "END EXPRESSION"
              << "\n";

    std::cout << "BEGIN CALL PATH INFO"
              << "\n";
    std::cout << "call_path " << source_call_path_filename << "\n";
    std::cout << "type "
              << "source"
              << "\n";
    std::cout << "id " << source_chunk_id << "\n";
    source_call_path_packet_manager.report();
    std::cout << "END CALL PATH INFO"
              << "\n";

    std::cout << "BEGIN CALL PATH INFO"
              << "\n";
    std::cout << "call_path " << pair_call_path_filename << "\n";
    std::cout << "type "
              << "pair"
              << "\n";
    std::cout << "id " << pair_chunk_id << "\n";
    pair_call_path_packet_manager.report();
    std::cout << "END CALL PATH INFO"
              << "\n";

    std::cout << "END CALL PATHS CONSTRAINT"
              << "\n";
  }
};

class ConstraintsAnalyser {
private:
  klee::ExprBuilder *builder = klee::createDefaultExprBuilder();
  LibvigAccessesManager accesses_manager;

  RenameChunks renameChunks = RenameChunks(-1);

private:
  std::vector<CallPathConstraint> packet_constraints;

  std::vector<std::shared_ptr<ConstraintBetweenCallPaths>>
      generated_constraints_between_call_paths;
  std::map<std::string, klee::ConstraintManager> constraints_per_call_path;

  KleeInterface build_klee_interface_between_call_paths(
      const CallPathConstraint &call_path_constraint,
      const klee::expr::ExprHandle &write_expr) {

    klee::ConstraintManager shared_constraints;
    shared_constraints.addConstraint(call_path_constraint.constraint);

    RetrieveReadsOnSymbol retrieveReadsOnVectorDataResetSymbol(
        call_path_constraint.chunks_connector);
    retrieveReadsOnVectorDataResetSymbol.visit(call_path_constraint.constraint);

    for (const auto &read :
         retrieveReadsOnVectorDataResetSymbol.get_retrieved()) {
      auto new_constraint =
          builder->Eq(read.expr, builder->Extract(write_expr, read.index * 8,
                                                  klee::Expr::Int8));
      shared_constraints.addConstraint(new_constraint);
    }

    KleeInterface klee_interface;
    klee_interface.add_constraints("merged", shared_constraints);

    return klee_interface;
  }

  void retrieve_packet_constraints_between_call_paths(
      const CallPathConstraint &call_path_constraint,
      LibvigAccess &write_access, unsigned int constraint_id) {
    auto write_data = write_access.get_write_argument();

    if (call_path_constraint.expr_has_connector(write_data.get_expr()))
      return;

    renameChunks.set_counter(write_access.get_id());
    auto write_expr = renameChunks.visit(write_data.get_expr());

    RetrieveReadsOnSymbol retrieveReadsOnConstraint("packet_chunks");
    retrieveReadsOnConstraint.visit(call_path_constraint.constraint);
    auto constraint_reads = retrieveReadsOnConstraint.get_retrieved();

    RetrieveUniqueSymbolsNames retrieve_unique_symbols_names;
    retrieve_unique_symbols_names.visit(write_expr);

    auto symbol_names = retrieve_unique_symbols_names.get_retrieved();
    assert(symbol_names.size() == 1);
    auto packet_chunks_access_name = symbol_names[0];

    RetrieveReadsOnSymbol retrieveReadsOnAccess(packet_chunks_access_name);
    retrieveReadsOnAccess.visit(write_expr);
    auto access_reads = retrieveReadsOnAccess.get_retrieved();

    auto klee_interface = build_klee_interface_between_call_paths(
        call_path_constraint, write_expr);

    klee::expr::ExprHandle final_constraint;

    for (const auto &constraint_read : constraint_reads) {
      for (const auto &access_read : access_reads) {
        auto equal = builder->Eq(constraint_read.expr, access_read.expr);
        auto are_equal =
            klee_interface.evaluate_expr_must_be_true(equal, "merged");
        auto are_not_equal =
            klee_interface.evaluate_expr_must_be_false(equal, "merged");

        if (are_equal) {
          if (final_constraint.isNull()) {
            final_constraint = equal;
          } else {
            final_constraint = builder->And(final_constraint, equal);
          }
        }

        if (are_not_equal) {
          assert(false && "What should I do?");
        }
      }
    }

    if (final_constraint.isNull())
      return;

    PacketManager pm_constraint, pm_access;

    {
      RenameChunks eraser;
      eraser.set_name_swapper(packet_chunks_access_name, "_");
      auto packet_chunks_eraser = eraser.visit(final_constraint);

      pm_constraint = accesses_manager.get_packet_manager(
          call_path_constraint.call_path_filenames[0]);

      std::vector<unsigned int> bytes_read;
      const auto &klee_interface = pm_constraint.get_klee_interface();

      if (klee_interface->has_packet(
              packet_chunks_eraser, bytes_read,
              call_path_constraint.call_path_filenames[0])) {
        pm_constraint.add_dependencies(bytes_read);
      }
    }

    {
      RenameChunks eraser;
      eraser.set_name_swapper("packet_chunks", "_");
      auto packet_chunks_eraser = eraser.visit(final_constraint);

      RenameChunks packet_chunks;
      packet_chunks.set_name_swapper(packet_chunks_access_name,
                                     "packet_chunks");
      auto all_good = packet_chunks.visit(packet_chunks_eraser);

      pm_access = accesses_manager.get_packet_manager(
          write_access.get_call_path_filename());

      std::vector<unsigned int> bytes_read;
      const auto &klee_interface = pm_access.get_klee_interface();

      if (klee_interface->has_packet(all_good, bytes_read,
                                     write_access.get_call_path_filename())) {
        pm_access.add_dependencies(bytes_read);
      }
    }

    std::string constraint_packet_symbol = "packet_chunks";
    constraint_packet_symbol += RenameChunks::marker_signature;
    constraint_packet_symbol += std::to_string(constraint_id);

    generated_constraints_between_call_paths.push_back(
        std::shared_ptr<ConstraintBetweenCallPaths>(
            new ConstraintBetweenCallPaths(
                final_constraint, constraint_packet_symbol,
                packet_chunks_access_name, constraint_id, write_access.get_id(),
                call_path_constraint.call_path_filenames[0],
                write_access.get_call_path_filename(), pm_constraint,
                pm_access)));
  }

public:
  void set_accesses_manager(const LibvigAccessesManager &_accesses_manager) {
    accesses_manager = _accesses_manager;
  }

  void store_libvig_packet_constraints(std::string call_path_filename,
                                       call_path_t *call_path) {
    if (constraints_per_call_path.find(call_path_filename) ==
        constraints_per_call_path.end()) {
      constraints_per_call_path[call_path_filename] = call_path->constraints;
    }

    for (const klee::expr::ExprHandle &constraint : call_path->constraints) {
      if (kutil::expr_to_string(constraint).find("packet_chunks") ==
          std::string::npos)
        continue;

      if (kutil::expr_to_string(constraint).find("vector_data_reset") ==
          std::string::npos)
        continue;

      auto found = std::find_if(
          packet_constraints.begin(), packet_constraints.end(),
          [&](const CallPathConstraint &call_path_constraint) {
            return call_path_constraint.constraint.compare(constraint) == 0;
          });

      if (found == packet_constraints.end()) {
        packet_constraints.emplace_back(call_path_filename, "vector_return",
                                        "vector_data_reset", constraint);
      } else {
        found->call_path_filenames.push_back(call_path_filename);
      }
    }
  }

  void analyse_constraints() {
    unsigned int counter = 0;
    for (const auto &packet_constraint : packet_constraints) {
      for (auto &access : accesses_manager.get_accesses()) {
        if (access.get_interface() == packet_constraint.write_interface) {
          const auto constraint_id =
              accesses_manager.get_accesses().size() + counter;
          retrieve_packet_constraints_between_call_paths(packet_constraint,
                                                         access, constraint_id);
          counter++;
        }
      }
    }
  }

  void print() const {
    for (const auto &generated : generated_constraints_between_call_paths)
      generated->print();
  }

  void report() const {
    for (const auto &generated : generated_constraints_between_call_paths)
      generated->report();
  }
};

// replace symbols being used as alias for results of another accesses
class AccessesStitcher {
private:
  klee::ExprBuilder *builder = klee::createDefaultExprBuilder();
  LibvigAccessesManager *accesses_manager;

  std::map<std::string, std::vector<LibvigAccessExpressionArgument>>
      alias_replacements;
  std::vector<std::string> alias_list;

public:
  AccessesStitcher() {
    alias_list = std::vector<std::string>{"vector_data_reset"};
  }

  void set_accesses_manager(LibvigAccessesManager *_accesses_manager) {
    accesses_manager = _accesses_manager;
  }

  void stitch_accesses() {
    std::vector<LibvigAccess> current_accesses =
        accesses_manager->get_accesses();
    std::vector<LibvigAccess> new_accesses;

    unsigned int id = current_accesses.size();

    for (auto access : current_accesses) {
      if (access.has_read_arg() &&
          get_alias(access.get_read_argument().get_expr()).first) {
        auto replacements = get_replacements(access.get_read_argument());

        for (auto rep : replacements) {
          auto new_access = access;

          new_access.set_id(id++);
          new_access.replace_arg(LibvigAccessExpressionArgument::Type::READ,
                                 rep);

          new_accesses.push_back(new_access);
        }

        continue;
      }

      new_accesses.push_back(access);
    }

    accesses_manager->set_accesses(new_accesses);
  }

private:
  std::pair<bool, std::vector<std::string>>
  get_alias(klee::expr::ExprHandle expr) {
    std::pair<bool, std::vector<std::string>> result;
    result.first = false;

    RetrieveUniqueSymbolsNames retriever;
    retriever.visit(expr);
    auto symbols = retriever.get_retrieved();

    for (auto symbol : symbols) {
      for (auto alias : alias_list) {
        if (symbol.find(alias) != std::string::npos) {
          result.first = true;
          result.second.push_back(symbol);
          continue;
        }
      }
    }

    return result;
  }

  std::vector<LibvigAccessExpressionArgument>
  get_replacements(LibvigAccessExpressionArgument arg) {
    std::vector<LibvigAccessExpressionArgument> replacements;
    auto expr = arg.get_expr();
    auto found_alias_pair = get_alias(expr);

    if (!found_alias_pair.first) {
      return replacements;
    }

    auto found_alias = found_alias_pair.second;
    for (auto alias : found_alias) {
      auto alias_expr_pair = alias_replacements.find(alias);

      if (alias_expr_pair == alias_replacements.end()) {
        find_alias_replacement(alias);
      }

      replacements.insert(replacements.end(), alias_replacements[alias].begin(),
                          alias_replacements[alias].end());

      for (auto replacement : replacements)
        assert(expr->getWidth() == replacement.get_expr()->getWidth());
    }

    return replacements;
  }

  unsigned int find_obj(std::string alias) {
    for (auto &access : accesses_manager->get_accesses()) {
      if (!access.has_result_arg()) {
        continue;
      }

      access.print();

      RetrieveUniqueSymbolsNames retriever;
      retriever.visit(access.get_result_argument().get_expr());
      auto symbols = retriever.get_retrieved();

      auto found_symbol = std::find(symbols.begin(), symbols.end(), alias);

      if (found_symbol != symbols.end()) {
        continue;
      }

      return access.get_obj().second;
    }

    assert(false);
    return 0;
  }

  void find_alias_replacement(std::string alias) {
    std::vector<LibvigAccessExpressionArgument> replacements;
    unsigned int obj = find_obj(alias);

    for (auto &access : accesses_manager->get_accesses()) {
      if (access.get_obj().second != obj) {
        continue;
      }

      if (!access.has_write_arg()) {
        continue;
      }

      RetrieveUniqueSymbolsNames retriever;
      retriever.visit(access.get_write_argument().get_expr());
      auto symbols = retriever.get_retrieved();

      auto found_symbol = std::find(symbols.begin(), symbols.end(), alias);

      if (found_symbol != symbols.end()) {
        continue;
      }

      auto is_stored = [&](klee::expr::ExprHandle expr) -> bool {
        for (auto inserted : replacements) {
          // hack
          auto replacement_expr = inserted.get_expr();
          if (kutil::expr_to_string(replacement_expr) ==
              kutil::expr_to_string(expr)) {
            return true;
          }
        }
        return false;
      };

      auto expr = access.get_write_argument().get_expr();
      if (!is_stored(expr)) {
        replacements.push_back(access.get_write_argument());
      }
    }

    assert(replacements.size());
    alias_replacements[alias] = replacements;
  }
};

int main(int argc, char **argv) {
  llvm::cl::ParseCommandLineOptions(argc, argv);

  LibvigAccessesManager libvig_manager;
  ConstraintsAnalyser constraints_analyser;
  AccessesStitcher accesses_stitcher;

  for (auto file : InputCallPathFiles) {
    std::cerr << "Loading: " << file << std::endl;

    call_path_t *call_path = load_call_path(file);

    libvig_manager.analyse_call_path(file, call_path);
    constraints_analyser.store_libvig_packet_constraints(file, call_path);
  }

  constraints_analyser.set_accesses_manager(libvig_manager);
  constraints_analyser.analyse_constraints();

  accesses_stitcher.set_accesses_manager(&libvig_manager);
  accesses_stitcher.stitch_accesses();

  libvig_manager.print();
  constraints_analyser.print();

  libvig_manager.report();
  constraints_analyser.report();

  return 0;
}
