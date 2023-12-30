#include "../../../bdd-reorderer/bdd-reorderer.h"

#include "../execution_plan.h"
#include "../visitors/graphviz/graphviz.h"
#include "../visitors/visitor.h"
#include "module.h"

#include <algorithm>
#include <set>

using BDD::symbex::CHUNK;
using BDD::symbex::PACKET_LENGTH;

namespace synapse {

std::vector<ExecutionPlan> get_reordered(const ExecutionPlan &ep,
                                         int max_reordered) {
    std::vector<ExecutionPlan> reordered;

    if (max_reordered >= 0 &&
        (int)ep.get_meta().reordered_nodes >= max_reordered) {
        return reordered;
    }

    auto next_node = ep.get_next_node();

    if (!next_node) {
        return reordered;
    }

    auto current_node = next_node->get_prev();

    if (!current_node) {
        return reordered;
    }

    auto current_bdd = ep.get_bdd();
    auto current_target = ep.get_current_platform();

    const auto &meta = ep.get_meta();
    auto roots_per_target = meta.roots_per_target.at(current_target);

    auto reordered_bdds =
        BDD::reorder(current_bdd, current_node, roots_per_target);

    for (auto reordered_bdd : reordered_bdds) {
        auto ep_cloned = ep.clone(reordered_bdd.bdd);

        if (!reordered_bdd.condition.isNull()) {
            auto mb = ep_cloned.get_memory_bank();
            mb->add_reorder_data(reordered_bdd.candidate->get_id(),
                                 reordered_bdd.condition);
        }

        ep_cloned.replace_active_leaf_node(reordered_bdd.candidate, false);
        ep_cloned.inc_reordered_nodes();

        // If the next node was a BDD starting point, then actually the starting
        // point becomes the candidate node.
        ep_cloned.replace_roots(next_node->get_id(),
                                reordered_bdd.candidate->get_id());

        reordered.push_back(ep_cloned);
    }

    return reordered;
}

bool can_process_platform(const ExecutionPlan &ep, TargetType target) {
    auto current_target = ep.get_current_platform();
    return current_target == target;
}

processing_result_t Module::process_node(const ExecutionPlan &ep,
                                         BDD::Node_ptr node,
                                         int max_reordered) {
    assert(node);
    processing_result_t result;

    if (can_process_platform(ep, target)) {
        result = process(ep, node);
    }

    std::vector<ExecutionPlan> reordered;

    for (auto ep : result.next_eps) {
        auto ep_reodered = get_reordered(ep, max_reordered);
        reordered.insert(reordered.end(), ep_reodered.begin(),
                         ep_reodered.end());
    }

    if (reordered.size() > 0) {
        Log::dbg() << "+ " << reordered.size() << " reordered BDDs\n";
    }

    result.next_eps.insert(result.next_eps.end(), reordered.begin(),
                           reordered.end());

    return result;
}

bool Module::query_contains_map_has_key(const BDD::Branch *node) const {
    assert(!node->get_condition().isNull());
    auto _condition = node->get_condition();

    kutil::RetrieveSymbols retriever;
    retriever.visit(_condition);

    auto symbols = retriever.get_retrieved_strings();

    auto found_it = std::find_if(
        symbols.begin(), symbols.end(), [](const std::string &symbol) -> bool {
            return symbol.find(BDD::symbex::MAP_HAS_THIS_KEY) !=
                   std::string::npos;
        });

    if (found_it == symbols.end()) {
        return false;
    }

    return true;
}

std::vector<BDD::Node_ptr> Module::get_prev_fn(
    const ExecutionPlan &ep, BDD::Node_ptr node,
    const std::vector<std::string> &functions_names,
    bool ignore_targets) const {
    std::vector<BDD::Node_ptr> prev_functions;

    auto target = ep.get_current_platform();
    const auto &meta = ep.get_meta();

    const auto &roots_per_target = meta.roots_per_target;
    auto roots_it = roots_per_target.find(target);

    auto is_starting_point = [&](const BDD::Node_ptr &node) -> bool {
        if (ignore_targets || roots_it == roots_per_target.end()) {
            return false;
        }

        auto starting_points = roots_it->second;
        return starting_points.find(node->get_id()) != starting_points.end();
    };

    assert(node);

    while (node && (node = node->get_prev()) && !is_starting_point(node)) {
        if (node->get_type() != BDD::Node::NodeType::CALL) {
            continue;
        }

        auto call_node = static_cast<const BDD::Call *>(node.get());
        auto call = call_node->get_call();

        auto found_it = std::find(functions_names.begin(),
                                  functions_names.end(), call.function_name);

        if (found_it != functions_names.end()) {
            prev_functions.push_back(node);
        }
    }

    return prev_functions;
}

std::vector<BDD::Node_ptr> Module::get_prev_fn(const ExecutionPlan &ep,
                                               BDD::Node_ptr node,
                                               const std::string &function_name,
                                               bool ignore_targets) const {
    auto functions_names = std::vector<std::string>{function_name};
    return get_prev_fn(ep, node, functions_names, ignore_targets);
}

std::vector<Module_ptr> Module::get_prev_modules(
    const ExecutionPlan &ep, const std::vector<ModuleType> &targets) const {
    std::vector<Module_ptr> modules;
    auto ep_node = ep.get_active_leaf();

    while (ep_node) {
        auto current = ep_node;
        ep_node = current->get_prev();

        auto module = current->get_module();
        assert(module);

        auto type = module->get_type();

        auto found_it = std::find(targets.begin(), targets.end(), type);

        if (found_it != targets.end()) {
            modules.push_back(module);
        }
    }

    return modules;
}

std::vector<Module::modification_t> Module::build_modifications(
    klee::ref<klee::Expr> before, klee::ref<klee::Expr> after) const {
    std::vector<modification_t> _modifications;
    assert(before->getWidth() == after->getWidth());

    auto size = before->getWidth();

    // TODO This gives me the number of bits of the total chunk. Divided by 8 I get the number of bytes.
    //  I can use this print all the variables that could be modified:
    //  (b,c) = (5 > a)*((b+1,c+1)) + ...
    std::cout << "-- Modifications" << std::endl << "size: " << size << std::endl;

    for (unsigned int b = 0; b < size; b += 8) {
        auto before_byte = kutil::solver_toolbox.exprBuilder->Extract(
            before, b, klee::Expr::Int8);
        auto after_byte = kutil::solver_toolbox.exprBuilder->Extract(
            after, b, klee::Expr::Int8);

        auto eq = kutil::solver_toolbox.are_exprs_always_equal(before_byte,
                                                               after_byte);

        if (eq) {
            continue;
        }

        _modifications.emplace_back(b / 8, after_byte);
    }

    return _modifications;
}

// This is somewhat of a hack... We assume that checksum expressions will only
// be used to modify checksum fields of a packet, not other packet fields.
std::vector<Module::modification_t> Module::ignore_checksum_modifications(
    const std::vector<Module::modification_t> &modifications) const {
    std::vector<Module::modification_t> filtered;

    for (auto mod : modifications) {
        auto simplified = kutil::simplify(mod.expr);
        auto symbols = kutil::get_symbols(simplified);

        if (symbols.size() == 1 && simplified->getWidth() == 8) {
            auto symbol = *symbols.begin();
            auto delim = symbol.find(BDD::symbex::CHECKSUM);

            if (delim != std::string::npos) {
                continue;
            }
        }

        filtered.emplace_back(mod.byte, simplified);
    }

    return filtered;
}

/*
  We use this method to check if we can coalesce the Map + Vector + Dchain
  paradigm into a single data structure mimicking a common map (which
  maps arbitrary data to arbitrary values).

  These data structures should coalesce if the index allocated by the dchain
  is used as a map value and a vector index.

  Multiple vectors can be coalesced into the same data structure, but typically
  a single map and a single dchain are used.

  The pattern we are looking is the following:

  1. Allocating the index
  -> dchain_allocate_new_index(dchain, &index)

  2. Storing the key
  -> vector_borrow(vector_1, index, value_1)

  3. Updating the map
  -> map_put(map_1, key, index)

  4. Returnin the key
  -> vector_return(vector_1, index, value_1)

  [ Loop updating all the other vectors ]
  -> vector_borrow(vector_n, index, value_n)
  -> vector_return(vector_n, index, value_n)
*/

struct next_t {
    std::unordered_set<addr_t> maps;
    std::unordered_set<addr_t> vectors;
    std::unordered_set<addr_t> dchains;

