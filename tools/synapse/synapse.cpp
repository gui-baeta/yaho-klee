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
#include <string>
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
           clEnumValN(TargetType::FPGA, "fpga", "FPGA (veriLog)"),
           clEnumValN(TargetType::Netronome, "netronome", "Netronome (uC)"),
           clEnumValN(TargetType::Tofino, "tofino", "Tofino (P4)"),
           clEnumValN(TargetType::x86_Tofino, "x86-tofino",
                      "Tofino ctrl (C++)"),
           clEnumValN(TargetType::tfhe, "tfhe", "TFHE-rs (Rust)"),
           clEnumValN(TargetType::x86, "x86", "x86 (DPDK C)"), clEnumValEnd),
    cat(SyNAPSE));

llvm::cl::opt<std::string> InputBDDFile(
    "in", desc("Input file for BDD deserialization."), cat(SyNAPSE));

llvm::cl::opt<std::string> Out(
    "out", desc("Output directory for every generated file."), cat(SyNAPSE));

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
}  // namespace

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

struct gen_data_t {
    int chunk_values_amount;
    std::vector<ep_node_id_t> visited_ep_nodes;
    std::vector<std::string> conditions;
};

std::string synthesize_code_aux(const ExecutionPlanNode_ptr &ep_node,
                                gen_data_t &gen_data) {
    std::string code("");

    std::cout << "Module Type: " << ep_node->get_module()->get_name()
              << std::endl;

    // If the node has already been visited, return
    if (std::find(gen_data.visited_ep_nodes.begin(),
                  gen_data.visited_ep_nodes.end(),
                  ep_node->get_id()) != gen_data.visited_ep_nodes.end()) {
        return code;
    }

    if (ep_node->get_module()->get_type() ==
        synapse::Module::ModuleType::tfhe_CurrentTime) {
        // Do nothing
        gen_data.visited_ep_nodes.push_back(ep_node->get_id());
        code += synthesize_code_aux(ep_node->get_next()[0], gen_data);

    } else if (ep_node->get_module()->get_type() ==
               synapse::Module::ModuleType::tfhe_Conditional) {
        gen_data.visited_ep_nodes.push_back(ep_node->get_id());
        auto conditional_module =
            std::static_pointer_cast<synapse::targets::tfhe::Conditional>(
                ep_node->get_module());

        size_t this_cond_val_num = gen_data.conditions.size();
        bool first_conditional_in_conditional_nesting = this_cond_val_num == 0;

        gen_data.conditions.push_back(
            std::string("let cond_val") +
            std::to_string(gen_data.conditions.size()) + std::string(" = ") +
            conditional_module->generate_code() + std::string("\n"));

        // FIXME This code is hardcoded for flattened if-else statements
        // FIXME I don't like this solution, but for now it will do
        //  Better solution -> See if this node is a child of a Else or Then
        //  node?
        if (first_conditional_in_conditional_nesting) {
            code += std::string("let result = cond_val");
            //            for (int n_value = gen_data.chunk_values_amount - 1;
            //            n_value >= 0;
            //                 --n_value) {
            //                code += std::string("val") +
            //                std::to_string(n_value); if (n_value > 0) {
            //                    code += std::string(", ");
            //                }
            //            }
            //            code += std::string(" = ");
        } else {
            code += std::string("&cond_val");
        }
        code += std::to_string(this_cond_val_num) + std::string(".mul_distr(") +
                synthesize_code_aux(ep_node->get_next()[0], gen_data) +
                std::string(").add(cond_val") +
                std::to_string(this_cond_val_num) +
                std::string(".map(not).mul_distr(") +
                synthesize_code_aux(ep_node->get_next()[1], gen_data) +
                std::string("))\n");

    } else if (ep_node->get_module()->get_type() ==
               synapse::Module::ModuleType::tfhe_Then) {
        gen_data.visited_ep_nodes.push_back(ep_node->get_id());
        code += synthesize_code_aux(ep_node->get_next()[0], gen_data);

    } else if (ep_node->get_module()->get_type() ==
               synapse::Module::ModuleType::tfhe_Else) {
        gen_data.visited_ep_nodes.push_back(ep_node->get_id());
        code += synthesize_code_aux(ep_node->get_next()[0], gen_data);

    } else if (ep_node->get_module()->get_type() ==
               synapse::Module::ModuleType::tfhe_TernarySum) {
        gen_data.visited_ep_nodes.push_back(ep_node->get_id());
        auto ternary_sum_module =
            std::static_pointer_cast<synapse::targets::tfhe::TernarySum>(
                ep_node->get_module());
        code += ternary_sum_module->generate_code();

    } else if (ep_node->get_module()->get_type() ==
               synapse::Module::tfhe_PacketBorrowNextChunk) {
        gen_data.visited_ep_nodes.push_back(ep_node->get_id());
        auto packet_borrow_next_chunk_module = std::static_pointer_cast<
            synapse::targets::tfhe::PacketBorrowNextChunk>(
            ep_node->get_module());
        //        code += packet_borrow_next_chunk_module->generate_code();
        gen_data.chunk_values_amount =
            packet_borrow_next_chunk_module->get_chunk_values_amount();

        code += synthesize_code_aux(ep_node->get_next()[0], gen_data);

    } else if (ep_node->get_module()->get_type() ==
               synapse::Module::tfhe_PacketBorrowNextSecret) {
        gen_data.visited_ep_nodes.push_back(ep_node->get_id());
        auto packet_borrow_next_secret_module = std::static_pointer_cast<
            synapse::targets::tfhe::PacketBorrowNextSecret>(
            ep_node->get_module());
        //        code += packet_borrow_next_secret_module->generate_code();
        gen_data.chunk_values_amount =
            packet_borrow_next_secret_module->get_chunk_values_amount();

        code += synthesize_code_aux(ep_node->get_next()[0], gen_data);
    }

    // FIXME This is probably too generic and I want to control each node that
    // is to be processed
    //    if (!ep_node->get_next().empty()) {
    //        gen_data.visited_ep_nodes.push_back(ep_node->get_id());
    //        code += synthesize_code_aux(ep_node->get_next()[0], gen_data);
    //    }

    return code;
}

