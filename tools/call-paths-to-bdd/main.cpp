#include "llvm/Support/CommandLine.h"
#include "llvm/Support/MemoryBuffer.h"

#include <fstream>

#include "call-paths-to-bdd.h"

namespace {
llvm::cl::list<std::string> InputCallPathFiles(llvm::cl::desc("<call paths>"),
                                               llvm::cl::Positional);

llvm::cl::OptionCategory BDDGeneratorCat("BDD generator specific options");

llvm::cl::opt<std::string>
    Gv("gv", llvm::cl::desc("GraphViz file for BDD visualization."),
       llvm::cl::cat(BDDGeneratorCat));

llvm::cl::opt<std::string>
    InputBDDFile("in", llvm::cl::desc("Input file for BDD deserialization."),
                 llvm::cl::cat(BDDGeneratorCat));

llvm::cl::opt<std::string>
    OutputBDDFile("out", llvm::cl::desc("Output file for BDD serialization."),
                  llvm::cl::cat(BDDGeneratorCat));

llvm::cl::opt<bool> Show("s", llvm::cl::desc("Render dot file."),
                         llvm::cl::ValueDisallowed, llvm::cl::init(false),
                         llvm::cl::cat(BDDGeneratorCat));
} // namespace

void assert_bdd(const BDD::BDD &bdd) {
  std::vector<const BDD::Node *> nodes;

  auto init = bdd.get_init();
  auto process = bdd.get_process();

  assert(init);
  assert(process);

  nodes.push_back(init.get());
  nodes.push_back(process.get());

  while (nodes.size()) {
    auto node = nodes[0];
    nodes.erase(nodes.begin());

    if (node->get_type() == BDD::Node::NodeType::CALL) {
      auto next = node->get_next();
      assert(next);

      auto next_prev = next->get_prev();
      assert(next_prev);
      assert(next_prev->get_id() == node->get_id());

      nodes.push_back(next.get());
    }

    else if (node->get_type() == BDD::Node::NodeType::BRANCH) {
      auto branch = static_cast<const BDD::Branch *>(node);

      auto on_true = branch->get_on_true();
      auto on_false = branch->get_on_false();

      assert(on_true);
      assert(on_false);

      auto on_true_prev = on_true->get_prev();
      auto on_false_prev = on_false->get_prev();

      assert(on_true_prev);
      assert(on_false_prev);

      assert(on_true_prev->get_id() == node->get_id());
      assert(on_false_prev->get_id() == node->get_id());

      nodes.push_back(on_true.get());
      nodes.push_back(on_false.get());
    }
  }
}

int main(int argc, char **argv) {
  llvm::cl::ParseCommandLineOptions(argc, argv);

  std::vector<call_path_t *> call_paths;

  for (auto file : InputCallPathFiles) {
    std::cerr << "Loading: " << file << std::endl;

    call_path_t *call_path = load_call_path(file);
    call_paths.push_back(call_path);
  }

  if (InputBDDFile.size() == 0 && InputCallPathFiles.size() == 0) {
    std::cerr << "No input files provided.\n";
    return 1;
  }

  auto bdd =
      InputBDDFile.size() ? BDD::BDD(InputBDDFile) : BDD::BDD(call_paths);

  std::cerr << "Asserting BDD...\n";
  assert_bdd(bdd);
  std::cerr << "OK!\n";

  BDD::PrinterDebug printer;
  bdd.visit(printer);

  if (OutputBDDFile.size()) {
    bdd.serialize(OutputBDDFile);
  }

  for (auto call_path : call_paths) {
    delete call_path;
  }

  return 0;
}
