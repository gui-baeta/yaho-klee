#pragma once

#include "tfhe_module.h"

#include "../../../tfhe_generate_code.h"

#include "else.h"
#include "then.h"

namespace synapse {
namespace targets {
namespace tfhe {

/* If given the following code:
 * let c = 0
 * if a > 5 {
 *    c += 1
 * } else {
 *    c += 2
 * }
 *
 * And, considering that the final outputted code will look something like:
 *
 * cond_val = (a.gt(5))
 * c = cond_val*(c + 1) + (not cond_val)*(c + 2)
 *
 * This Module tries to implement the condition evaluation:
 *
 * - cond_val = (a.gt(5))
 *
 * The responsibility of tfhe_Condition is to save the condition and try
 * to implement it.
 *
 * The rest, is the responsibility of other Modules
 *
 * In a nutshell, what we want is to divide this problem like so:
 *
 * # module: Conditional
 * 1. cond_val = (a > 5)
 *
 * # module: Ternary Sum - Save the level/scope, so that the leaf
 * #                       "on the other side"/Else Node can see this
 * #                        half of the ternary condition and complete it
 * 2.a: cond_val * (c + 2) + <nop>
 *
 * # "Doesn't see the light of day."
 * #    This module gets processed and modifies the other one (above).
 * #    Does not create a new EP!
 * #      Check what existed at the same level "on the other side"/Then Node
 * #       and complete the operation
 * 2.b: ~ (not cond_val) * c
 *
 * # module: Ternary Sum - The complete form of (2.).
 * #    Joint work between (2.a) and (2.b)
 * 2.: c = cond_val * (c + 2) + (not cond_val) * c
 *
 */
class SinglePBS : public tfheModule {
private:
    klee::ref<klee::Expr> condition;
    klee::ref<klee::Expr> then_modification;
    klee::ref<klee::Expr> else_modification;

public:
    SinglePBS() : tfheModule(ModuleType::tfhe_SinglePBS, "SinglePBS") {}

    SinglePBS(BDD::Node_ptr node, klee::ref<klee::Expr> _condition,
              klee::ref<klee::Expr> _then_modification,
              klee::ref<klee::Expr> _else_modification)
        : tfheModule(ModuleType::tfhe_SinglePBS, "SinglePBS", node),
          condition(_condition),
          then_modification(_then_modification),
          else_modification(_else_modification) {}

private:
    processing_result_t process(const ExecutionPlan &ep,
                                BDD::Node_ptr node) override {
        processing_result_t result;

        // Check if it's a branch node
        const BDD::Branch *branch_node = BDD::cast_node<BDD::Branch>(node);
        if (!branch_node) {
            return result;
        }
        // At this point, we know it's a branch
        assert(!branch_node->get_condition().isNull());
        klee::ref<klee::Expr> _condition = branch_node->get_condition();

        std::cout << "In branch" << std::endl;

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
            std::vector<BDD::Node_ptr> prev_borrows =
                get_prev_fn(ep, node, std::vector<std::string>{BDD::symbex::FN_BORROW_SECRET});
            // There should be only one borrow!
            assert(prev_borrows.size() == 1);
            BDD::Node_ptr borrow_node = prev_borrows.at(0);
            auto call_node = BDD::cast_node<BDD::Call>(borrow_node);
            call_t call = call_node->get_call();
            assert(call.function_name == BDD::symbex::FN_BORROW_SECRET);
            assert(!call.extra_vars[BDD::symbex::FN_BORROW_CHUNK_EXTRA].second.isNull());

            auto _chunk =
                call.extra_vars[BDD::symbex::FN_BORROW_CHUNK_EXTRA].second;
            auto chunk_width = _chunk->getWidth();
            // FIXME This is specific for 1 Byte values
            //  and will function wrongly if each value is not 1 Byte
            number_of_values = chunk_width / 8;
        }

        std::cout << "Number of values: " << std::to_string(number_of_values)
                  << std::endl;

