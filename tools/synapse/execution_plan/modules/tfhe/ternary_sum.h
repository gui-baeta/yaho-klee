#pragma once

#include "tfhe_module.h"
#include "../../../tfhe_generate_code.h"

namespace synapse {
namespace targets {
namespace tfhe {

// TODO Rename this class to SumInConditionalBranch
class TernarySum : public tfheModule {
private:
    addr_t chunk_addr;
    klee::ref<klee::Expr> original_chunk;
    std::vector<modification_t> modifications;

public:
    TernarySum() : tfheModule(ModuleType::tfhe_TernarySum, "TernarySum") {}

    TernarySum(BDD::Node_ptr node, addr_t _chunk_addr,
               klee::ref<klee::Expr> _original_chunk,
               const std::vector<modification_t> &_modifications)
        : tfheModule(ModuleType::tfhe_TernarySum, "TernarySum", node),
          chunk_addr(_chunk_addr),
          original_chunk(_original_chunk),
          modifications(_modifications) {}

private:
    klee::ref<klee::Expr> get_original_chunk(const ExecutionPlan &ep,
                                             BDD::Node_ptr node) const {
        auto prev_borrows = get_prev_fn(
            ep, node,
            std::vector<std::string>{BDD::symbex::FN_BORROW_CHUNK,
                                     BDD::symbex::FN_BORROW_SECRET});
        auto prev_returns = get_prev_fn(ep, node, BDD::symbex::FN_RETURN_CHUNK);

        assert(prev_borrows.size());
        assert(prev_borrows.size() > prev_returns.size());

        auto target = prev_borrows[prev_returns.size()];

        auto call_node = BDD::cast_node<BDD::Call>(target);
        assert(call_node);

        auto call = call_node->get_call();

        assert(call.function_name == BDD::symbex::FN_BORROW_CHUNK ||
               call.function_name == BDD::symbex::FN_BORROW_SECRET);
        assert(!call.extra_vars[BDD::symbex::FN_BORROW_CHUNK_EXTRA]
                    .second.isNull());

        return call.extra_vars[BDD::symbex::FN_BORROW_CHUNK_EXTRA].second;
    }

    processing_result_t process(const ExecutionPlan &ep,
                                BDD::Node_ptr node) override {
        // TODO Check if this is a sum. If not, bail out

        // TODO Check if the previous node is a Conditional node. If not, bail
        // out

        // Get previous Conditional nodes
        //    std::vector<Module_ptr> previous_conditionals =
        //        get_prev_modules(ep, {ModuleType::tfhe_Conditional});
        //
        //    // Get only the nearest previous Conditional node
        //    Module_ptr previous_conditional = previous_conditionals[0];
        //
        //    // Build the expression for this arm.
        //    auto _current_chunk =
        //    call.args[BDD::symbex::FN_BORROW_CHUNK_EXTRA].in;
        //    // Is it a concatenation?
        //    assert(_current_chunk->getKind() == klee::Expr::Kind::Concat)
        //    klee:ref<klee:Expr> left_kid = _current_chunk.getKid(0);
        //
        //    // Is it an add, between two expression?
        //    assert(left_kid->getKind() == klee::Expr::Kind::Add);
        //    klee::ref<klee::Expr> left_add_expr = left_kid.getKid(0);
        //
        processing_result_t result;

        // Check if this is a valid return chunk
        auto casted = BDD::cast_node<BDD::Call>(node);

        if (!casted) {
            return result;
        }

        auto call = casted->get_call();

        if (call.function_name != BDD::symbex::FN_RETURN_CHUNK) {
            return result;
        }

        assert(!call.args[BDD::symbex::FN_BORROW_CHUNK_EXTRA].expr.isNull());
        assert(!call.args[BDD::symbex::FN_BORROW_CHUNK_EXTRA].in.isNull());

        auto _chunk = call.args[BDD::symbex::FN_BORROW_CHUNK_EXTRA].expr;
        auto _current_chunk = call.args[BDD::symbex::FN_BORROW_CHUNK_EXTRA].in;
        auto _original_chunk = get_original_chunk(ep, node);

        auto _chunk_addr = kutil::expr_addr_to_obj_addr(_chunk);
        auto _modifications =
            build_modifications(_original_chunk, _current_chunk);

        auto new_module = std::make_shared<TernarySum>(
            node, _chunk_addr, _original_chunk, _modifications);
        auto new_ep = ep.add_leaves(new_module, node->get_next());

        result.module = new_module;
        result.next_eps.push_back(new_ep);

        return result;
    }

public:
    virtual void visit(ExecutionPlanVisitor &visitor,
                       const ExecutionPlanNode *ep_node) const override {
        visitor.visit(ep_node, this);
    }

    virtual Module_ptr clone() const override {
        auto cloned =
            new TernarySum(node, chunk_addr, original_chunk, modifications);
        return std::shared_ptr<Module>(cloned);
    }

    virtual bool equals(const Module *other) const override {
        if (other->get_type() != type) {
            return false;
        }

        auto other_cast = static_cast<const TernarySum *>(other);

        if (chunk_addr != other_cast->get_chunk_addr()) {
            return false;
        }

        if (!kutil::solver_toolbox.are_exprs_always_equal(
                original_chunk, other_cast->original_chunk)) {
            return false;
        }

        auto other_modifications = other_cast->get_modifications();

        if (modifications.size() != other_modifications.size()) {
            return false;
        }

        for (unsigned i = 0; i < modifications.size(); i++) {
            auto modification = modifications[i];
            auto other_modification = other_modifications[i];

            if (modification.byte != other_modification.byte) {
                return false;
            }

            if (!kutil::solver_toolbox.are_exprs_always_equal(
                    modification.expr, other_modification.expr)) {
                return false;
            }
        }

        return true;
    }

    const addr_t &get_chunk_addr() const { return chunk_addr; }

    klee::ref<klee::Expr> get_original_chunk() const { return original_chunk; }

    const std::vector<modification_t> &get_modifications() const {
        return modifications;
    }

    std::string to_string_aux(klee::ref<klee::Expr> expr) const {
        std::string str;
        llvm::raw_string_ostream _s(str);

        if (expr->getNumKids() > 0) {
            _s << std::string("(");
            for (unsigned kid_i = 0; kid_i < expr->getNumKids(); kid_i++) {
                _s << expr->getKid(kid_i)->getKind();
                _s << to_string_aux(expr->getKid(kid_i));
                if (kid_i < expr->getNumKids() - 1) _s << std::string(", ");
            }
            _s << std::string(")");
        }

        return _s.str();
    }

    std::string generate_code() const {
        std::cout << "Modifications width: "
                  << this->get_modifications()[0].expr->getWidth() << std::endl;
        return generate_tfhe_code(this->get_modifications()[0].expr);
    }

    // Debug representation of the operations module
    std::string to_string() const {
        std::string str;
        llvm::raw_string_ostream s(str);
        s << std::string("modifications amount: ")
          << this->get_modifications().size() << std::string(" - ");
        s << "< ";
        for (auto &&_modf : this->get_modifications()) {
            klee::ref<klee::Expr> _expr = _modf.expr;
            // print expression kind
            s << _expr->getKind();
            s << this->to_string_aux(_expr);
            if (&_modf != &this->get_modifications().back())
                s << std::string(", ");
        }
        s << " >";
        return s.str();
    }
};
}  // namespace tfhe
}  // namespace targets
}  // namespace synapse
