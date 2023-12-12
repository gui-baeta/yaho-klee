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
#include "call-paths-to-bdd.h"
#include "load-call-paths.h"

namespace {
llvm::cl::list<std::string> InputCallPathFiles(llvm::cl::desc("<call paths>"),
                                               llvm::cl::Positional,
                                               llvm::cl::OneOrMore);
}

std::string network_layer_to_label(int layer, klee::ref<klee::Expr> length) {
  // we start with 2, but this layer actually starts from 0

  switch (layer) {
  case 0:
    return "ethernet";
  case 1:
    return "ip";
  case 2:
    if (length->getKind() == klee::Expr::Kind::Constant) {
      return "tcp/udp";
    }

    return "ip options";
  case 3:
    return "tcp/udp";
  default:
    assert(false && "Should not be here");
  }

  // shut up warnings
  return "";
}

struct chunk_t {
  klee::ref<klee::Expr> in;
  klee::ref<klee::Expr> out;
  klee::ConstraintManager constraints;
  std::string label;

  chunk_t(klee::ref<klee::Expr> _in, klee::ConstraintManager _constraints,
          const std::string &_label)
      : in(_in), constraints(_constraints), label(_label) {}

  bool was_chunk_modified() const {
    auto eq = kutil::solver_toolbox.exprBuilder->Eq(in, out);
    auto always_eq = kutil::solver_toolbox.is_expr_always_true(constraints, eq);
    return !always_eq;
  }

  std::vector<unsigned> get_modified_bytes() const {
    std::vector<unsigned> modified_bytes;

    assert(in->getWidth() == out->getWidth());
    auto width = in->getWidth(); // bits

    for (auto byte = 0u; byte < width / 8; byte++) {
      auto in_byte = kutil::solver_toolbox.exprBuilder->Extract(in, byte * 8,
                                                              klee::Expr::Int8);
      auto out_byte = kutil::solver_toolbox.exprBuilder->Extract(
          out, byte * 8, klee::Expr::Int8);
      auto eq_bytes = kutil::solver_toolbox.exprBuilder->Eq(in_byte, out_byte);
      auto always_eq =
          kutil::solver_toolbox.is_expr_always_true(constraints, eq_bytes);

      if (!always_eq) {
        modified_bytes.push_back(byte);
      }
    }

    return modified_bytes;
  }
};

struct call_path_chunks_t {
  std::vector<chunk_t> chunks;
  std::string call_path_filename;

  call_path_chunks_t(const std::vector<chunk_t> &_chunks,
                     const std::string &_call_path_filename)
      : chunks(_chunks), call_path_filename(_call_path_filename) {}
};

std::vector<call_path_chunks_t>
get_chunks_per_call_path(std::vector<call_path_t *> call_paths) {
  std::vector<call_path_chunks_t> call_paths_chunks;

  for (auto cp : call_paths) {
    std::vector<chunk_t> chunks;
    std::cerr << "filename " << cp->file_name << "\n";

    int layer = -1;
    for (auto call : cp->calls) {
      if (call.function_name == "packet_borrow_next_chunk" ||
          call.function_name == "packet_borrow_next_secret") {
        assert(call.extra_vars.find("the_chunk") != call.extra_vars.end());

        layer++;

        auto in = call.extra_vars["the_chunk"].second;
        auto constraints = cp->constraints;
        auto label = network_layer_to_label(layer, call.args["length"].expr);
        auto call_path_filename = cp->file_name;

        chunks.emplace_back(in, constraints, label);
      } else if (call.function_name == "packet_return_chunk") {
        assert(call.args.find("the_chunk") != call.args.end());

        auto out = call.args["the_chunk"].in;
        auto constraints = cp->constraints;

        assert(layer >= 0 && static_cast<size_t>(layer) < chunks.size());
        chunks[layer].out = out;

        layer--;
      }
    }

    auto call_path_filename = cp->file_name;
    call_paths_chunks.emplace_back(chunks, call_path_filename);
  }

  return call_paths_chunks;
}

int main(int argc, char **argv) {
  llvm::cl::ParseCommandLineOptions(argc, argv);

  kutil::solver_toolbox.build();
  std::vector<call_path_t *> call_paths;

  for (auto file : InputCallPathFiles) {
    std::cerr << "Loading: " << file << std::endl;

    auto call_path = load_call_path(file);
    call_paths.push_back(call_path);
  }

  auto call_paths_chunks = get_chunks_per_call_path(call_paths);

  for (auto call_path_chunks : call_paths_chunks) {
    std::cerr << "\n";
    std::cerr << "**********************************************************\n";
    std::cerr << "call path: " << call_path_chunks.call_path_filename << "\n";

    for (auto chunk : call_path_chunks.chunks) {
      std::cerr << "\n";
      std::cerr << "  label    : " << chunk.label << "\n";
      std::cerr << "  in       : " << kutil::expr_to_string(chunk.in, true) << "\n";
      std::cerr << "  out      : " << kutil::expr_to_string(chunk.out, true) << "\n";

      std::cerr << "  modified : ";
      auto modified = chunk.was_chunk_modified();

      if (modified) {
        std::cerr << "bytes ";
        for (auto modified_byte : chunk.get_modified_bytes()) {
          std::cerr << modified_byte << " ";
        }
      } else {
        std::cerr << "no";
      }

      std::cerr << "\n";
    }

    std::cerr << "**********************************************************\n";
  }

  return 0;
}
