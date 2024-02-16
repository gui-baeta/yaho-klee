#pragma once

#include "../module.h"
#include "data_structures/data_structures.h"
#include "memory_bank.h"

namespace synapse {
namespace targets {
namespace tfhe {

class tfheModule : public Module {
public:
    tfheModule(ModuleType _type, const char *_name)
        : Module(_type, TargetType::tfhe, _name) {}

    tfheModule(ModuleType _type, const char *_name, BDD::Node_ptr node)
        : Module(_type, TargetType::tfhe, _name, node) {}

protected:
    void save_map(const ExecutionPlan &ep, addr_t addr) {
        auto mb = ep.get_memory_bank<tfheMemoryBank>(TargetType::tfhe);
        auto saved = mb->has_map_config(addr);
        if (!saved) {
            auto cfg = BDD::symbex::get_map_config(ep.get_bdd(), addr);
            mb->save_map_config(addr, cfg);
        }
    }

    void save_vector(const ExecutionPlan &ep, addr_t addr) {
        auto mb = ep.get_memory_bank<tfheMemoryBank>(TargetType::tfhe);
        auto saved = mb->has_vector_config(addr);
        if (!saved) {
            auto cfg = BDD::symbex::get_vector_config(ep.get_bdd(), addr);
            mb->save_vector_config(addr, cfg);
        }
    }

    void save_dchain(const ExecutionPlan &ep, addr_t addr) {
        auto mb = ep.get_memory_bank<tfheMemoryBank>(TargetType::tfhe);
        auto saved = mb->has_dchain_config(addr);
        if (!saved) {
            auto cfg = BDD::symbex::get_dchain_config(ep.get_bdd(), addr);
            mb->save_dchain_config(addr, cfg);
        }
    }

    void save_sketch(const ExecutionPlan &ep, addr_t addr) {
        auto mb = ep.get_memory_bank<tfheMemoryBank>(TargetType::tfhe);
        auto saved = mb->has_sketch_config(addr);
        if (!saved) {
            auto cfg = BDD::symbex::get_sketch_config(ep.get_bdd(), addr);
            mb->save_sketch_config(addr, cfg);
        }
    }

    void save_cht(const ExecutionPlan &ep, addr_t addr) {
        auto mb = ep.get_memory_bank<tfheMemoryBank>(TargetType::tfhe);
        auto saved = mb->has_cht_config(addr);
        if (!saved) {
            auto cfg = BDD::symbex::get_cht_config(ep.get_bdd(), addr);
            mb->save_cht_config(addr, cfg);
        }
    }

public:
    virtual void visit(ExecutionPlanVisitor &visitor,
                       const ExecutionPlanNode *ep_node) const = 0;
    virtual Module_ptr clone() const = 0;
    virtual bool equals(const Module *other) const = 0;

protected:
//    std::vector<klee::ref<klee::Expr>> get_modifications_from_bddnode(
//        const ExecutionPlan &ep, const BDD::Node_ptr& node) const {
//        klee::ref<klee::Expr> _chunk;
//        klee::ref<klee::Expr> _current_chunk;
//        {
//            auto casted = BDD::cast_node<BDD::Call>(node);
//            auto call = casted->get_call();
//            assert(call.function_name == BDD::symbex::FN_RETURN_CHUNK);
//
//            assert(
//                !call.args[BDD::symbex::FN_BORROW_CHUNK_EXTRA].expr.isNull());
//            assert(!call.args[BDD::symbex::FN_BORROW_CHUNK_EXTRA].in.isNull());
//
//            _chunk = call.args[BDD::symbex::FN_BORROW_CHUNK_EXTRA].expr;
//            _current_chunk = call.args[BDD::symbex::FN_BORROW_CHUNK_EXTRA].in;
//        }
//        klee::ref<klee::Expr> _original_chunk;
//        addr_t _chunk_addr;
//        std::vector<modification_t> _modifications;
//        {
//            auto prev_borrows = get_prev_fn(
//                ep, node,
//                std::vector<std::string>{BDD::symbex::FN_BORROW_CHUNK,
//                                         BDD::symbex::FN_BORROW_SECRET});
//            auto prev_returns =
//                get_prev_fn(ep, node, BDD::symbex::FN_RETURN_CHUNK);
//
//            assert(prev_borrows.size());
//            assert(prev_borrows.size() > prev_returns.size());
//
//            auto target = prev_borrows[prev_returns.size()];
//
//            auto call_node = BDD::cast_node<BDD::Call>(target);
//            assert(call_node);
//
//            auto call = call_node->get_call();
//
//            assert(call.function_name == BDD::symbex::FN_BORROW_CHUNK ||
//                   call.function_name == BDD::symbex::FN_BORROW_SECRET);
//            assert(!call.extra_vars[BDD::symbex::FN_BORROW_CHUNK_EXTRA]
//                        .second.isNull());
//
//            _original_chunk =
//                call.extra_vars[BDD::symbex::FN_BORROW_CHUNK_EXTRA].second;
//            _chunk_addr = kutil::expr_addr_to_obj_addr(_chunk);
//            _modifications =
//                build_modifications(_original_chunk, _current_chunk);
//        }
//
//        auto return_chunk_ep_node = TernarySum(
//            node, _chunk_addr, _original_chunk, _modifications);
//
//        return return_chunk_ep_node.get_modifications_exprs();
//    }
};

}  // namespace tfhe
}  // namespace targets
}  // namespace synapse
