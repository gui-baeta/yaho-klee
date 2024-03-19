#pragma once

#include "tfhe_module.h"

#include "../../../tfhe_generate_code.h"

#include "else.h"
#include "then.h"

namespace synapse {
namespace targets {
namespace tfhe {

class UnivariatePBS : public tfheModule {
private:
    klee::ref<klee::Expr> condition;
    klee::ref<klee::Expr> then_modification;
    klee::ref<klee::Expr> else_modification;

    /// The value that was changed
    int changed_value = 0;
    /// The condition depends on this value
    int value_in_condition = 0;

public:
    UnivariatePBS() : tfheModule(ModuleType::tfhe_UnivariatePBS, "UnivariatePBS") {}

    UnivariatePBS(BDD::Node_ptr node, klee::ref<klee::Expr> _condition,
            klee::ref<klee::Expr> _then_modification,
            klee::ref<klee::Expr> _else_modification, int _changed_value,
            int _value_in_condition)
        : tfheModule(ModuleType::tfhe_UnivariatePBS, "UnivariatePBS", node),
          condition(_condition),
          then_modification(_then_modification),
          else_modification(_else_modification),
          changed_value(_changed_value),
          value_in_condition(_value_in_condition) {}

private:
    processing_result_t process(const ExecutionPlan &ep,
                                BDD::Node_ptr node) override {
        processing_result_t result;

        const BDD::node_id_t this_node_id = node->get_id();

        // TODO This module expects that the Branch Node has
        // "packet_return_chunk" as children.
        //  This should be corrected

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

                // Return a branch if both the children are a branch
        if (branch_node->get_on_true()->get_type() ==
                BDD::Node::NodeType::BRANCH &&
            branch_node->get_on_false()->get_type() ==
                BDD::Node::NodeType::BRANCH) {

            std::cout << "On Univariate PBS: Both children are branches. "
                         "Univariate PBS can't be used when both children are branches" << std::endl;

            return result;
        }

        if (branch_node->get_on_true()->get_type() == BDD::Node::NodeType::BRANCH ||
            branch_node->get_on_false()->get_type() == BDD::Node::NodeType::BRANCH) {
            std::cerr << "On Univariate PBS: One children is a branch and another isn't. Not supported yet" << std::endl;
            exit(2);
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

        const BDD::Node_ptr on_true_node = branch_node->get_on_true();
        const BDD::Node_ptr on_false_node = branch_node->get_on_false();

        // TODO * Expects that the children are of the type PacketReturnChunk
        std::shared_ptr<Operation> empty_operations_module =
            std::make_shared<Operation>();
        std::shared_ptr<Operation> _on_true_module =
            empty_operations_module->inflate(ep, on_true_node);
        std::shared_ptr<Operation> _on_false_module =
            empty_operations_module->inflate(ep, on_false_node);

        typedef klee::ref<klee::Expr> expr_ref;

        // TODO * Expects that the children are of the type PacketReturnChunk
        std::vector<expr_ref> on_true_modifications =
            _on_true_module->get_modifications_exprs();
        std::vector<expr_ref> on_false_modifications =
            _on_false_module->get_modifications_exprs();

//        ExecutionPlan new_ep = ep.clone();
//        Module_ptr new_module;
//
//        const int n = new_ep.get_next_value_of_node(this_node_id);
//        expr_ref on_true_expr = on_true_modifications[n];
//        expr_ref on_false_expr = on_false_modifications[n];
//
//        std::cout << "-- Value " << std::to_string(n) << "/"
//                  << number_of_values - 1 << ":" << std::endl;


        auto new_branch_module = std::make_shared<UnivariatePBS>(node, _condition, nullptr, nullptr, -1, _value_in_condition);
        auto new_then_module = std::make_shared<Then>(node);
        auto new_else_module = std::make_shared<Else>(node);

        auto then_leaf =
            ExecutionPlan::leaf_t(new_then_module, branch_node->get_on_true());
        auto else_leaf =
            ExecutionPlan::leaf_t(new_else_module, branch_node->get_on_false());

        std::vector<ExecutionPlan::leaf_t> then_else_leaves{then_leaf, else_leaf};

        std::shared_ptr<ExecutionPlan> _new_ep = nullptr;

        std::string parent_condition_name = ep.get_last_developed_node()->get_prev()->get_module_name();
        std::string parent_condition_case = ep.get_last_developed_node()->get_module_name();

        if (ep.get_last_developed_node()->get_module_type() == ModuleType::tfhe_Then ||
            ep.get_last_developed_node()->get_module_type() == ModuleType::tfhe_Else) {

            klee::ref<klee::Expr> parent_condition = ep.get_last_developed_node()->get_prev()->get_module_condition();
            std::cout << "Last developed node is a Then/Else" << std::endl;
//            _new_ep = std::make_shared<ExecutionPlan>(ep.replace_leaf(new_branch_module, nullptr, true));
            _new_ep = std::make_shared<ExecutionPlan>(ep.add_leaf(new_branch_module, nullptr, false, true));

        } else {
            _new_ep = std::make_shared<ExecutionPlan>(ep.add_leaf(new_branch_module, nullptr, false, true));
        }

        ExecutionPlan new_ep = _new_ep->add_leaves(then_else_leaves);

        result.module = new_branch_module;
        result.next_eps.push_back(new_ep);

        return result;

//        if (on_true_expr.isNull() && on_false_expr.isNull()) {
//            std::cout << "Both modifications are null" << std::endl;
//            /* No operations needed since both are null/non-existent,
//             *
//             *  We continue to the next pair of values */
//
//            int _unchanged_value = n;
//            std::cout << "///////////////////////////////////////// value: "
//                      << _unchanged_value << std::endl;
//            new_module = std::make_shared<NoChange>(node, _unchanged_value);
//
//            new_ep = new_ep.add_leaf(new_module, node, false, false);
//        } else if (kutil::solver_toolbox.are_exprs_always_equal(
//                       on_true_expr, on_false_expr)) {
//            /* Creates a Change Module when there is a modification to the
//             * value but no difference between branches */
//            std::cout << "Both modifications are equal" << std::endl;
//            /* Build a Change Module from the modification.
//             * Since both modifications are the same, we choose one
//             * arbitrarily */
//
//            int _changed_value = n;
//            std::cout << "///////////////////////////////////////// value: "
//                      << _changed_value << std::endl;
//
//            new_module = std::make_shared<Change>(
//                node, klee::ref<klee::Expr>(on_true_expr), _changed_value);
//
//            /* This node is not marked as terminal or processed since
//             * there could be other changes in other values */
//            new_ep = new_ep.add_leaf(new_module, node, false, false);
//        } else {
//            /* Creates a new UnivariatePBS Module when there is a difference
//             * between the same value */
//
//            std::cout << "Both modifications are different" << std::endl;
//
//            int _changed_value = n;
//            std::cout << "///////////////////////////////////////// value: "
//                      << _changed_value << std::endl;
//
//            if (_changed_value != _value_in_condition) {
//                std::cout << "Changed value is different from value in "
//                             "condition and a MonoBPS cannot be used. Another "
//                             "Module may be able to process this node."
//                          << std::endl;
//                return result;
//            }
//
//            new_module = std::make_shared<UnivariatePBS>(
//                node, _condition, klee::ref<klee::Expr>(on_true_expr),
//                klee::ref<klee::Expr>(on_false_expr), _changed_value,
//                _value_in_condition);
//
//            /* Marked as terminal because both children are modifications */
//            new_ep = new_ep.add_leaf(new_module, node, false, false);
//        }
//
//        /* Sets this node "next_value" as the next value to be taken care of.
//         *
//         * This NEEDS to be done before checking if all values are cared for
//         */
//        new_ep.mark_value_as_done_for_node(this_node_id);
//
//        if (new_ep.all_values_marked_for_node(this_node_id, number_of_values)) {
//            /* mark the node as processed */
//            new_ep.add_processed_bdd_node(node->get_id(), number_of_values);
//        }
//
//        // If no new module was created,
//        // it means no modification was seen for this specific value
//        if (!new_module) {
//            return result;
//        }
//
//        result.module = new_module;
//        result.next_eps.push_back(new_ep);
//
//        return result;
    }

public:
    virtual void visit(ExecutionPlanVisitor &visitor,
                       const ExecutionPlanNode *ep_node) const override {
        visitor.visit(ep_node, this);
    }

    virtual Module_ptr clone() const override {
        auto cloned =
            new UnivariatePBS(node, this->condition, this->then_modification,
                        this->else_modification, this->changed_value,
                        this->value_in_condition);
        return std::shared_ptr<Module>(cloned);
    }

    virtual bool equals(const Module *other) const override {
        if (other->get_type() != this->type) {
            return false;
        }

        auto other_cast = static_cast<const UnivariatePBS *>(other);

        if (!(kutil::solver_toolbox.are_exprs_always_equal(
                  this->condition, other_cast->get_condition()) &&
              kutil::solver_toolbox.are_exprs_always_equal(
                  this->then_modification,
                  other_cast->get_then_modification()) &&
              kutil::solver_toolbox.are_exprs_always_equal(
                  this->else_modification,
                  other_cast->get_else_modification()))) {
            return false;
        }

        auto other_then_modification = other_cast->get_then_modification();
        auto other_else_modification = other_cast->get_else_modification();

        if (!kutil::solver_toolbox.are_exprs_always_equal(
                this->then_modification, other_then_modification)) {
            return false;
        }

        if (!kutil::solver_toolbox.are_exprs_always_equal(
                this->else_modification, other_else_modification)) {
            return false;
        }

        if (this->changed_value != other_cast->get_changed_value()) {
            return false;
        }

        if (this->value_in_condition != other_cast->get_value_in_condition()) {
            return false;
        }

        return true;
    }

    const klee::ref<klee::Expr> &get_condition() const {
        return this->condition;
    }

    klee::ref<klee::Expr> get_expr() const override {
        return this->condition;
    }

    const klee::ref<klee::Expr> &get_then_modification() const {
        return this->then_modification;
    }

    const klee::ref<klee::Expr> &get_else_modification() const {
        return this->else_modification;
    }

    int get_changed_value() const { return this->changed_value; }

    int get_value_in_condition() const { return this->value_in_condition; }

    std::string changed_value_to_string(int changed_value) const {
        return std::string("val") + std::to_string(changed_value);
    }
    std::string changed_value_to_string() const {
        return std::string("val") + std::to_string(this->changed_value);
    }
    std::string value_in_condition_to_string() const {
        return std::string("val") + std::to_string(this->value_in_condition);
    }
    std::string condition_to_string() const {
        return generate_code(true, false);
    }
    std::string then_modification_to_string(bool needs_cloning = true) const {
        return generate_tfhe_code(this->then_modification, needs_cloning);
    }
    std::string else_modification_to_string(bool needs_cloning = true) const {
        return generate_tfhe_code(this->else_modification, needs_cloning);
    }

    std::string to_string(int changed_value, klee::ref<klee::Expr> then_operation, klee::ref<klee::Expr> else_operation) const {
        std::ostringstream s;

        std::string value = this->changed_value_to_string(changed_value);
        std::string condition_value = this->value_in_condition_to_string();
        // TODO I Could potentially flip this condition if there is no Then
        //  modification
        std::string condition = this->condition_to_string();
        std::string then_op = generate_tfhe_code(then_operation, false);
        std::string else_op = generate_tfhe_code(else_operation, false);

        if (!else_op.empty() && !then_op.empty()) {
            s << "let " << value << " = " << condition_value << ".map(|"
              << condition_value << "| if " << condition << " { " << then_op
              << " } else { " << else_op << " }";
        } else if (then_op.empty()) {
            s << "let " << value << " = " << condition_value << ".map(|"
              << condition_value << "| if " << condition << " { " << value
              << " } else { " << else_op << " }";
        } else if (else_op.empty()) {
            s << "let " << value << " = " << condition_value << ".map(|"
              << condition_value << "| if " << condition << " { " << then_op
              << " } else { " << value << " }";
        }

        s << ");" << std::endl;

        return s.str();
    }

    std::string to_string() const {
        std::ostringstream s;

        std::string value = this->changed_value_to_string();
        std::string condition_value = this->value_in_condition_to_string();
        // TODO I Could potentially flip this condition if there is no Then
        // modification
        std::string condition = this->condition_to_string();
        std::string then_result = this->then_modification_to_string(false);
        std::string else_result = this->else_modification_to_string(false);

        if (!else_result.empty() && !then_result.empty()) {
            s << "let " << value << " = " << condition_value << ".map(|"
              << condition_value << "| if " << condition << " {" << then_result
              << "} else {" << else_result << "}";
        } else if (then_result.empty()) {
            s << "let " << value << " = " << condition_value << ".map(|"
              << condition_value << "| if " << condition << " { " << value
              << " } else {" << else_result << "}";
        } else if (else_result.empty()) {
            s << "let " << value << " = " << condition_value << ".map(|"
              << condition_value << "| if " << condition << " {" << then_result
              << "}";
        }

        s << ");" << std::endl;

        return s.str();
    }

    std::string generate_code(bool using_operators = false,
                              bool needs_cloning = true) const {
        // Get the condition type
        klee::Expr::Kind conditionType = this->condition->getKind();

        //        std::cout << this->to_string() << std::endl;

        // Initialize the Rust code string as the unit
        std::string code = "()";

        std::string closing_character = using_operators ? "" : ")";
        std::string operator_str = "";

        // Generate the corresponding Rust code based on the condition
        switch (conditionType) {
        // Case for handling equality expressions.
        case klee::Expr::Eq:
            operator_str = using_operators ? " == " : ".eq(";
            code =
                generate_tfhe_code(this->condition->getKid(1), needs_cloning) +
                operator_str +
                generate_tfhe_code(this->condition->getKid(0), needs_cloning) +
                closing_character;
            break;
        // Case for handling unsigned less-than expressions.
        case klee::Expr::Ult:
            operator_str = using_operators ? " > " : ".ge(";
            code =
                generate_tfhe_code(this->condition->getKid(1), needs_cloning) +
                operator_str +
                generate_tfhe_code(this->condition->getKid(0), needs_cloning) +
                closing_character;
            break;

        // Case for handling unsigned less-than-or-equal-to expressions.
        case klee::Expr::Ule:
            operator_str = using_operators ? " > " : ".gt(";
            code =
                generate_tfhe_code(this->condition->getKid(1), needs_cloning) +
                operator_str +
                generate_tfhe_code(this->condition->getKid(0), needs_cloning) +
                closing_character;
            break;

        // Case for handling unsigned greater-than expressions.
        case klee::Expr::Ugt:
            operator_str = using_operators ? " <= " : ".le(";
            code =
                generate_tfhe_code(this->condition->getKid(1), needs_cloning) +
                operator_str +
                generate_tfhe_code(this->condition->getKid(0), needs_cloning) +
                closing_character;
            break;

        // Fallthrough to Uge since there is no signed values in TFHE
        case klee::Expr::Sge:
            // TODO Look at https://www.zama.ai/post/releasing-tfhe-rs-v0-4-0
            //  and see how to use signed comparisons
            operator_str = using_operators ? " < " : ".lt(";
            code =
                generate_tfhe_code(this->condition->getKid(1), needs_cloning) +
                operator_str +
                generate_tfhe_code(this->condition->getKid(0), needs_cloning) +
                closing_character;
            break;
        // Case for handling unsigned greater-than-or-equal-to expressions.
        case klee::Expr::Uge:
            operator_str = using_operators ? " < " : ".lt(";
            code =
                generate_tfhe_code(this->condition->getKid(1), needs_cloning) +
                operator_str +
                generate_tfhe_code(this->condition->getKid(0), needs_cloning) +
                closing_character;
            break;

        // Case for handling signed less-than expressions.
        case klee::Expr::Slt:
            operator_str = using_operators ? " >= " : ".ge(";
            code =
                generate_tfhe_code(this->condition->getKid(1), needs_cloning) +
                operator_str +
                generate_tfhe_code(this->condition->getKid(0), needs_cloning) +
                closing_character;
            break;

        // Case for handling signed less-than-or-equal-to expressions.
        case klee::Expr::Sle:
            operator_str = using_operators ? " > " : ".gt(";
            code =
                generate_tfhe_code(this->condition->getKid(1), needs_cloning) +
                operator_str +
                generate_tfhe_code(this->condition->getKid(0), needs_cloning) +
                closing_character;
            break;
        // FIXME Add more cases as needed for other condition types
        default:
            std::cerr << "Unsupported condition type: " << conditionType
                      << std::endl;
            exit(1);
        }

        return code;
    }

    std::string to_string_debug() const {
        std::string str;
        llvm::raw_string_ostream s(str);
        s << "UnivariatePBS(" << this->condition_to_string() << ", "
          << this->then_modification_to_string() << ", "
          << this->else_modification_to_string() << ", "
          << this->changed_value_to_string() << ", "
          << this->value_in_condition_to_string() << ")";
        return s.str();
    }

    friend std::ostream &operator<<(std::ostream &os,
                                    std::shared_ptr<UnivariatePBS> const &univariate_pbs) {
        os << univariate_pbs->to_string();
        return os;
    }
};
}  // namespace tfhe
}  // namespace targets
}  // namespace synapse