        std::shared_ptr<TernarySum> empty_operations_module =
            std::make_shared<TernarySum>();
        std::cout << "1" << std::endl;
        std::shared_ptr<TernarySum> _on_true_module =
            empty_operations_module->inflate(ep, branch_node->get_on_true());
        std::cout << "2" << std::endl;
        std::shared_ptr<TernarySum> _on_false_module =
            empty_operations_module->inflate(ep, branch_node->get_on_false());
        std::cout << "Inflated both Operations modules" << std::endl;

        typedef klee::ref<klee::Expr> expr_ref;

        std::vector<expr_ref> on_true_modifications =
            _on_true_module->get_modifications_exprs();
        std::vector<expr_ref> on_false_modifications =
            _on_false_module->get_modifications_exprs();

        std::cout << "Got the modifications for each child" << std::endl;

        for (int i = 0; i < number_of_values; ++i) {
            std::cout << "-- Value " << std::to_string(i) << ":" << std::endl;
            expr_ref on_true_mod = on_true_modifications[i];
            expr_ref on_false_mod = on_false_modifications[i];

            std::cout << "(" << generate_tfhe_code(on_true_mod) << ")"
                      << std::endl;
            std::cout << "(" << generate_tfhe_code(on_false_mod) << ")"
                      << std::endl;

            if (on_true_mod.isNull() && on_false_mod.isNull()) {
                std::cout << "Both modifications are null" << std::endl;
            } else if (kutil::solver_toolbox.are_exprs_always_equal(
                           on_true_mod, on_false_mod)) {
                std::cout << "Both modifications are equal" << std::endl;
                // TODO Add a Change module
                //  Should this be done by the Change module itself?
            } else {
                std::cout << "Both modifications are different" << std::endl;
                auto new_single_pbs = std::make_shared<SinglePBS>(
                    node, _condition, klee::ref<klee::Expr>(on_true_mod),
                    klee::ref<klee::Expr>(on_false_mod));
                ExecutionPlan new_ep =
                    ep.add_leaf(new_single_pbs, nullptr, true);

                result.module = new_single_pbs;
                result.next_eps.push_back(new_ep);
                return result;
            }

            // TODO Check for differences in modifications from both sides.
            //     - If they are the same, we add a "Change" module above this
            //     one
            //          * We need to mark, somehow, that this value is already
            //          dealt with!!
            //     - If there are differences, we spit out a SinglePBS module
            //     and mark this value as dealt with
            //                                           so to leave the rest of
            //                                           the values for the next
            //                                           module
        }

        return result;
    }

public:
    virtual void visit(ExecutionPlanVisitor &visitor,
                       const ExecutionPlanNode *ep_node) const override {
        visitor.visit(ep_node, this);
    }

    virtual Module_ptr clone() const override {
        auto cloned =
            new SinglePBS(node, this->condition, this->then_modification,
                          this->else_modification);
        return std::shared_ptr<Module>(cloned);
    }

    virtual bool equals(const Module *other) const override {
        if (other->get_type() != type) {
            return false;
        }

        auto other_cast = static_cast<const SinglePBS *>(other);

        if (!(kutil::solver_toolbox.are_exprs_always_equal(
                  condition, other_cast->get_condition()) &&
              kutil::solver_toolbox.are_exprs_always_equal(
                  then_modification, other_cast->get_then_modification()) &&
              kutil::solver_toolbox.are_exprs_always_equal(
                  else_modification, other_cast->get_else_modification()))) {
            return false;
        }

        auto other_then_modification = other_cast->get_then_modification();
        auto other_else_modification = other_cast->get_else_modification();

        if (!kutil::solver_toolbox.are_exprs_always_equal(
                then_modification, other_then_modification)) {
            return false;
        }

        if (!kutil::solver_toolbox.are_exprs_always_equal(
                else_modification, other_else_modification)) {
            return false;
        }

        return true;
    }

    const klee::ref<klee::Expr> &get_condition() const {
        return this->condition;
    }

    const klee::ref<klee::Expr> &get_then_modification() const {
        return this->then_modification;
    }

    const klee::ref<klee::Expr> &get_else_modification() const {
        return this->else_modification;
    }

