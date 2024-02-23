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
    //  is to be processed
    //    if (!ep_node->get_next().empty()) {
    //        gen_data.visited_ep_nodes.push_back(ep_node->get_id());
    //        code += synthesize_code_aux(ep_node->get_next()[0], gen_data);
    //    }

    return code;
}

/// \brief Preprocess the Execution Plan to extract metadata. Starts at the
/// first Conditional node. \param ep_node \param value_conditions \param value
/// \param all_conditions \return void
void preprocess_aux(const ExecutionPlanNode_ptr &ep_node,
                    value_conditions_t &value_conditions, int value,
                    std::vector<ExecutionPlanNode_ptr> &all_conditions) {
    std::cout << ep_node->get_module()->get_name() << std::endl;

    if (ep_node->get_module_type() == synapse::Module::tfhe_Conditional ||
        ep_node->get_module_type() == synapse::Module::tfhe_TruthTablePBS) {
        // If condition wasn't added yet
        if (std::find_if(all_conditions.begin(), all_conditions.end(),
                         [ep_node](ExecutionPlanNode_ptr node) {
                             return node->get_id() == ep_node->get_id();
                         }) == all_conditions.end()) {
            all_conditions.emplace_back(ep_node);
        }
    }

    if (ep_node->get_module_type() == synapse::Module::tfhe_Conditional) {
        value_conditions.set_condition(ep_node);

        // Then EP node
        if (value_conditions.get_then_branch() == nullptr) {
            value_conditions.set_then_branch(value_conditions_t());
        } else {
            std::cout << "(value_conditions Then arm on conditional node "
                         "preprocessing) ERROR: This "
                         "should be null..."
                      << std::endl;
        }
        preprocess_aux(ep_node->get_next()[0],
                       *(value_conditions.get_then_branch()), value,
                       all_conditions);

        // Else EP node
        if (value_conditions.get_else_branch() == nullptr) {
            value_conditions.set_else_branch(value_conditions_t());
        } else {
            std::cout << "(value_conditions Else arm on conditional node "
                         "preprocessing) ERROR: This "
                         "should be null..."
                      << std::endl;
        }
        preprocess_aux(ep_node->get_next()[1],
                       *(value_conditions.get_else_branch()), value,
                       all_conditions);

        // Then EP node is just a wrapper around one of the two branch arms
    } else if (ep_node->get_module_type() == synapse::Module::tfhe_Then) {
        preprocess_aux(ep_node->get_next()[0], value_conditions, value,
                       all_conditions);

        // Else EP node is just a wrapper around one of the two branch arms
    } else if (ep_node->get_module_type() == synapse::Module::tfhe_Else) {
        preprocess_aux(ep_node->get_next()[0], value_conditions, value,
                       all_conditions);

        // Modification EP node
    } else if (ep_node->get_module_type() == synapse::Module::tfhe_TernarySum) {
        auto ternary_sum_module =
            std::static_pointer_cast<synapse::targets::tfhe::TernarySum>(
                ep_node->get_module());

        value_conditions.modification =
            ternary_sum_module->get_modification_of(value);
        std::cout << "After getting modification.... Modification: "
                  << generate_tfhe_code(value_conditions.modification)
                  << std::endl;
    }

    if (ep_node->get_module()->get_type() == synapse::Module::tfhe_Drop) {
        std::cout << "Found the end/drop" << std::endl;
        return;
    }
}

// Preprocess the Execution Plan to extract metadata
std::vector<value_conditions_t> preprocess(
    const ExecutionPlanNode_ptr &ep_node,
    std::vector<ExecutionPlanNode_ptr> &all_conditions) {
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
    for (int n_value = 0; n_value < chunk_values_amount; ++n_value) {
        preprocess_aux(packet_borrow_secret_node->get_next()[0],
                       value_conditions[n_value], n_value, all_conditions);
    }

    // TODO At the end we have a metadata object with all the modifications
    //  for each value
    //  We need to simplify the value_conditions_t object and
    //  remove any unnecessary condition nodes
    //  that have all nulls on the "modification" vector.

    return value_conditions;
}

std::string produce_ep_code(const ExecutionPlan &ep) {
    std::string code("");
    ExecutionPlanNode_ptr current_ep_node_ptr = ep.get_root();

    int number_of_values = 0;

    while (true) {
        std::cout << "current Module name:"
                  << current_ep_node_ptr->get_module_name() << std::endl;
        if (current_ep_node_ptr->get_module_type() ==
            synapse::Module::tfhe_CurrentTime) {
            /* Do Nothing */
            std::cout << "Currently on CurrentTime. Do Nothing." << std::endl;

        } else if (current_ep_node_ptr->get_module_type() ==
                   synapse::Module::tfhe_PacketBorrowNextSecret) {
            auto packet_borrow_secret_module = std::static_pointer_cast<
                synapse::targets::tfhe::PacketBorrowNextSecret>(
                current_ep_node_ptr->get_module());

            number_of_values =
                packet_borrow_secret_module->get_chunk_values_amount();

            for (int n_value = 0; n_value < number_of_values; ++n_value) {
                code += std::string("let val") + std::to_string(n_value) +
                        std::string(";\n");
            }

            // TODO
            //  (after printing the values) - Put code for counting time
        } else if (current_ep_node_ptr->get_module_type() ==
                   synapse::Module::tfhe_MonoPBS) {
            auto monoPBS_module =
                std::static_pointer_cast<synapse::targets::tfhe::MonoPBS>(
                    current_ep_node_ptr->get_module());

            /* In a mono PBS, the condition depends only on one value */

            std::string value = monoPBS_module->changed_value_to_string();
            std::string condition_value = monoPBS_module->value_in_condition_to_string();
            std::string condition = monoPBS_module->condition_to_string();
            std::string then_result =
                monoPBS_module->then_modification_to_string();
            std::string else_result =
                monoPBS_module->else_modification_to_string();

            code += std::string("let ") + value + std::string(" = ") +
                    condition_value + std::string(".map(|") + condition_value +
                    std::string("| if ") + condition + std::string(" {") +
                    then_result + std::string("} else {") + else_result +
                    std::string("});\n");
        }
        // TODO Add other types of Conditionals, such as multi PBS
        if (current_ep_node_ptr->has_next()) {
            current_ep_node_ptr = current_ep_node_ptr->get_next_sequential_ep_node();
        } else {
            break;
        }
    }

    // TODO
    //  (after everything is printed) - Put code that closes the counting of
    //  time

    return code;
}

void synthesize_code(const ExecutionPlan &ep) {
    CodeGenerator code_generator(Out);
    // Assuming the only target is TFHE-rs
    assert(TargetList.size() == 1 && TargetList[0] == TargetType::tfhe);
    code_generator.add_target(TargetList[0]);
    ExecutionPlan extracted_ep = code_generator.extract_at(ep, 0);

    // Create file if not exists and open it with write permission
    std::ofstream myfile;
    myfile.open("main.rs");
    std::string code = produce_ep_code(extracted_ep);
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
