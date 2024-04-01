#include "llvm/Support/CommandLine.h"

#include "bdd-visualizer.h"

namespace {
llvm::cl::OptionCategory BDDVisualizer("BDDVisualizer specific options");

llvm::cl::opt<std::string> InputBDDFile("in", llvm::cl::desc("BDD."),
                                        llvm::cl::Required,
                                        llvm::cl::cat(BDDVisualizer));

llvm::cl::opt<std::string>
    BDDAnalyzerReport("report", llvm::cl::desc("BDD analyzer report file."),
                      llvm::cl::Optional, llvm::cl::cat(BDDVisualizer));

llvm::cl::opt<std::string>
    OutputDot("out", llvm::cl::desc("Output graphviz dot file."),
              llvm::cl::cat(BDDVisualizer));

llvm::cl::opt<bool> Show("show", llvm::cl::desc("Render dot file."),
                         llvm::cl::ValueDisallowed, llvm::cl::init(false),
                         llvm::cl::cat(BDDVisualizer));

} // namespace

int main(int argc, char **argv) {
  llvm::cl::ParseCommandLineOptions(argc, argv);

  BDD::BDD bdd = BDD::BDD(InputBDDFile);

  if (BDDAnalyzerReport.size()) {
    auto report = parse_bdd_analyzer_report_t(BDDAnalyzerReport);

    if (OutputDot.size()) {
      std::ofstream ofs(OutputDot);
      BDD::HitRateGraphvizGenerator generator(ofs, report.counters);
      generator.visit(bdd);
    }

    if (Show) {
      BDD::HitRateGraphvizGenerator::visualize(bdd, report.counters, false);
    }
  } else {
    if (OutputDot.size()) {
      std::ofstream ofs(OutputDot);
      BDD::GraphvizGenerator generator(ofs);
      generator.visit(bdd);
    }

    if (Show) {
      BDD::GraphvizGenerator::visualize(bdd, false);
    }
  }

  return 0;
}
