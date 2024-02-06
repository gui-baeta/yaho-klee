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

        // TODO Check if the previous node is a Conditional node. If not, bail
        // out
        auto ep_node = ep.get_active_leaf();
        auto prev_ep_node =
            ep_node
                ->get_prev();  // Get only the nearest previous Conditional node
        if (!((ep_node->get_module()->get_type() == ModuleType::tfhe_Else ||
               ep_node->get_module()->get_type() == ModuleType::tfhe_Then) &&
              prev_ep_node->get_module()->get_type() ==
                  ModuleType::tfhe_Conditional)) {
            return result;
        }

        // TODO Check if this is a sum. If not, bail out

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

    /// Helper function that organises the given modifications in rising order.
    /// It searches recursively DFS like, from left to right, for a Read
    /// Expression.
    ///     The given modification expression is assigned to the same index as
    ///     the ReadExpr's index value.
    void process_modifications(
        klee::ref<klee::Expr> &modification_expr,
        std::vector<klee::ref<klee::Expr>> &modifications_per_value,
        klee::ref<klee::Expr> &_this_expr) const {
        if (_this_expr->getKind() == klee::Expr::Kind::Read) {
            klee::ConstantExpr *constExpr =
                dyn_cast<klee::ConstantExpr>(_this_expr->getKid(0));

            // Get the index of the value being read by the Read Expression
            unsigned index = constExpr->getZExtValue();

            // Use the index to assign the current modification expression to
            // the vector
            modifications_per_value.at(index) = modification_expr;
        } else {
            // Recursively search for a Read expression with priority on the
            // left child
            // FIXME I think this works by accident!
            //  The left child is always the one that is first checked.
            //  Let's say we have "a = b + c".
            //      This a value will be expressed in the form of:
            //      "a + (b + c)".
            //      So, even in this case, the index to be fetched will be a's
            //      index!
            for (unsigned i = 0; i < _this_expr->getNumKids(); ++i) {
                klee::ref<klee::Expr> kid = _this_expr->getKid(i);
                if (!kid.isNull()) {
                    process_modifications(modification_expr,
                                          modifications_per_value, kid);
                }
            }
        }
    }

    // Recursive function to search for the specified pattern
    void collect_modification_exprs(
        const klee::ref<klee::Expr> &expr,
        std::vector<klee::ref<klee::Expr>> &modifications) const {
        switch (expr->getKind()) {
        case klee::Expr::Kind::Extract:
            // Check if this is the initial Extract wrapper,
            // and start going down the Expression tree
            // TODO Is this enough??
            if (expr->getKid(0)->getKind() == klee::Expr::Kind::Concat) {
                collect_modification_exprs(expr->getKid(0), modifications);
                return;
            }
            // Check if the Extract has a child arithmetic expression
            // Modifications are wrapped by x2 Extract's, so we need to take
            // that into account
            if (expr->getKid(0)->getKind() == klee::Expr::Kind::Extract) {
                auto inner_extract = expr->getKid(0);
                /* TODO Add more arithmetic operations as needed */
                if (inner_extract->getKid(0)->getKind() ==
                        klee::Expr::Kind::Add ||
                    inner_extract->getKid(0)->getKind() ==
                        klee::Expr::Kind::Sub) {
                    modifications.push_back(inner_extract->getKid(0));
                }
            }
            break;
        case klee::Expr::Kind::Concat:
            // Check if the left, or right, child of Concat is a Read (value
            // with no modification)
            if (expr->getKid(0)->getKind() == klee::Expr::Kind::Read ||
                expr->getKid(1)->getKind() == klee::Expr::Kind::Read) {
                // If so, return null
                return;
            }

            // Dive into the left side of Concat
            collect_modification_exprs(expr->getKid(0), modifications);

            // Dive into the right side of Concat
            collect_modification_exprs(expr->getKid(1), modifications);

            break;
        // Add more cases for other expression kinds as needed
        default:
            break;
        }

        return;  // Finished pattern matching
    }

    // FIXME Is this API to get all modifications necessary?
    //  After optimizing obtaining only the needed modification, will we need this?
    // Extracts the modifications expression for each value, based on the
    // expressions in the modifications vector
    std::vector<klee::ref<klee::Expr>> get_modifications_exprs() const {
        int n_values = this->get_chunk_values_amount();

        // A klee:ref starts as null. (See its default constructor)
        std::vector<klee::ref<klee::Expr>> modifications_per_value(n_values);

        std::cout << "modifications to_string: " << this->to_string()
                  << std::endl;

        // If there are no modifications in this module, assign null to each
        // value
        // FIXME Does this work?
        if (this->get_modifications().empty()) {
            return modifications_per_value;
        }

        klee::ref<klee::Expr> first_bit_modifications_expr =
            this->get_modifications()[0].expr;

        // Collect all modification expressions
        std::vector<klee::ref<klee::Expr>> all_modifications;
        // Search for modifications to the values in the expression
        collect_modification_exprs(first_bit_modifications_expr,
                                   all_modifications);

        // Organize the modifications in expected values sequence [0th, 1st,
        // 2nd, ...]
        for (auto &expr : all_modifications) {
            process_modifications(expr, modifications_per_value, expr);
        }

        std::cout << "Done processing and filling modifications_per_value"
                  << std::endl;
        std::cout << "modifications_per_value size: "
                  << modifications_per_value.size() << std::endl;
        for (auto &_expr : modifications_per_value) {
            if (_expr.isNull()) {
                std::cout << "null" << std::endl;
                continue;
            }
            std::cout << "modifications_per_value: "
                      << generate_tfhe_code(_expr) << std::endl;
        }

        return modifications_per_value;
    }

    std::string generate_code() const {
        return generate_tfhe_code(this->get_modifications()[0].expr);
    }

    // TODO We should optimize this API. It should only look for the modification of the n-th value
    //  instead of getting all modifications.
    klee::ref<klee::Expr> get_modification_of(int n_value) const {
        auto modifications_exprs = this->get_modifications_exprs();
        return modifications_exprs.at(n_value);
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
                s << std::string(",_");
        }
        s << " >";
        return s.str();
    }

    unsigned get_chunk_width() const {
        return this->original_chunk->getWidth();
    }

    // FIXME This is specific to 8-bit chunks
    int get_chunk_values_amount() const { return this->get_chunk_width() / 8; }
};
}  // namespace tfhe
}  // namespace targets
}  // namespace synapse
