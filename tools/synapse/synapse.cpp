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

std::pair<std::vector<ExecutionPlan>, SearchSpace> search(const BDD::BDD &bdd,
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
    tfheMostAbstracted tfhe_most_abstracted;

    // auto winner = search_engine.search(biggest, peek);
    // auto winner = search_engine.search(least_reordered, peek);
    // auto winner = search_engine.search(dfs, peek);
    // auto winner = search_engine.search(most_compact, peek);
    // auto winner = search_engine.search(maximize_switch_nodes, peek);
    std::vector<ExecutionPlan> winner = search_engine.search(tfhe_most_abstracted, peek);
    const auto &ss = search_engine.get_search_space();

    return {winner, ss};
}

//struct gen_data_t {
//    int chunk_values_amount;
//    std::vector<ep_node_id_t> visited_ep_nodes;
//    std::vector<std::string> conditions;
//};

//std::string synthesize_code_aux(const ExecutionPlanNode_ptr &ep_node,
//                                gen_data_t &gen_data) {
//    std::string code("");
//
//    std::cout << "Module Type: " << ep_node->get_module()->get_name()
//              << std::endl;
//
//    // If the node has already been visited, return
//    if (std::find(gen_data.visited_ep_nodes.begin(),
//                  gen_data.visited_ep_nodes.end(),
//                  ep_node->get_id()) != gen_data.visited_ep_nodes.end()) {
//        return code;
//    }
//
//    if (ep_node->get_module()->get_type() ==
//        synapse::Module::ModuleType::tfhe_CurrentTime) {
//        // Do nothing
//        gen_data.visited_ep_nodes.push_back(ep_node->get_id());
//        code += synthesize_code_aux(ep_node->get_next()[0], gen_data);
//
//    } else if (ep_node->get_module()->get_type() ==
//               synapse::Module::ModuleType::tfhe_Conditional) {
//        gen_data.visited_ep_nodes.push_back(ep_node->get_id());
//        auto conditional_module =
//            std::static_pointer_cast<synapse::targets::tfhe::Conditional>(
//                ep_node->get_module());
//
//        size_t this_cond_val_num = gen_data.conditions.size();
//        bool first_conditional_in_conditional_nesting = this_cond_val_num == 0;
//
//        gen_data.conditions.push_back(
//            std::string("let cond_val") +
//            std::to_string(gen_data.conditions.size()) + std::string(" = ") +
//            conditional_module->generate_code() + std::string("\n"));
//
//        // FIXME This code is hardcoded for flattened if-else statements
//        // FIXME I don't like this solution, but for now it will do
//        //  Better solution -> See if this node is a child of a Else or Then
//        //  node?
//        if (first_conditional_in_conditional_nesting) {
//            code += std::string("let result = cond_val");
//            //            for (int n_value = gen_data.chunk_values_amount - 1;
//            //            n_value >= 0;
//            //                 --n_value) {
//            //                code += std::string("val") +
//            //                std::to_string(n_value); if (n_value > 0) {
//            //                    code += std::string(", ");
//            //                }
//            //            }
//            //            code += std::string(" = ");
//        } else {
//            code += std::string("&cond_val");
//        }
//        code += std::to_string(this_cond_val_num) + std::string(".mul_distr(") +
//                synthesize_code_aux(ep_node->get_next()[0], gen_data) +
//                std::string(").add(cond_val") +
//                std::to_string(this_cond_val_num) +
//                std::string(".map(not).mul_distr(") +
//                synthesize_code_aux(ep_node->get_next()[1], gen_data) +
//                std::string("))\n");
//
//    } else if (ep_node->get_module()->get_type() ==
//               synapse::Module::ModuleType::tfhe_Then) {
//        gen_data.visited_ep_nodes.push_back(ep_node->get_id());
//        code += synthesize_code_aux(ep_node->get_next()[0], gen_data);
//
//    } else if (ep_node->get_module()->get_type() ==
//               synapse::Module::ModuleType::tfhe_Else) {
//        gen_data.visited_ep_nodes.push_back(ep_node->get_id());
//        code += synthesize_code_aux(ep_node->get_next()[0], gen_data);
//
//    } else if (ep_node->get_module()->get_type() ==
//               synapse::Module::ModuleType::tfhe_Operation) {
//        gen_data.visited_ep_nodes.push_back(ep_node->get_id());
//        auto operation_module =
//            std::static_pointer_cast<synapse::targets::tfhe::Operation>(
//                ep_node->get_module());
//        code += operation_module->generate_code();
//
//    } else if (ep_node->get_module()->get_type() ==
//               synapse::Module::tfhe_PacketBorrowNextChunk) {
//        gen_data.visited_ep_nodes.push_back(ep_node->get_id());
//        auto packet_borrow_next_chunk_module = std::static_pointer_cast<
//            synapse::targets::tfhe::PacketBorrowNextChunk>(
//            ep_node->get_module());
//        //        code += packet_borrow_next_chunk_module->generate_code();
//        gen_data.chunk_values_amount =
//            packet_borrow_next_chunk_module->get_chunk_values_amount();
//
//        code += synthesize_code_aux(ep_node->get_next()[0], gen_data);
//
//    } else if (ep_node->get_module()->get_type() ==
//               synapse::Module::tfhe_PacketBorrowNextSecret) {
//        gen_data.visited_ep_nodes.push_back(ep_node->get_id());
//        auto packet_borrow_next_secret_module = std::static_pointer_cast<
//            synapse::targets::tfhe::PacketBorrowNextSecret>(
//            ep_node->get_module());
//        //        code += packet_borrow_next_secret_module->generate_code();
//        gen_data.chunk_values_amount =
//            packet_borrow_next_secret_module->get_chunk_values_amount();
//
//        code += synthesize_code_aux(ep_node->get_next()[0], gen_data);
//    }
//
//    // FIXME This is probably too generic and I want to control each node that
//    //  is to be processed
//    //    if (!ep_node->get_next().empty()) {
//    //        gen_data.visited_ep_nodes.push_back(ep_node->get_id());
//    //        code += synthesize_code_aux(ep_node->get_next()[0], gen_data);
//    //    }
//
//    return code;
//}

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
    } else if (ep_node->get_module_type() == synapse::Module::tfhe_Operation) {
        auto operation_module =
            std::static_pointer_cast<synapse::targets::tfhe::Operation>(
                ep_node->get_module());

        value_conditions.modification =
            operation_module->get_modification_of(value);
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

std::string tfhe_initial_boiler_plate(){
    // Open the file for reading
    std::ifstream file("tfhe_initial_boiler_plate.rs");

    // Check if the file was opened successfully
    if (!file.is_open()) {
        std::cerr << "Unable to open file\n";
        exit(2);
    }

    // Read the entire content of the file into a string
    std::string content((std::istreambuf_iterator<char>(file)),
                        (std::istreambuf_iterator<char>()));

    // Close the file
    file.close();

    return content;
}

std::string tfhe_end_boiler_plate(int number_of_values){
    // (after everything is printed) - Put code that closes the counting of time
    std::string _str = "\tlet elapsed_time = std::time::Instant::now() - time;\n"
            "\tprintln!(\"Result:\");\n";
    for (int n_value = 0; n_value < number_of_values; ++n_value) {
        _str = _str + "\tprintln!(\"val" + std::to_string(n_value) + ": {}\", val" + std::to_string(n_value) + ".decrypt(&client_key));\n";
    }
    _str += "\n"
            "\tprintln!(\"Time taken: {:?}\", elapsed_time.as_secs_f64());\n"
            "}\n";

    return _str;
}

std::string produce_ep_code(const ExecutionPlan &ep) {
    std::ostringstream code;

    ExecutionPlanNode_ptr current_ep_node_ptr = ep.get_root();

    int number_of_values = 0;

    code << tfhe_initial_boiler_plate();

    while (true) {
        std::cout << "current Module name:"
                  << current_ep_node_ptr->get_module_name() << std::endl;
        if (current_ep_node_ptr->get_module_type() ==
            synapse::Module::tfhe_CurrentTime) {
            /* Do Nothing */

        } else if (current_ep_node_ptr->get_module_type() ==
                   synapse::Module::tfhe_PacketBorrowNextSecret) {
            auto packet_borrow_secret_module = std::static_pointer_cast<
                synapse::targets::tfhe::PacketBorrowNextSecret>(
                current_ep_node_ptr->get_module());

            number_of_values =
                packet_borrow_secret_module->get_chunk_values_amount();

            code << "\t// Packet Borrow Next Secret\n";

            for (int n_value = 0; n_value < number_of_values; ++n_value) {
                code << "\tlet val" << n_value
                     << ": FheUint = FheUint::encrypt(values[" << n_value
                     << "], &client_key);" << std::endl;
            }

            // (after printing the values) - Put code for counting time
            code << "\tlet time = std::time::Instant::now();" << std::endl;
        } else if (current_ep_node_ptr->get_module_type() ==
                   synapse::Module::tfhe_UnivariatePBS) {
            auto univariatePBS_module =
                std::static_pointer_cast<synapse::targets::tfhe::UnivariatePBS>(
                    current_ep_node_ptr->get_module());

            /* In a mono PBS, the condition depends only on one value */

            code << "\t// Univariate PBS\n";
            code << "\t" << univariatePBS_module;
        } else if (current_ep_node_ptr->get_module_type() ==
                   synapse::Module::tfhe_AidedUnivariatePBS) {
            auto aided_univariate_pbs = std::static_pointer_cast<
                synapse::targets::tfhe::AidedUnivariatePBS>(
                current_ep_node_ptr->get_module());

            /* In an Aided Univariate PBS, the condition depends only on one
             * value */

            code << "\t// Aided Univariate PBS\n";
            code << "\t" << aided_univariate_pbs;
        } else if (current_ep_node_ptr->get_module_type() ==
                   synapse::Module::tfhe_Change) {
            auto change_module =
                std::static_pointer_cast<synapse::targets::tfhe::Change>(
                    current_ep_node_ptr->get_module());

            /* A Change generates one value change */

            code << "\t// Change\n";
            code << "\t" << change_module;
        } else if (current_ep_node_ptr->get_module_type() ==
                   synapse::Module::tfhe_NoChange) {
            auto no_change_module =
                std::static_pointer_cast<synapse::targets::tfhe::NoChange>(
                    current_ep_node_ptr->get_module());

            /* Generates nothing */

            code << no_change_module;
        } else if (current_ep_node_ptr->get_module_type() ==
                   synapse::Module::tfhe_NoOpPacketReturnChunk) {
            auto no_op_packet_return_chunk_module = std::static_pointer_cast<
                synapse::targets::tfhe::NoOpPacketReturnChunk>(
                current_ep_node_ptr->get_module());

            /* Generates nothing */

            code << no_op_packet_return_chunk_module;
        }

        // TODO Add other types of Conditionals, such as multi PBS

        if (current_ep_node_ptr->has_next()) {
            current_ep_node_ptr =
                current_ep_node_ptr->get_next_sequential_ep_node();
        } else {
            break;
        }
    }

    // (after everything is printed) - Put code that closes the counting of time
    code << "\t"
         << "let elapsed_time = std::time::Instant::now() - time;" << std::endl;

    code << "\tprintln!(\"Result:\");\n";
    for (int n_value = 0; n_value < number_of_values; ++n_value) {
        code << "\tprintln!(\"val" << n_value << ": {}\", val" << n_value << ".decrypt(&client_key));\n";
    }
    code << std::endl;

    code << "\tprintln!(\"Time taken: {:?}\", elapsed_time.as_secs_f64());" << std::endl;

    code << "}" << std::endl;

    return code.str();
}

/// Recursively visit the parent node until the parent is a packet borrow
/// Then, start returning the code up to bottom - back down
std::string synthesize_code_aux(ExecutionPlanNode_ptr ep_node, int changed_value, int column_i, klee::ref<klee::Expr> value_modification, int then_arm = -1) {
    if (ep_node->get_module_type() == synapse::Module::ModuleType::tfhe_Operation) {

        return "\t" + std::string("let val") + std::to_string(changed_value) + "_" + std::to_string(column_i) + " = " + generate_tfhe_code(value_modification, true) +
               synthesize_code_aux(ep_node->get_prev(), changed_value, column_i, nullptr);

//        if (ep_node->get_prev()->get_prev()->get_module_type() == synapse::Module::ModuleType::tfhe_UnivariatePBS) {
//            std::cout << "Found an Univariate PBS..." << std::endl;
//            std::string then_str = ep_node->get_prev()->get_prev()->get_module()->univariate_pbs_to_string(changed_value, nullptr, nullptr, false);
//            std::string else_str = ep_node->get_prev()->get_prev()->get_module()->univariate_pbs_to_string(changed_value, nullptr, nullptr, false);
//
//            if (ep_node->get_prev()->get_module_type() == synapse::Module::ModuleType::tfhe_Then) {
//                return "\t" + generate_tfhe_code(value_modification, true) + " * " + then_str + synthesize_code_aux(ep_node->get_prev()->get_prev()->get_prev(), changed_value, nullptr);
//            } else if (ep_node->get_prev()->get_module_type() == synapse::Module::ModuleType::tfhe_Else) {
//                return "\t" + else_str + synthesize_code_aux(ep_node->get_prev()->get_prev()->get_prev(), changed_value, nullptr);
//            }
//        }
//        if (ep_node->get_prev()->get_prev()->get_module_type() == synapse::Module::ModuleType::tfhe_BivariatePBS) {
//            std::cout << "Found a Bivariate PBS..." << std::endl;
//            std::string then_str = ep_node->get_prev()->get_prev()->get_module()->bivariate_pbs_to_string(changed_value, value_modification, nullptr, false);
//            std::string else_str = ep_node->get_prev()->get_prev()->get_module()->bivariate_pbs_to_string(changed_value, nullptr, value_modification, false);
//
//            if (ep_node->get_prev()->get_module_type() == synapse::Module::ModuleType::tfhe_Then) {
//                return "\t" + then_str + synthesize_code_aux(ep_node->get_prev()->get_prev()->get_prev(), changed_value, nullptr);
//            } else if (ep_node->get_prev()->get_module_type() == synapse::Module::ModuleType::tfhe_Else) {
//                return "\t" + else_str + synthesize_code_aux(ep_node->get_prev()->get_prev()->get_prev(), changed_value, nullptr);
//            }
//        }
    } else if (ep_node->get_module_type() == synapse::Module::ModuleType::tfhe_Then) {
        return " * " + synthesize_code_aux(ep_node->get_prev(), changed_value, column_i, nullptr, 1);

    } else if (ep_node->get_module_type() == synapse::Module::ModuleType::tfhe_Else) {
        return " * " + synthesize_code_aux(ep_node->get_prev(), changed_value, column_i, nullptr, 0);

    } else if (ep_node->get_module_type() == synapse::Module::ModuleType::tfhe_UnivariatePBS) {
        std::string str = ep_node->get_module()->univariate_pbs_to_string(changed_value, nullptr, then_arm, false);

        // FIXME Test
        str = ep_node->get_module()->to_string_debug(then_arm, true, ep_node->cond_id);
        return str + synthesize_code_aux(ep_node->get_prev(), changed_value, column_i, nullptr, -1);
    } else if (ep_node->get_module_type() == synapse::Module::ModuleType::tfhe_BivariatePBS) {
        std::string str = ep_node->get_module()->bivariate_pbs_to_string(changed_value, nullptr, nullptr, then_arm, false);

        // FIXME Test
        str = ep_node->get_module()->to_string_debug(then_arm, true, ep_node->cond_id);
        return str + synthesize_code_aux(ep_node->get_prev(), changed_value, column_i, nullptr, -1);
    }

    if (ep_node->get_module_type() == synapse::Module::ModuleType::tfhe_PacketBorrowNextSecret) {
        return ";\n";
    }

//    if (ep_node->get_prev()->get_module_type() == synapse::Module::ModuleType::tfhe_AidedUnivariatePBS) {
//        std::cout << "Found an Aided Univariate PBS..." << std::endl;
//        std::string then_str = ep_node->get_prev()->get_module()->aided_univariate_pbs_to_string(changed_value, true);
//        std::string else_str = ep_node->get_prev()->get_module()->aided_univariate_pbs_to_string(changed_value, false);
//
//        if (ep_node->get_module_type() == synapse::Module::ModuleType::tfhe_Then) {
//            return " * " + then_str + synthesize_code_aux(ep_node->get_prev()->get_prev(), changed_value, nullptr);
//        } else if (ep_node->get_module_type() == synapse::Module::ModuleType::tfhe_Else) {
//            return " * " + else_str + synthesize_code_aux(ep_node->get_prev()->get_prev(), changed_value, nullptr);
//        }
//    }

    std::cerr << "No return statement on synthesizing code" << std::endl;
}

std::string synthesize_ep_code(ExecutionPlan &ep) {
    std::ostringstream code;

    std::cout << "Synthesizing Execution Plan code..." << std::endl;

    code << tfhe_initial_boiler_plate();

    int number_of_values = 0;
    auto packet_borrow_node = ep.find_node_by_module_type(
        synapse::Module::ModuleType::tfhe_PacketBorrowNextSecret);
    auto packet_borrow_module = std::static_pointer_cast<synapse::targets::tfhe::PacketBorrowNextSecret>(
        packet_borrow_node->get_module());
    number_of_values = packet_borrow_module->get_chunk_values_amount();

    code << "\t// Packet Borrow Next Secret\n";

    for (int n_value = 0; n_value < number_of_values; ++n_value) {
        code << "\tlet val" << n_value
             << ": FheUint = FheUint::encrypt(values[" << n_value
             << "], &client_key);" << std::endl;
    }

    // (after printing the values) - Put code for counting time
    code << "\tlet time = std::time::Instant::now();" << std::endl;

    std::vector<ExecutionPlanNode_ptr> different_conditions = ep.get_all_different_conditions();

    // Conditions calculation
    for (auto& cond : different_conditions) {
        if (cond->get_module_type() == synapse::Module::ModuleType::tfhe_UnivariatePBS) {
            // True arm
            code << "\tlet c" << std::to_string(cond->cond_id) << "_1" << " = ";
            code << cond->get_module()->univariate_pbs_to_string(0, nullptr, 1, false) << ";\n";
            // False arm
            code << "\tlet c" << std::to_string(cond->cond_id) << "_0" << " = ";
            code << cond->get_module()->univariate_pbs_to_string(0, nullptr, 0, false) << ";\n";
        } else if (cond->get_module_type() == synapse::Module::ModuleType::tfhe_BivariatePBS) {
            code << "\tlet c" << std::to_string(cond->cond_id) << "_1" << " = ";
            code << cond->get_module()->bivariate_pbs_to_string(0, nullptr, nullptr, 1, false) << ";\n";
            code << "\tlet c" << std::to_string(cond->cond_id) << "_0" << " = ";
            code << cond->get_module()->bivariate_pbs_to_string(0, nullptr, nullptr, 0, false) << ";\n";
        }
    }

    std::vector<ExecutionPlanNode_ptr> packet_return_ep_nodes = ep.get_packet_return_chunks_ep_nodes();

    int num_of_columns = packet_return_ep_nodes.size();

    int packet_return_i = 0;
    int column_i = 0;
    for (auto ep_node : packet_return_ep_nodes) {
        std::cout << "Packet Return Chunk " << packet_return_i << std::endl;
        auto operation_module = std::static_pointer_cast<synapse::targets::tfhe::Operation>(ep_node->get_module());

        // If this Operation Module has nothing, no need to generate code
        if (operation_module->get_modifications().empty()) {
            // No changes for packet return chunk
            std::cout << "-- No changes for packet return chunk" << std::endl;
            continue;
        }

        std::vector<klee::ref<klee::Expr>> value_mods = operation_module->get_modifications_exprs();

        for (int i = 0; i < value_mods.size(); ++i) {
            if (value_mods[i].isNull()) {
                std::cout << "Value " << i << " is null" << std::endl;
                continue;
            } else {
                std::cout << "Value " << i << " is modified" << std::endl;
            }

            code << synthesize_code_aux(ep_node, i, column_i, value_mods[i]);

            std::cout << "--------------------------------------------" << std::endl;
            std::cout << code.str() << std::endl;
            std::cout << "---------------------------------------------" << std::endl;
        }

        packet_return_i += 1;
        column_i += 1;
    }

    num_of_columns = column_i;

    std::string sum_of_columns = "\tlet val" + std::to_string(number_of_values - 1) + " = ";

    for (int i = 0; i < num_of_columns; ++i) {
        sum_of_columns += std::string("val") + std::to_string(number_of_values - 1) + "_" + std::to_string(i);
        if (i == num_of_columns - 1) {
            sum_of_columns += ";";
        } else {
            sum_of_columns += " + ";
        }
    }

    code << sum_of_columns << std::string("\n");

    code << tfhe_end_boiler_plate(number_of_values);

    return code.str();
}

void synthesize_code(const std::vector<ExecutionPlan> &eps) {
    CodeGenerator code_generator(Out);

    std::cout << "Output Directory: " << Out << std::endl;
    // Assuming the only target is TFHE-rs
    assert(TargetList.size() == 1 && TargetList[0] == TargetType::tfhe);
    code_generator.add_target(TargetList[0]);

    std::vector<ExecutionPlan> extracted_eps;

    for (auto& ep : eps) {
        extracted_eps.push_back(code_generator.extract_at(ep, 0));
    }

    for (int i = 0; i < extracted_eps.size(); ++i) {
        // Create file if not exists and open it with write permission
        std::ofstream myfile;
        myfile.open(Out + "main_" + std::to_string(i) + ".rs");
        if (!myfile.is_open()) {
            std::cerr << "Error opening file for writing!" << std::endl;
            return;  // or handle the error appropriately
        }
        std::string code = synthesize_ep_code(extracted_eps.at(i));
        myfile << code;
        myfile.flush();
        myfile.close();
    }

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
    std::pair<std::vector<ExecutionPlan>, SearchSpace> search_results = search(bdd, Peek);
    auto end_search = std::chrono::steady_clock::now();

    auto search_dt = std::chrono::duration_cast<std::chrono::seconds>(
                         end_search - start_search)
                         .count();

    if (ShowEP) {
        Graphviz::visualize(search_results.first.at(0), false, "/home/guibaeta/Documents/yaho-synapse/synthesized/graphs/main_" + std::to_string(0) + ".gv");

//        for (int i = 0; i < search_results.first.size(); ++i) {
//            Graphviz::visualize(search_results.first.at(i), false, "/home/guibaeta/Documents/yaho-synapse/synthesized/graphs/main_" + std::to_string(i) + ".gv");
//        }
    }

    std::cout << "Finished exporting graphs" << std::endl;

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
