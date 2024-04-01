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

#include "bdd-reorderer.h"
#include "bdd-visualizer.h"

namespace {
llvm::cl::list<std::string> InputCallPathFiles(llvm::cl::desc("<call paths>"),
                                               llvm::cl::Positional);

llvm::cl::OptionCategory BDDReorderer("BDDReorderer specific options");

llvm::cl::opt<std::string>
    InputBDDFile("in", llvm::cl::desc("Input file for BDD deserialization."),
                 llvm::cl::cat(BDDReorderer));

llvm::cl::opt<int> MaxReorderingOperations(
    "max", llvm::cl::desc("Maximum number of reordering operations."),
    llvm::cl::initializer<int>(-1), llvm::cl::cat(BDDReorderer));

llvm::cl::opt<bool>
    Approximate("approximate",
                llvm::cl::desc("Get a lower bound approximation."),
                llvm::cl::ValueDisallowed, llvm::cl::init(false),
                llvm::cl::cat(BDDReorderer));

llvm::cl::opt<bool> Show("s", llvm::cl::desc("Show Execution Plans."),
                         llvm::cl::ValueDisallowed, llvm::cl::init(false),
                         llvm::cl::cat(BDDReorderer));

llvm::cl::opt<std::string> ReportFile("report",
                                      llvm::cl::desc("Output report file"),
                                      llvm::cl::cat(BDDReorderer));
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

void test(const BDD::BDD &bdd) {
  auto root = bdd.get_node_by_id(88);
  assert(root);

  auto reordered = BDD::reorder(bdd, root);
  std::cerr << "reordered " << reordered.size() << "\n";

  for (auto r : reordered) {
    BDD::GraphvizGenerator::visualize(r.bdd);
  }
}

int main(int argc, char **argv) {
  llvm::cl::ParseCommandLineOptions(argc, argv);

  auto start = std::chrono::steady_clock::now();
  auto original_bdd = build_bdd();

  // test(original_bdd);
  // return 0;

  if (Approximate) {
    auto approximation =
        BDD::approximate_number_of_reordered_bdds(original_bdd);
    auto end = std::chrono::steady_clock::now();
    auto elapsed = end - start;
    std::cerr << "\nApproximately " << approximation << " BDDs generated\n";
    std::cerr
        << "Elapsed: "
        << std::chrono::duration_cast<std::chrono::seconds>(elapsed).count()
        << " seconds\n";
    return 0;
  }

  auto reordered_bdds =
      BDD::get_all_reordered_bdds(original_bdd, MaxReorderingOperations);

  std::cerr << "\nFinal: " << reordered_bdds.size() << "\n";

  if (Show) {
    for (auto bdd : reordered_bdds) {
      BDD::GraphvizGenerator::visualize(bdd, true);
    }
  }

  if (ReportFile.size() == 0) {
    return 0;
  }

  auto end = std::chrono::steady_clock::now();
  auto elapsed = end - start;
  auto report = std::ofstream(ReportFile, std::ios::out);

  if (report.is_open()) {
    report << "# time (s) \t total\n";
    report << std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
    report << "\t";
    report << reordered_bdds.size();
    report << "\n";
    report.close();
  } else {
    std::cerr << "Unable to open report file " << ReportFile << "\n";
  }

  return 0;
}
