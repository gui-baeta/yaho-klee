#pragma once

#include "tfhe_module.h"

#include "../../../tfhe_generate_code.h"

#include "else.h"
#include "then.h"

namespace synapse {
namespace targets {
namespace tfhe {

/// Flattened if-then-else
class If : public tfheModule {
private:
    klee::ref<klee::Expr> condition;

public:
    If() : tfheModule(ModuleType::tfhe_If, "If") {}

    If(BDD::Node_ptr node, klee::ref<klee::Expr> _condition)
        : tfheModule(ModuleType::tfhe_If, "If", node), condition(_condition) {}

private:
    processing_result_t process(const ExecutionPlan &ep,
                                BDD::Node_ptr node) override {
        processing_result_t result;

        // FIXME This is just for testing.
        //  This makes SyNAPSE skip this Module
        return result;

        // Check if it's a branch node
        const BDD::Branch *branch_node = BDD::cast_node<BDD::Branch>(node);
        if (!branch_node) {
            return result;
        }
        // At this point, we know it's a branch
        assert(!branch_node->get_condition().isNull());
        klee::ref<klee::Expr> _condition =
            klee::ref<klee::Expr>(branch_node->get_condition());

        std::vector<int> values_in_condition = get_dependent_values(_condition);
        std::cout
            << "----------------------- Condition values ------------------"
            << std::endl;
        for (auto &val : values_in_condition) {
            std::cout << "Dependent value: " << val << std::endl;
        }
        std::cout << "-----------------------------------------" << std::endl;

        // Check if this condition depends on only one value
        assert(values_in_condition.size() == 1);

        // Save the value this condition depends on
        int _value_in_condition = values_in_condition.at(0);

        // Bail out if any of the children is a branch/conditional
        if (branch_node->get_on_true()->get_type() ==
                BDD::Node::NodeType::BRANCH ||
            branch_node->get_on_false()->get_type() ==
                BDD::Node::NodeType::BRANCH) {
            return result;
        }
        std::cout << "None of the Children are branches" << std::endl;
        // We can assume that the children are of the type PacketReturnChunk

        int number_of_values = 0;
        {
            // Find the call for the chunks borrow,
            //  by checking for its function_name in the BDD
            std::vector<BDD::Node_ptr> prev_borrows = get_prev_fn(
                ep, node,
                std::vector<std::string>{BDD::symbex::FN_BORROW_SECRET});
            // There should be only one borrow!
            assert(prev_borrows.size() == 1);
            BDD::Node_ptr borrow_node = prev_borrows.at(0);
            auto call_node = BDD::cast_node<BDD::Call>(borrow_node);
            call_t call = call_node->get_call();
            assert(call.function_name == BDD::symbex::FN_BORROW_SECRET);
            assert(!call.extra_vars[BDD::symbex::FN_BORROW_CHUNK_EXTRA]
                        .second.isNull());

            auto _chunk =
                call.extra_vars[BDD::symbex::FN_BORROW_CHUNK_EXTRA].second;
            auto chunk_width = _chunk->getWidth();
            // FIXME This is specific for 1 Byte values
            //  and will function wrongly if each value is not 1 Byte
            number_of_values = chunk_width / 8;
        }

        std::shared_ptr<Operation> empty_operations_module =
            std::make_shared<Operation>();
        std::shared_ptr<Operation> _on_true_module =
            empty_operations_module->inflate(ep, branch_node->get_on_true());
        std::shared_ptr<Operation> _on_false_module =
            empty_operations_module->inflate(ep, branch_node->get_on_false());

        typedef klee::ref<klee::Expr> expr_ref;

        std::vector<expr_ref> on_true_modifications =
            _on_true_module->get_modifications_exprs();
        std::vector<expr_ref> on_false_modifications =
            _on_false_module->get_modifications_exprs();

        for (int n = 0; n < number_of_values; ++n) {
            std::cout << "-- Value " << std::to_string(n) << ":" << std::endl;
            expr_ref on_true_mod = on_true_modifications[n];
            expr_ref on_false_mod = on_false_modifications[n];

            std::cout << "(" << generate_tfhe_code(on_true_mod) << ")"
                      << std::endl;
            std::cout << "(" << generate_tfhe_code(on_false_mod) << ")"
                      << std::endl;

            // TODO
            //  * We need to mark, somehow, that this value is already dealt
            //  with!!

            if (on_true_mod.isNull() && on_false_mod.isNull()) {
                std::cout << "Both modifications are null" << std::endl;
                /* No operations needed since both are null,
                 *
                 *  We continue to the next pair of values */
            } else if (kutil::solver_toolbox.are_exprs_always_equal(
                           on_true_mod, on_false_mod)) {
                std::cout << "Both modifications are equal" << std::endl;
                // TODO Add a Change module
                //  Should this be done by the Change module, and not by the
                //  UnivariatePBS?
            } else {
                std::cout << "Both modifications are different" << std::endl;

                int _changed_value = n;

                auto new_if = std::make_shared<If>(
                    node, _condition);

                // TODO If this value is not the last one to be iterated,
                //  We DON'T mark it as "is_terminal".
                //  We pass the argument "processed_bdd_node" as false and mark
                //  this value as solved,
                //      so the next module knows it must
                //      take care of the next value
                ExecutionPlan new_ep = ep.add_leaf(new_if, nullptr, true, true);

                result.module = new_if;
                result.next_eps.push_back(new_ep);
                return result;
            }
        }

        return result;
    }

public:
    virtual void visit(ExecutionPlanVisitor &visitor,
                       const ExecutionPlanNode *ep_node) const override {
        visitor.visit(ep_node, this);
    }

    virtual Module_ptr clone() const override {
        auto cloned = new If(node, condition);
        return std::shared_ptr<Module>(cloned);
    }

    virtual bool equals(const Module *other) const override {
        if (other->get_type() != type) {
            return false;
        }

        auto other_cast = static_cast<const If *>(other);

        if (!kutil::solver_toolbox.are_exprs_always_equal(
                condition, other_cast->get_condition())) {
            return false;
        }

        return true;
    }

    const klee::ref<klee::Expr> &get_condition() const { return condition; }
};
}  // namespace tfhe
}  // namespace targets
}  // namespace synapse