/// \brief Preprocess the Execution Plan to extract metadata. Starts at the
/// first Branching node. \param ep_node \param value_conditions
void preprocess_aux(const ExecutionPlanNode_ptr &ep_node,
                    value_conditions_t &value_conditions) {
    std::cout << ep_node->get_module()->get_name() << std::endl;

    if (ep_node->get_module_type() == synapse::Module::tfhe_Conditional) {
        // Then EP node
        if (value_conditions.get_then_branch() == nullptr) {
            value_conditions.get_else_branch() =
                std::make_shared<value_conditions_t>(value_conditions_t());
        } else {
            std::cout << "(Then conditional arm on preprocessing) ERROR: This "
                         "should be null..."
                      << std::endl;
        }
        preprocess_aux(ep_node->get_next()[0],
                       *(value_conditions.get_then_branch()));

        // Else EP node
        if (value_conditions.get_else_branch() == nullptr) {
            value_conditions.get_else_branch() =
                std::make_shared<value_conditions_t>(value_conditions_t());
        } else {
            std::cout << "(Else conditional arm on preprocessing) ERROR: This "
                         "should be null..."
                      << std::endl;
        }
        preprocess_aux(ep_node->get_next()[1],
                       *(value_conditions.get_else_branch()));

        // Then EP node is just a wrapper around one of the two branch arms
    } else if (ep_node->get_module_type() == synapse::Module::tfhe_Then) {
        preprocess_aux(ep_node->get_next()[0], value_conditions);

        // Else EP node is just a wrapper around one of the two branch arms
    } else if (ep_node->get_module_type() == synapse::Module::tfhe_Else) {
        preprocess_aux(ep_node->get_next()[0], value_conditions);

        // Modification EP node
    } else if (ep_node->get_module_type() == synapse::Module::tfhe_TernarySum) {
        auto ternary_sum_module =
            std::static_pointer_cast<synapse::targets::tfhe::TernarySum>(
                ep_node->get_module());

        // TODO Should call here some kind of method that sees if there are
        //  actual modifications on this specific value
        std::cout << ternary_sum_module->generate_code() << std::endl;
    }

    if (ep_node->get_module()->get_type() == synapse::Module::tfhe_Drop) {
        std::cout << "Found the end/drop" << std::endl;
        return;
    }
}

