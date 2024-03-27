#include "klee/ExprBuilder.h"
#include "klee/perf-contracts.h"
#include "klee/util/ArrayCache.h"
#include "klee/util/ExprSMTLIBPrinter.h"
#include "klee/util/ExprVisitor.h"
#include "llvm/Support/CommandLine.h"

#include <klee/Constraints.h>
#include <klee/Solver.h>

#include <algorithm>
#include <chrono>
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

#include "call-paths-to-bdd.h"
#include "load-call-paths.h"

#include "code_generator.h"
#include "execution_plan/execution_plan.h"
#include "execution_plan/visitors/graphviz/graphviz.h"
#include "heuristics/heuristics.h"
#include "log.h"
#include "search.h"

using llvm::cl::cat;
using llvm::cl::desc;
using llvm::cl::OneOrMore;
using llvm::cl::Positional;
using llvm::cl::Required;
using llvm::cl::values;

using namespace synapse;

namespace {
llvm::cl::list<std::string> InputCallPathFiles(desc("<call paths>"),
                                               Positional);

llvm::cl::OptionCategory SyNAPSE("SyNAPSE specific options");

llvm::cl::list<TargetType> TargetList(
    desc("Available targets:"), Required, OneOrMore,
    values(clEnumValN(TargetType::x86_BMv2, "x86-bmv2", "BMv2 ctrl (C)"),
           clEnumValN(TargetType::BMv2, "bmv2", "BMv2 (P4)"),
           clEnumValN(TargetType::Tofino, "tofino", "Tofino (P4)"),
           clEnumValN(TargetType::x86_Tofino, "x86-tofino",
                      "Tofino ctrl (C++)"),
           clEnumValN(TargetType::x86, "x86", "x86 (DPDK C)"), clEnumValEnd),
    cat(SyNAPSE));

llvm::cl::opt<std::string>
    InputBDDFile("in", desc("Input file for BDD deserialization."),
                 cat(SyNAPSE));

llvm::cl::opt<std::string>
    Out("out", desc("Output directory for every generated file."),
        cat(SyNAPSE));

llvm::cl::opt<int> MaxReordered(
    "max-reordered",
    desc("Maximum number of reordenations on the BDD (-1 for unlimited)."),
    llvm::cl::Optional, llvm::cl::init(-1), cat(SyNAPSE));

llvm::cl::opt<bool> ShowEP("s", desc("Show winner Execution Plan."),
                           llvm::cl::ValueDisallowed, llvm::cl::init(false),
                           cat(SyNAPSE));

llvm::cl::opt<bool> ShowSS("ss", desc("Show the entire search space."),
                           llvm::cl::ValueDisallowed, llvm::cl::init(false),
                           cat(SyNAPSE));

llvm::cl::opt<int> Peek("peek",
                        desc("Peek search space at the given BDD node."),
                        llvm::cl::Optional, llvm::cl::init(-1), cat(SyNAPSE));

llvm::cl::opt<bool> Verbose("v", desc("Verbose mode."),
                            llvm::cl::ValueDisallowed, llvm::cl::init(false),
                            cat(SyNAPSE));
} // namespace

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

std::pair<ExecutionPlan, SearchSpace> search(const BDD::BDD &bdd,
                                             BDD::node_id_t peek) {
  SearchEngine search_engine(bdd, MaxReordered);

  for (unsigned i = 0; i != TargetList.size(); ++i) {
    auto target = TargetList[i];
    search_engine.add_target(target);
  }

  Biggest biggest;
  DFS dfs;
  MostCompact most_compact;
  LeastReordered least_reordered;
  MaximizeSwitchNodes maximize_switch_nodes;
  Gallium gallium;

  // auto winner = search_engine.search(biggest, peek);
  // auto winner = search_engine.search(least_reordered, peek);
  // auto winner = search_engine.search(dfs, peek);
  // auto winner = search_engine.search(most_compact, peek);
  // auto winner = search_engine.search(maximize_switch_nodes, peek);
  auto winner = search_engine.search(gallium, peek);
  const auto &ss = search_engine.get_search_space();

  return {winner, ss};
}

void synthesize(const ExecutionPlan &ep) {
  CodeGenerator code_generator(Out);

  for (unsigned i = 0; i != TargetList.size(); ++i) {
    auto target = TargetList[i];
    code_generator.add_target(target);
  }

  code_generator.generate(ep);
}

int main(int argc, char **argv) {
  llvm::cl::ParseCommandLineOptions(argc, argv);

  if (Verbose) {
    Log::MINIMUM_LOG_LEVEL = Log::Level::DEBUG;
  } else {
    Log::MINIMUM_LOG_LEVEL = Log::Level::LOG;
  }

  BDD::BDD bdd = build_bdd();

  auto start_search = std::chrono::steady_clock::now();
  auto search_results = search(bdd, Peek);
  auto end_search = std::chrono::steady_clock::now();

  auto search_dt = std::chrono::duration_cast<std::chrono::seconds>(
                       end_search - start_search)
                       .count();

  if (ShowEP) {
    Graphviz::visualize(search_results.first);
  }

  if (ShowSS) {
    Graphviz::visualize(search_results.second);
  }

  int64_t synthesis_dt = -1;

  if (Out.size()) {
    auto start_synthesis = std::chrono::steady_clock::now();
    synthesize(search_results.first);
    auto end_synthesis = std::chrono::steady_clock::now();

    synthesis_dt = std::chrono::duration_cast<std::chrono::seconds>(
                       end_synthesis - start_synthesis)
                       .count();
  }

  Log::log() << "Search time:     " << search_dt << " sec\n";

  if (synthesis_dt >= 0) {
    Log::log() << "Generation time: " << synthesis_dt << " sec\n";
  }

  return 0;
}