    int size() const { return maps.size() + vectors.size(); }

    void intersect(const next_t &other) {
        auto intersector = [](std::unordered_set<addr_t> &a,
                              const std::unordered_set<addr_t> &b) {
            for (auto it = a.begin(); it != a.end();) {
                if (b.find(*it) == b.end()) {
                    it = a.erase(it);
                } else {
                    it++;
                }
            }
        };

        intersector(maps, other.maps);
        intersector(vectors, other.vectors);
        intersector(dchains, other.dchains);
    }

    bool has_obj(addr_t obj) const {
        if (maps.find(obj) != maps.end()) {
            return true;
        }

        if (vectors.find(obj) != vectors.end()) {
            return true;
        }

        if (dchains.find(obj) != dchains.end()) {
            return true;
        }

        return false;
    }
};

next_t get_next_maps_and_vectors(BDD::Node_ptr root,
                                 klee::ref<klee::Expr> index) {
    assert(root);

    std::vector<BDD::Node_ptr> nodes{root};
    next_t candidates;

    while (nodes.size()) {
        auto node = nodes[0];
        nodes.erase(nodes.begin());

        if (node->get_type() == BDD::Node::BRANCH) {
            auto branch_node = static_cast<const BDD::Branch *>(node.get());
            nodes.push_back(branch_node->get_on_true());
            nodes.push_back(branch_node->get_on_false());
            continue;
        }

        if (node->get_type() != BDD::Node::CALL) {
            continue;
        }

        auto call_node = static_cast<const BDD::Call *>(node.get());
        auto call = call_node->get_call();

        if (call.function_name == BDD::symbex::FN_MAP_PUT) {
            auto _map = call.args[BDD::symbex::FN_MAP_ARG_MAP].expr;
            auto _value = call.args[BDD::symbex::FN_MAP_ARG_VALUE].expr;

            auto _map_addr = kutil::expr_addr_to_obj_addr(_map);
            auto same_index =
                kutil::solver_toolbox.are_exprs_always_equal(index, _value);

            if (same_index) {
                candidates.maps.insert(_map_addr);
            }
        }

        else if (call.function_name == BDD::symbex::FN_VECTOR_BORROW) {
            auto _vector = call.args[BDD::symbex::FN_VECTOR_ARG_VECTOR].expr;
            auto _value = call.args[BDD::symbex::FN_VECTOR_ARG_INDEX].expr;

            auto _vector_addr = kutil::expr_addr_to_obj_addr(_vector);
            auto same_index =
                kutil::solver_toolbox.are_exprs_always_equal(index, _value);

            if (same_index) {
                candidates.vectors.insert(_vector_addr);
            }
        }

        nodes.push_back(node->get_next());
    }

    return candidates;
}

bool Module::is_expr_only_packet_dependent(klee::ref<klee::Expr> expr) const {
    kutil::RetrieveSymbols retriever;
    retriever.visit(expr);

    auto symbols = retriever.get_retrieved_strings();

    std::vector<std::string> allowed_symbols = {
        BDD::symbex::PACKET_LENGTH,
        BDD::symbex::CHUNK,
    };

    for (auto symbol : symbols) {
        auto found_it =
            std::find(allowed_symbols.begin(), allowed_symbols.end(), symbol);

        if (found_it == allowed_symbols.end()) {
            return false;
        }
    }

    return true;
}

std::vector<BDD::Node_ptr> Module::get_all_functions_after_node(
    BDD::Node_ptr root, const std::vector<std::string> &wanted,
    bool stop_on_branches) const {
    std::vector<BDD::Node_ptr> functions;
    std::vector<BDD::Node_ptr> nodes{root};

    while (nodes.size()) {
        auto node = nodes[0];
        nodes.erase(nodes.begin());

        if (node->get_type() == BDD::Node::BRANCH && !stop_on_branches) {
            auto branch_node = static_cast<const BDD::Branch *>(node.get());
            nodes.push_back(branch_node->get_on_true());
            nodes.push_back(branch_node->get_on_false());
        }

        else if (node->get_type() == BDD::Node::CALL) {
            auto call_node = static_cast<const BDD::Call *>(node.get());
            auto call = call_node->get_call();

            auto found_it =
                std::find(wanted.begin(), wanted.end(), call.function_name);

            if (found_it != wanted.end()) {
                functions.push_back(node);
            }

            nodes.push_back(node->get_next());
        }
    }

    return functions;
}

bool Module::is_parser_drop(BDD::Node_ptr root) const {
    assert(root);

    std::vector<BDD::Node_ptr> functions;
    std::vector<BDD::Node_ptr> nodes{root};

    while (nodes.size()) {
        auto node = nodes[0];
        nodes.erase(nodes.begin());

        switch (node->get_type()) {
        case BDD::Node::CALL: {
            auto call_node = BDD::cast_node<BDD::Call>(node);

            if (call_node->get_call().function_name !=
                BDD::symbex::FN_RETURN_CHUNK) {
                return false;
            }

            nodes.push_back(node->get_next());
        } break;
        case BDD::Node::RETURN_PROCESS: {
            auto return_node =
                static_cast<const BDD::ReturnProcess *>(node.get());
            auto ret_value = return_node->get_return_operation();

            if (ret_value != BDD::ReturnProcess::Operation::DROP) {
                return false;
            }
        } break;
        default:
            return false;
        }
    }

    return true;
}

next_t get_allowed_coalescing_objs(std::vector<BDD::Node_ptr> index_allocators,
                                   addr_t obj) {
    next_t candidates;

    for (auto allocator : index_allocators) {
        auto allocator_node = BDD::cast_node<BDD::Call>(allocator);
        auto allocator_call = allocator_node->get_call();

        assert(
            !allocator_call.args[BDD::symbex::FN_DCHAIN_ARG_OUT].out.isNull());
        assert(!allocator_call.args[BDD::symbex::FN_DCHAIN_ARG_CHAIN]
                    .expr.isNull());

        auto allocated_index =
            allocator_call.args[BDD::symbex::FN_DCHAIN_ARG_OUT].out;
        auto dchain =
            allocator_call.args[BDD::symbex::FN_DCHAIN_ARG_CHAIN].expr;
        auto dchain_addr = kutil::expr_addr_to_obj_addr(dchain);

        // We expect the coalescing candidates to be the same regardless of
        // where in the BDD we are. In case there is some discrepancy, then it
        // should be invalid. We thus consider only the intersection of all
        // candidates.

        auto new_candidates =
            get_next_maps_and_vectors(allocator, allocated_index);
        new_candidates.dchains.insert(dchain_addr);

        if (!new_candidates.has_obj(obj)) {
            continue;
        }

        if (candidates.size() == 0) {
            candidates = new_candidates;
        } else {
            // If we can't find the current candidates in the new candidates'
            // list, then it is not true that we can coalesce them in every
            // scenario of the NF.

            candidates.intersect(new_candidates);
        }
    }

    return candidates;
}

Module::map_coalescing_data_t Module::get_map_coalescing_data_t(
    const ExecutionPlan &ep, addr_t obj) const {
    // We can cache results previously made, as BDD reordering will not change
    // the result.
    std::unordered_map<addr_t, Module::map_coalescing_data_t> cache;

    if (cache.find(obj) != cache.end()) {
        return cache[obj];
    }

    Module::map_coalescing_data_t data;

    const auto &bdd = ep.get_bdd();
    auto root = bdd.get_process();
    assert(root);

    auto index_allocators = get_all_functions_after_node(
        root, {BDD::symbex::FN_DCHAIN_ALLOCATE_NEW_INDEX});

    if (index_allocators.size() == 0) {
        return data;
    }

    auto candidates = get_allowed_coalescing_objs(index_allocators, obj);

    if (candidates.size() == 0) {
        return data;
    }

    assert(candidates.maps.size() == 1);
    assert(candidates.dchains.size() == 1);

    data.valid = true;
    data.map = *candidates.maps.begin();
    data.dchain = *candidates.dchains.begin();
    data.vectors = candidates.vectors;

    cache[data.map] = data;
    cache[data.dchain] = data;
    for (auto v : data.vectors) cache[v] = data;

    return data;
}

klee::ref<klee::Expr> Module::get_original_vector_value(
    const ExecutionPlan &ep, BDD::Node_ptr node, addr_t target_addr) const {
    BDD::Node_ptr source;
    return get_original_vector_value(ep, node, target_addr, source);
}

klee::ref<klee::Expr> Module::get_original_vector_value(
    const ExecutionPlan &ep, BDD::Node_ptr node, addr_t target_addr,
    BDD::Node_ptr &source) const {
    auto all_prev_vector_borrow =
        get_prev_fn(ep, node, BDD::symbex::FN_VECTOR_BORROW);

    for (auto prev_vector_borrow : all_prev_vector_borrow) {
        auto call_node = BDD::cast_node<BDD::Call>(prev_vector_borrow);
        assert(call_node);

        auto call = call_node->get_call();

        assert(!call.args[BDD::symbex::FN_VECTOR_ARG_VECTOR].expr.isNull());
        assert(!call.extra_vars[BDD::symbex::FN_VECTOR_EXTRA].second.isNull());

        auto _vector = call.args[BDD::symbex::FN_VECTOR_ARG_VECTOR].expr;
        auto _borrowed_cell =
            call.extra_vars[BDD::symbex::FN_VECTOR_EXTRA].second;

        auto _vector_addr = kutil::expr_addr_to_obj_addr(_vector);

        if (_vector_addr != target_addr) {
            continue;
        }

        source = prev_vector_borrow;
        return _borrowed_cell;
    }

    assert(false && "Expecting a previous vector borrow but not found.");
    Log::err() << "Expecting a previous vector borrow but not found. Run with "
                  "debug.\n";
    exit(1);
}

bool is_incrementing_op(klee::ref<klee::Expr> before,
                        klee::ref<klee::Expr> after) {
    if (after->getKind() != klee::Expr::Add) {
        return false;
    }

    auto lhs = after->getKid(0);
    auto rhs = after->getKid(1);

    auto lhs_is_const = lhs->getKind() == klee::Expr::Constant;
    auto rhs_is_const = rhs->getKind() == klee::Expr::Constant;

    if (!lhs_is_const && !rhs_is_const) {
        return false;
    }

    auto const_expr = lhs_is_const ? lhs : rhs;
    auto not_const_expr = lhs_is_const ? rhs : lhs;

    // We only support increment of one, for now...
    assert(kutil::solver_toolbox.value_from_expr(const_expr) == 1);

    auto eq_to_before =
        kutil::solver_toolbox.are_exprs_always_equal(not_const_expr, before);

    return eq_to_before;
}

std::pair<bool, uint64_t> get_max_value(klee::ref<klee::Expr> original_value,
                                        klee::ref<klee::Expr> condition) {
    auto max_value = std::pair<bool, uint64_t>{false, 0};

    auto original_symbol = kutil::get_symbol(original_value);
    assert(original_symbol.first);

    auto symbol = kutil::get_symbol(condition);

    if (!symbol.first) {
        return max_value;
    }

    // We are looking for expression that look like this:
    // !(65536 <= vector_data_reset)
    // We should be more careful with this and be compatible with more
    // expressions.

    while (condition->getKind() == klee::Expr::Not) {
        condition = condition->getKid(0);
    }

    if (condition->getKind() == klee::Expr::Eq) {
        auto lhs = condition->getKid(0);
        auto rhs = condition->getKid(1);
        auto lhs_is_const = lhs->getKind() == klee::Expr::Constant;

        if (!lhs_is_const) {
            return max_value;
        }

        auto const_value = kutil::solver_toolbox.value_from_expr(lhs);

        if (const_value != 0) {
            return max_value;
        }

        condition = rhs;
    }

    if (condition->getKind() != klee::Expr::Ule) {
        return max_value;
    }

    auto lhs = condition->getKid(0);
    auto rhs = condition->getKid(1);

    auto lhs_is_const = lhs->getKind() == klee::Expr::Constant;
    auto rhs_is_const = rhs->getKind() == klee::Expr::Constant;

    if (!lhs_is_const && !rhs_is_const) {
        return max_value;
    }

    auto const_expr = lhs_is_const ? lhs : rhs;
    auto not_const_expr = lhs_is_const ? rhs : lhs;

    if (!kutil::solver_toolbox.are_exprs_always_equal(not_const_expr,
                                                      original_value)) {
        return max_value;
    }

    auto value = kutil::solver_toolbox.value_from_expr(const_expr);

    max_value.first = true;
    max_value.second = value;

    return max_value;
}

bool is_counter_inc_op(BDD::Node_ptr vector_borrow,
                       std::pair<bool, uint64_t> &max_value) {
    auto branch = vector_borrow->get_next();
    auto branch_node = BDD::cast_node<BDD::Branch>(branch);

    if (!branch_node) {
        return false;
    }

    auto condition = branch_node->get_condition();
    auto on_true = branch_node->get_on_true();
    auto on_false = branch_node->get_on_false();

    auto borrow_node = BDD::cast_node<BDD::Call>(vector_borrow);
    auto on_true_node = BDD::cast_node<BDD::Call>(on_true);
    auto on_false_node = BDD::cast_node<BDD::Call>(on_false);

    if (!on_true_node || !on_false_node) {
        return false;
    }

    auto borrow_call = borrow_node->get_call();
    auto on_true_call = on_true_node->get_call();
    auto on_false_call = on_false_node->get_call();

    if (on_true_call.function_name != BDD::symbex::FN_VECTOR_RETURN ||
        on_false_call.function_name != BDD::symbex::FN_VECTOR_RETURN) {
        return false;
    }

    assert(!borrow_call.args[BDD::symbex::FN_VECTOR_ARG_VECTOR].expr.isNull());
    assert(
        !borrow_call.extra_vars[BDD::symbex::FN_VECTOR_EXTRA].second.isNull());

    assert(!on_true_call.args[BDD::symbex::FN_VECTOR_ARG_VECTOR].expr.isNull());
    assert(!on_true_call.args[BDD::symbex::FN_VECTOR_ARG_VALUE].in.isNull());

    assert(
        !on_false_call.args[BDD::symbex::FN_VECTOR_ARG_VECTOR].expr.isNull());
    assert(!on_false_call.args[BDD::symbex::FN_VECTOR_ARG_VALUE].in.isNull());

    auto borrow_vector =
        borrow_call.args[BDD::symbex::FN_VECTOR_ARG_VECTOR].expr;
    auto on_true_vector =
        on_true_call.args[BDD::symbex::FN_VECTOR_ARG_VECTOR].expr;
    auto on_false_vector =
        on_false_call.args[BDD::symbex::FN_VECTOR_ARG_VECTOR].expr;

    auto borrow_vector_addr = kutil::expr_addr_to_obj_addr(borrow_vector);
    auto on_true_vector_addr = kutil::expr_addr_to_obj_addr(on_true_vector);
    auto on_false_vector_addr = kutil::expr_addr_to_obj_addr(on_false_vector);

    if (borrow_vector_addr != on_true_vector_addr ||
        borrow_vector_addr != on_false_vector_addr) {
        return false;
    }

    auto borrow_value =
        borrow_call.extra_vars[BDD::symbex::FN_VECTOR_EXTRA].second;
    auto on_true_value = on_true_call.args[BDD::symbex::FN_VECTOR_ARG_VALUE].in;
    auto on_false_value =
        on_false_call.args[BDD::symbex::FN_VECTOR_ARG_VALUE].in;

    auto on_true_inc_op = is_incrementing_op(borrow_value, on_true_value);

    if (!on_true_inc_op) {
        return false;
    }

    auto on_false_eq = kutil::solver_toolbox.are_exprs_always_equal(
        borrow_value, on_false_value);

    if (!on_false_eq) {
        return false;
    }

    auto local_max_value = get_max_value(borrow_value, condition);

    if (!max_value.first) {
        max_value = local_max_value;
    } else if (max_value.second != local_max_value.second) {
        return false;
    }

    return true;
}

bool is_counter_read_op(BDD::Node_ptr vector_borrow) {
    auto vector_return = vector_borrow->get_next();

    auto borrow_node = BDD::cast_node<BDD::Call>(vector_borrow);
    auto return_node = BDD::cast_node<BDD::Call>(vector_return);

    if (!return_node) {
        return false;
    }

    auto borrow_call = borrow_node->get_call();
    auto return_call = return_node->get_call();

    if (return_call.function_name != BDD::symbex::FN_VECTOR_RETURN) {
        return false;
    }

    assert(!borrow_call.args[BDD::symbex::FN_VECTOR_ARG_VECTOR].expr.isNull());
    assert(
        !borrow_call.extra_vars[BDD::symbex::FN_VECTOR_EXTRA].second.isNull());

    assert(!return_call.args[BDD::symbex::FN_VECTOR_ARG_VECTOR].expr.isNull());
    assert(!return_call.args[BDD::symbex::FN_VECTOR_ARG_VALUE].in.isNull());

    auto borrow_vector =
        borrow_call.args[BDD::symbex::FN_VECTOR_ARG_VECTOR].expr;
    auto return_vector =
        return_call.args[BDD::symbex::FN_VECTOR_ARG_VECTOR].expr;

    auto borrow_vector_addr = kutil::expr_addr_to_obj_addr(borrow_vector);
    auto return_vector_addr = kutil::expr_addr_to_obj_addr(return_vector);

    if (borrow_vector_addr != return_vector_addr) {
        return false;
    }

    auto borrow_value =
        borrow_call.extra_vars[BDD::symbex::FN_VECTOR_EXTRA].second;
    auto return_value = return_call.args[BDD::symbex::FN_VECTOR_ARG_VALUE].in;

    auto equal_values = kutil::solver_toolbox.are_exprs_always_equal(
        borrow_value, return_value);

    return equal_values;
}

Module::counter_data_t Module::is_counter(const ExecutionPlan &ep,
                                          addr_t obj) const {
    Module::counter_data_t data;

    auto bdd = ep.get_bdd();
    auto cfg = BDD::symbex::get_vector_config(ep.get_bdd(), obj);

    if (cfg.elem_size > 64 || cfg.capacity != 1) {
        return data;
    }

    auto root = bdd.get_process();
    auto vector_borrows =
        get_all_functions_after_node(root, {BDD::symbex::FN_VECTOR_BORROW});

    for (auto vector_borrow : vector_borrows) {
        auto call_node = BDD::cast_node<BDD::Call>(vector_borrow);
        auto call = call_node->get_call();

        assert(!call.args[BDD::symbex::FN_VECTOR_ARG_VECTOR].expr.isNull());
        auto _vector = call.args[BDD::symbex::FN_VECTOR_ARG_VECTOR].expr;
        auto _vector_addr = kutil::expr_addr_to_obj_addr(_vector);

        if (_vector_addr != obj) {
            continue;
        }

        if (is_counter_read_op(vector_borrow)) {
            data.reads.push_back(vector_borrow);
            continue;
        }

        if (is_counter_inc_op(vector_borrow, data.max_value)) {
            data.writes.push_back(vector_borrow);
            continue;
        }

        return data;
    }

    data.valid = true;
    return data;
}

klee::ref<klee::Expr> Module::get_expr_from_addr(const ExecutionPlan &ep,
                                                 addr_t addr) const {
    auto bdd = ep.get_bdd();
    auto root = bdd.get_process();
    auto nodes = get_all_functions_after_node(root, {
                                                        BDD::symbex::FN_MAP_GET,
                                                    });

    for (auto node : nodes) {
        auto call_node = BDD::cast_node<BDD::Call>(node);
        assert(call_node);

        auto call = call_node->get_call();

        if (call.function_name == BDD::symbex::FN_MAP_GET) {
            assert(!call.args[BDD::symbex::FN_MAP_ARG_KEY].expr.isNull());
            assert(!call.args[BDD::symbex::FN_MAP_ARG_KEY].in.isNull());

            auto _key_addr = call.args[BDD::symbex::FN_MAP_ARG_KEY].expr;
            auto _key = call.args[BDD::symbex::FN_MAP_ARG_KEY].in;
            auto _key_addr_value = kutil::expr_addr_to_obj_addr(_key_addr);

            if (_key_addr_value != addr) {
                continue;
            }

            return _key;
        } else {
            assert(false);
        }
    }

    return nullptr;
}

}  // namespace synapse