    //
    //    std::string generate_code(bool needs_cloning = true) const {
    //        // Get the condition type
    //        klee::Expr::Kind conditionType = this->condition->getKind();
    //
    //        std::cout << this->to_string() << std::endl;
    //
    //        // Initialize the Rust code string as the unit
    //        std::string code = "()";
    //
    //        // Generate the corresponding Rust code based on the condition
    //        type switch (conditionType) {
    //        // Case for handling equality expressions.
    //        case klee::Expr::Eq:
    //            code = generate_tfhe_code(this->condition->getKid(1),
    //            needs_cloning)
    //                   + std::string(".eq(")
    //                   + generate_tfhe_code(this->condition->getKid(0),
    //                   needs_cloning) + std::string(")");
    //            break;
    //        // Case for handling unsigned less-than expressions.
    //        case klee::Expr::Ult:
    //            code = generate_tfhe_code(this->condition->getKid(1),
    //            needs_cloning)
    //                   + std::string(".ge(")
    //                   + generate_tfhe_code(this->condition->getKid(0),
    //                   needs_cloning) + std::string(")");
    //            break;
    //
    //        // Case for handling unsigned less-than-or-equal-to expressions.
    //        case klee::Expr::Ule:
    //            code = generate_tfhe_code(this->condition->getKid(1),
    //            needs_cloning)
    //                   + std::string(".gt(")
    //                   + generate_tfhe_code(this->condition->getKid(0),
    //                   needs_cloning) + std::string(")");
    //            break;
    //
    //        // Case for handling unsigned greater-than expressions.
    //        case klee::Expr::Ugt:
    //            code = generate_tfhe_code(this->condition->getKid(1),
    //            needs_cloning)
    //                   + std::string(".le(")
    //                   + generate_tfhe_code(this->condition->getKid(0),
    //                   needs_cloning) + std::string(")");
    //            break;
    //
    //        // Fallthrough to Uge since there is no signed values in TFHE
    //        case klee::Expr::Sge:
    //            // TODO Look at
    //            https://www.zama.ai/post/releasing-tfhe-rs-v0-4-0
    //            //  and see how to use signed comparisons
    //            code = generate_tfhe_code(this->condition->getKid(1),
    //            needs_cloning)
    //                   + std::string(".lt(")
    //                   + generate_tfhe_code(this->condition->getKid(0),
    //                   needs_cloning) + std::string(")");
    //            break;
    //        // Case for handling unsigned greater-than-or-equal-to
    //        expressions. case klee::Expr::Uge:
    //            code = generate_tfhe_code(this->condition->getKid(1),
    //            needs_cloning)
    //                   + std::string(".lt(")
    //                   + generate_tfhe_code(this->condition->getKid(0),
    //                   needs_cloning) + std::string(")");
    //            break;
    //
    //        // Case for handling signed less-than expressions.
    //        case klee::Expr::Slt:
    //            code = generate_tfhe_code(this->condition->getKid(1),
    //            needs_cloning)
    //                   + std::string(".ge(")
    //                   + generate_tfhe_code(this->condition->getKid(0),
    //                   needs_cloning) + std::string(")");
    //            break;
    //
    //        // Case for handling signed less-than-or-equal-to expressions.
    //        case klee::Expr::Sle:
    //            code = generate_tfhe_code(this->condition->getKid(1),
    //            needs_cloning)
    //                   + std::string(".gt(")
    //                   + generate_tfhe_code(this->condition->getKid(0),
    //                   needs_cloning) + std::string(")");
    //            break;
    //        // TODO Add more cases as needed for other condition types
    //        default:
    //            std::cerr << "Unsupported condition type: " << conditionType
    //                      << std::endl;
    //            exit(1);
    //        }
    //
    //        return code;
    //    }
    //
    //    std::string to_string() const {
    //        std::string str;
    //        llvm::raw_string_ostream s(str);
    //        this->get_condition()->print(s);
    //        return s.str();
    //    }
    //
    //    friend std::ostream &operator<<(std::ostream &os,
    //                                    const SinglePBS &single_pbs) {
    //        std::string str;
    //        llvm::raw_string_ostream s(str);
    //        conditional.get_condition()->print(s);
    //        os << s.str();
    //        return os;
    //    }
};
}  // namespace tfhe
}  // namespace targets
}  // namespace synapse
