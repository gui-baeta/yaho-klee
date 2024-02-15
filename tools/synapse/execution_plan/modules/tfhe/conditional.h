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
class Conditional : public tfheModule {
private:
    // Collection of all the binary conditions to evaluate
    klee::ref<klee::Expr> condition;

public:
    Conditional() : tfheModule(ModuleType::tfhe_Conditional, "Conditional") {}

    Conditional(BDD::Node_ptr node, klee::ref<klee::Expr> _condition)
        : tfheModule(ModuleType::tfhe_Conditional, "Conditional", node),
          condition(_condition) {}

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

        // We want to store the if condition
        assert(!branch_node->get_condition().isNull());
        // If condition: Expression
        auto _condition = branch_node->get_condition();
        // TODO See later if this is enough. For now this will do.
        //    this->condition = _condition;
        // Then block: node
        auto _then_node = branch_node->get_on_true();
        // Else block: node
        auto _else_node = branch_node->get_on_false();

        auto new_flat_if_module =
            std::make_shared<Conditional>(node, _condition);
        auto new_then_module = std::make_shared<Then>(node);
        auto new_else_module = std::make_shared<Else>(node);

        auto flat_if_leaf = ExecutionPlan::leaf_t(new_flat_if_module, nullptr);
        auto then_leaf = ExecutionPlan::leaf_t(new_then_module, _then_node);
        auto else_leaf = ExecutionPlan::leaf_t(new_else_module, _else_node);

        std::vector<ExecutionPlan::leaf_t> branch_leaves{flat_if_leaf};
        std::vector<ExecutionPlan::leaf_t> then_else_leaves{then_leaf,
                                                            else_leaf};

        auto ep_branch = ep.add_leaves(branch_leaves);
        auto ep_flat_if_then_else = ep_branch.add_leaves(then_else_leaves);

        result.module = new_flat_if_module;
        result.next_eps.push_back(ep_flat_if_then_else);

        return result;
    }

public:
    virtual void visit(ExecutionPlanVisitor &visitor,
                       const ExecutionPlanNode *ep_node) const override {
        visitor.visit(ep_node, this);
    }

    virtual Module_ptr clone() const override {
        auto cloned = new Conditional(node, condition);
        return std::shared_ptr<Module>(cloned);
    }

    virtual bool equals(const Module *other) const override {
        if (other->get_type() != type) {
            return false;
        }

        auto other_cast = static_cast<const Conditional *>(other);

        if (!kutil::solver_toolbox.are_exprs_always_equal(
                condition, other_cast->get_condition())) {
            return false;
        }

        return true;
    }

    const klee::ref<klee::Expr> &get_condition() const { return condition; }

    std::string generate_code(bool needs_cloning = true) const {
        // Get the condition type
        klee::Expr::Kind conditionType = this->condition->getKind();

        std::cout << this->to_string() << std::endl;

        // Initialize the Rust code string as the unit
        std::string code = "()";

        // Generate the corresponding Rust code based on the condition type
        switch (conditionType) {
        // Case for handling equality expressions.
        case klee::Expr::Eq:
            code = generate_tfhe_code(this->condition->getKid(1), needs_cloning)
                   + std::string(".eq(")
                   + generate_tfhe_code(this->condition->getKid(0), needs_cloning) + std::string(")");
            break;
        // Case for handling unsigned less-than expressions.
        case klee::Expr::Ult:
            code = generate_tfhe_code(this->condition->getKid(1), needs_cloning)
                   + std::string(".ge(")
                   + generate_tfhe_code(this->condition->getKid(0), needs_cloning) + std::string(")");
            break;

        // Case for handling unsigned less-than-or-equal-to expressions.
        case klee::Expr::Ule:
            code = generate_tfhe_code(this->condition->getKid(1), needs_cloning)
                   + std::string(".gt(")
                   + generate_tfhe_code(this->condition->getKid(0), needs_cloning) + std::string(")");
            break;

        // Case for handling unsigned greater-than expressions.
        case klee::Expr::Ugt:
            code = generate_tfhe_code(this->condition->getKid(1), needs_cloning)
                   + std::string(".le(")
                   + generate_tfhe_code(this->condition->getKid(0), needs_cloning) + std::string(")");
            break;

        // Fallthrough to Uge since there is no signed values in TFHE
        case klee::Expr::Sge:
            // TODO Look at https://www.zama.ai/post/releasing-tfhe-rs-v0-4-0
            //  and see how to use signed comparisons
            code = generate_tfhe_code(this->condition->getKid(1), needs_cloning)
                   + std::string(".lt(")
                   + generate_tfhe_code(this->condition->getKid(0), needs_cloning) + std::string(")");
            break;
        // Case for handling unsigned greater-than-or-equal-to expressions.
        case klee::Expr::Uge:
            code = generate_tfhe_code(this->condition->getKid(1), needs_cloning)
                   + std::string(".lt(")
                   + generate_tfhe_code(this->condition->getKid(0), needs_cloning) + std::string(")");
            break;

        // Case for handling signed less-than expressions.
        case klee::Expr::Slt:
            code = generate_tfhe_code(this->condition->getKid(1), needs_cloning)
                   + std::string(".ge(")
                   + generate_tfhe_code(this->condition->getKid(0), needs_cloning) + std::string(")");
            break;

        // Case for handling signed less-than-or-equal-to expressions.
        case klee::Expr::Sle:
            code = generate_tfhe_code(this->condition->getKid(1), needs_cloning)
                   + std::string(".gt(")
                   + generate_tfhe_code(this->condition->getKid(0), needs_cloning) + std::string(")");
            break;
        // TODO Add more cases as needed for other condition types
        default:
            std::cerr << "Unsupported condition type: " << conditionType
                      << std::endl;
            exit(1);
        }

        return code;
    }

    std::string to_string() const {
        std::string str;
        llvm::raw_string_ostream s(str);
        this->get_condition()->print(s);
        return s.str();
    }

    friend std::ostream &operator<<(std::ostream &os,
                                    const Conditional &conditional) {
        std::string str;
        llvm::raw_string_ostream s(str);
        conditional.get_condition()->print(s);
        os << s.str();
        return os;
    }
};
}  // namespace tfhe
}  // namespace targets
}  // namespace synapse