// Preprocess the Execution Plan to extract metadata
std::vector<value_conditions_t> preprocess(
    const ExecutionPlanNode_ptr &ep_node) {
    // For each value,
    // create a value_conditions_t object,
    auto packet_borrow_secret_node = ep_node->find_node_by_module_type(
        synapse::Module::ModuleType::tfhe_PacketBorrowNextSecret);
    auto packet_borrow_secret_module = std::static_pointer_cast<
        synapse::targets::tfhe::PacketBorrowNextSecret>(
        packet_borrow_secret_node->get_module());

    int chunk_values_amount =
        packet_borrow_secret_module->get_chunk_values_amount();

    std::vector<value_conditions_t> value_conditions(chunk_values_amount);

    std::cout << "Chunk Values Amount: " << chunk_values_amount << std::endl;
    // TODO start by first figuring out how to see if the value is modified or
    // not for this return chunk leaf.
    //  I'm saying this because for a case like: ((1 + packet_chunks[2])) ++
    //  (packet_chunks) The first and second value is not modified, but the
    //  third is. So we need to figure out how to see this. Does the
    //  "modifications", by being a vector, imply this??
    //                                            Meaning, the vector elements
    //                                            and their indexes will
    //                                             tell me if this value is
    //                                             modified or not?
    preprocess_aux(packet_borrow_secret_node->get_next()[0],
                   value_conditions[0]);

    // and go through each condition for all of these objects/values.
    for (int n_value = 0; n_value < chunk_values_amount; ++n_value) {
        // For each condition,
        //      if it is a condition node,
        //          then go through the nodes on each branching
        //          (from right to left in the tree) and save the
        //          modifications on the right and left
        //          if they are modifications...
        //          If on the right is not a condition and the modification
        //          doesn't translate to any real modification, then the value
        //          is not modified, and we should maintain that right
        //          modification value as null in the metadata object; The same
        //          but for the left; If on the right is a condition, then we
        //          should go to the condition and repeat the process until we
        //          reach the end of the condition tree; The same but for the
        //          left;
        //      else if it is a modification node,
        //          then save the modification on the right and left
        //          if they are modifications...
        //          If on the right is not a condition and the modification
        //          doesn't translate to any real modification, then the value
        //          is not modified, and we should maintain that right
        //          modification value as null in the metadata object; The same
        //          but for the left; If on the right is a condition, then we
        //          should go to the condition and repeat the process until we
        //          reach the end of the condition tree; The same but for the
        //          left;
        //      else if it is a return node,
        //          then save the modification on the right and left
        //          if they are modifications...
        //          If on the right is not a condition and the modification
        //          doesn't translate to any real modification, then the value
        //          is not modified, and we should maintain that right
        //          modification value as null in the metadata object; The same
        //          but for the left; If on the right is a condition, then we
        //          should go to the condition and repeat the process until we
        //          reach the end of the condition tree; The same but for the
        //          left;
    }
    // -- Need to Save the node id of each condition --
    //      Go through the nodes on each branching
    //      (from right to left in the tree) and save the
    //      modifications on the right and left
    //      if they are modifications...
    //      If on the right is not a condition and the modification doesn't
    //      translate to any real modification, then the value is not modified,
    //      and we should maintain that right modification value as null in the
    //      metadata object; The same but for the left; If on the right is a
    //      condition, then we should go to the condition and repeat the process
    //      until we reach the end of the condition tree; The same but for the
    //      left;

    // At the end we should have a metadata object with all the modifications
    // for each value
    // We need to simplify the value_conditions_t object and
    // remove any unnecessary condition nodes
    // that don't modify the value.

    // After this simplification,
    // we can start producing the code for each value.
    // For this,
    // we need to go through the object
    // and get the condition klee Expression through the node id.
    //      Write the code for this condition
    //      and continue down the tree of this object to write the
    //      full chain of conditions
    //      and finally writing the actual modification.

    return value_conditions;
}

void synthesize_code(const ExecutionPlan &ep) {
    // Create file if not exists and open it with write permission
    std::ofstream myfile;
    myfile.open("main.rs");

    CodeGenerator code_generator(Out);
    // Assuming the only target is TFHE-rs
    assert(TargetList.size() == 1 && TargetList[0] == TargetType::tfhe);
    code_generator.add_target(TargetList[0]);
    ExecutionPlan extracted_ep = code_generator.extract_at(ep, 0);

    std::vector<value_conditions_t> value_conditions =
        preprocess(extracted_ep.get_root());

    gen_data_t gen_data;
    gen_data.chunk_values_amount = 0;
    std::string code;
    //    code = synthesize_code_aux(extracted_ep.get_root(), gen_data);
    code += std::string("\n");

    for (std::string condition : gen_data.conditions) {
        myfile << condition;
    }

    myfile << code;

    myfile << code;
    myfile.flush();
    std::cout << code << std::endl;

    myfile.close();
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
        synthesize_code(search_results.first);
        //    synthesize(search_results.first);
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
