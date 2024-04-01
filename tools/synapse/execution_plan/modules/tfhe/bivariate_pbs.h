#pragma once

#include "tfhe_module.h"

#include "../../../tfhe_generate_code.h"

#include "else.h"
#include "then.h"

#include <utility>

namespace synapse {
namespace targets {
namespace tfhe {

class BivariatePBS : public tfheModule {
private:
    klee::ref<klee::Expr> condition;
    klee::ref<klee::Expr> other_condition;

    bool complete = false;

    /// The condition depends on this value
    std::pair<int, int> values_in_condition = {0, 0};

public:
    BivariatePBS() : tfheModule(ModuleType::tfhe_BivariatePBS, "BivariatePBS") {}

    BivariatePBS(BDD::Node_ptr node, klee::ref<klee::Expr> _condition, klee::ref<klee::Expr> _other_condition,
            std::pair<int, int> _values_in_condition, bool _complete = false)
        : tfheModule(ModuleType::tfhe_BivariatePBS, "BivariatePBS", node),
          condition(_condition),
          other_condition(_other_condition),
          values_in_condition(_values_in_condition), complete(_complete) {}

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

        klee::ref<klee::Expr> _condition =
            klee::ref<klee::Expr>(branch_node->get_condition());

        std::cout << "Processing Bivariate PBS" << std::endl;

        // If the last-developed node is an incomplete Bivariate PBS, this module needs to skip this node
//        const ExecutionPlanNode* previous_ep_node = ep.find_node_by_bdd_node_id(node->get_prev()->get_id());
        ExecutionPlanNode* previous_ep_node = ep.get_last_developed_node_raw();
        if (previous_ep_node) {
            if (previous_ep_node->get_module_type() == ModuleType::tfhe_BivariatePBS &&
                !previous_ep_node->get_module()->is_complete()) {

                auto new_then_module = std::make_shared<Then>(node);
                auto new_else_module = std::make_shared<Else>(node);

                 auto then_leaf =
                    ExecutionPlan::leaf_t(new_then_module, branch_node->get_on_true());
                auto else_leaf =
                    ExecutionPlan::leaf_t(new_else_module, branch_node->get_on_false());

                std::vector<ExecutionPlan::leaf_t> then_else_leaves{then_leaf, else_leaf};

                result.module = previous_ep_node->get_module();

                ExecutionPlan new_ep = ep.add_leaves(then_else_leaves);

//                ExecutionPlanNode* previous_bivariate_pbs = new_ep.find_node_by_ep_node_id(previous_ep_node->get_id());
//                previous_bivariate_pbs->set_completed();
                previous_ep_node->set_completed();
                previous_ep_node->set_other_condition(_condition);

                result.next_eps.push_back(new_ep);

                return result;
            }
        }

        std::vector<int> values_in_condition = get_dependent_values(_condition);
        std::cout
            << "----------------------- Condition values ------------------"
            << std::endl;
        for (auto &val : values_in_condition) {
            std::cout << "Dependent value: " << val << std::endl;
        }
        std::cout << "-----------------------------------------" << std::endl;

        // Bivariate of IFxy
        if (values_in_condition.size() == 2) {
            auto new_branch_module = std::make_shared<BivariatePBS>(node, _condition, nullptr,
                                                                    std::make_pair(values_in_condition[0], values_in_condition[1]), true);
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
        }

        // Bivariate of IFx -> (IFy, IFy)
        if (values_in_condition.size() == 1 &&
            branch_node->get_on_true()->get_type() ==
                BDD::Node::NodeType::BRANCH &&
            branch_node->get_on_false()->get_type() ==
                BDD::Node::NodeType::BRANCH) {

            const BDD::Branch *on_true_branch = BDD::cast_node<BDD::Branch>(branch_node->get_on_true());
            const BDD::Branch *on_false_branch = BDD::cast_node<BDD::Branch>(branch_node->get_on_false());
            assert(!on_true_branch->get_condition().isNull());
            assert(!on_false_branch->get_condition().isNull());

            klee::ref<klee::Expr> _on_true_branch_condition = klee::ref<klee::Expr>(on_true_branch->get_condition());
            klee::ref<klee::Expr> _on_false_branch_condition = klee::ref<klee::Expr>(on_false_branch->get_condition());

            std::vector<int> on_true_values = get_dependent_values(_on_true_branch_condition);
            std::vector<int> on_false_values = get_dependent_values(_on_false_branch_condition);

            if (on_true_values.size() == 1 && on_false_values.size() == 1 &&
                on_true_values.at(0) != values_in_condition.at(0) &&
                on_false_values.at(0) != values_in_condition.at(0)) {

                // True arm
                auto new_left_module = std::make_shared<BivariatePBS>(node, _condition, nullptr,
                                                                      std::make_pair(values_in_condition.at(0), on_true_values.at(0)), false);

                auto new_right_module = std::make_shared<BivariatePBS>(node, _condition, nullptr,
                                                                    std::make_pair(values_in_condition.at(0), on_false_values.at(0)), false);

                auto left_leaf =
                    ExecutionPlan::leaf_t(new_left_module, branch_node->get_on_true());
                auto right_leaf =
                    ExecutionPlan::leaf_t(new_right_module, branch_node->get_on_false());

                std::vector<ExecutionPlan::leaf_t> left_and_right_leaves{left_leaf, right_leaf};
                ExecutionPlan _new_ep = ep.add_leaves(left_and_right_leaves);

                result.module = new_left_module;
                result.next_eps.push_back(_new_ep);

                return result;
            }
        }

        // If children are not branches and the number of condition values is 1, this node should be done by the Univariate PBS
        if (values_in_condition.size() == 1) {
            return result;
        }

        std::cerr << "Unsupported BivariatePBS condition" << std::endl;
        exit(1);
    }

public:
    virtual void visit(ExecutionPlanVisitor &visitor,
                       const ExecutionPlanNode *ep_node) const override {
        visitor.visit(ep_node, this);
    }

    virtual Module_ptr clone() const override {
        auto cloned =
            new BivariatePBS(node, this->condition, this->other_condition,
                        this->values_in_condition, this->complete);
        return std::shared_ptr<Module>(cloned);
    }

    virtual bool equals(const Module *other) const override {
        if (other->get_type() != this->type) {
            return false;
        }

        auto other_cast = static_cast<const BivariatePBS *>(other);

        if (!(kutil::solver_toolbox.are_exprs_always_equal(
                  this->condition, other_cast->get_condition()) &&
              kutil::solver_toolbox.are_exprs_always_equal(
                  this->other_condition, other_cast->get_other_condition()))) {
            return false;
        }

        if (this->values_in_condition.first != other_cast->get_values_in_condition().first) {
            return false;
        }

        if (this->values_in_condition.second != other_cast->get_values_in_condition().second) {
            return false;
        }

        if (this->complete != other_cast->is_complete()) {
            return false;
        }

        return true;
    }

    bool is_complete() const override { return this->complete; }

    void set_completed() override { this->complete = true; }
    void set_other_condition(klee::ref<klee::Expr> _other_condition) override { this->other_condition = _other_condition; }

    const klee::ref<klee::Expr> &get_condition() const {
        return this->condition;
    }

    const klee::ref<klee::Expr> &get_other_condition() const {
        return this->other_condition;
    }

    klee::ref<klee::Expr> get_expr() const override {
        return this->condition;
    }

    std::pair<int, int> get_values_in_condition() const { return this->values_in_condition; }

    std::string changed_value_to_string(int changed_value) const {
        return std::string("val") + std::to_string(changed_value);
    }
    std::string value_in_condition_to_string(int val_i = -1) const {
        if (val_i == -1) {
            return std::string("val") + std::to_string(this->values_in_condition.first) + ", " +
                   std::string("val") + std::to_string(this->values_in_condition.second);
        } else if (val_i == 0) {
            return std::string("val") + std::to_string(this->values_in_condition.first);
        } else if (val_i == 1) {
            return std::string("val") + std::to_string(this->values_in_condition.second);
        }
    }
    std::string is_complete_to_string() const {
        return this->complete ? " (Complete)" : "(Incomplete)";
    }
    std::string condition_to_string() const {
        return generate_code(this->condition, true, false);
    }

    std::string other_condition_to_string() const {
        return generate_code(this->other_condition, true, false);
    }

    std::string bivariate_pbs_to_string(int changed_value, klee::ref<klee::Expr> operation, klee::ref<klee::Expr> other_operation, bool then_arm, bool terminate = false) const override {
        std::ostringstream s;

        std::string value = this->changed_value_to_string(changed_value);
        std::string condition_value = this->value_in_condition_to_string(0);
        std::string other_condition_value = this->value_in_condition_to_string(1);

        std::string condition_str = this->condition_to_string();
        std::string other_condition_str = this->other_condition_to_string();

        std::string op = generate_tfhe_code(operation, false);
        std::string other_op = generate_tfhe_code(other_operation, false);
        // TODO

        if (then_arm == 1) {
            s << condition_value << ".bivariate_function(&" + other_condition_value + ", |"
              << condition_value << ", " << other_condition_value << "| if " << condition_str << " && " << other_condition_str << " { 1 } else { 0 }";
        } else {
            s << condition_value << ".bivariate_function(&" + other_condition_value + ", |"
              << condition_value << ", " << other_condition_value << "| if " << condition_str << " && " << other_condition_str
              << " { 0 } else { 1 }";
        }

        if (terminate) {
            s << ");" << std::endl;
        } else {
            s << ")";
        }

        return s.str();
    }

    std::string generate_code(klee::ref<klee::Expr> condition_expr, bool using_operators = false,
                              bool needs_cloning = true) const {
        // Get the condition type
        klee::Expr::Kind conditionType = condition_expr->getKind();

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
                generate_tfhe_code(condition_expr->getKid(1), needs_cloning) +
                operator_str +
                generate_tfhe_code(condition_expr->getKid(0), needs_cloning) +
                closing_character;
            break;
        // Case for handling unsigned less-than expressions.
        case klee::Expr::Ult:
            operator_str = using_operators ? " > " : ".ge(";
            code =
                generate_tfhe_code(condition_expr->getKid(1), needs_cloning) +
                operator_str +
                generate_tfhe_code(condition_expr->getKid(0), needs_cloning) +
                closing_character;
            break;

        // Case for handling unsigned less-than-or-equal-to expressions.
        case klee::Expr::Ule:
            operator_str = using_operators ? " > " : ".gt(";
            code =
                generate_tfhe_code(condition_expr->getKid(1), needs_cloning) +
                operator_str +
                generate_tfhe_code(condition_expr->getKid(0), needs_cloning) +
                closing_character;
            break;

        // Case for handling unsigned greater-than expressions.
        case klee::Expr::Ugt:
            operator_str = using_operators ? " <= " : ".le(";
            code =
                generate_tfhe_code(condition_expr->getKid(1), needs_cloning) +
                operator_str +
                generate_tfhe_code(condition_expr->getKid(0), needs_cloning) +
                closing_character;
            break;

        // Fallthrough to Uge since there is no signed values in TFHE
        case klee::Expr::Sge:
            // TODO Look at https://www.zama.ai/post/releasing-tfhe-rs-v0-4-0
            //  and see how to use signed comparisons
            operator_str = using_operators ? " < " : ".lt(";
            code =
                generate_tfhe_code(condition_expr->getKid(1), needs_cloning) +
                operator_str +
                generate_tfhe_code(condition_expr->getKid(0), needs_cloning) +
                closing_character;
            break;
        // Case for handling unsigned greater-than-or-equal-to expressions.
        case klee::Expr::Uge:
            operator_str = using_operators ? " < " : ".lt(";
            code =
                generate_tfhe_code(condition_expr->getKid(1), needs_cloning) +
                operator_str +
                generate_tfhe_code(condition_expr->getKid(0), needs_cloning) +
                closing_character;
            break;

        // Case for handling signed less-than expressions.
        case klee::Expr::Slt:
            operator_str = using_operators ? " >= " : ".ge(";
            code =
                generate_tfhe_code(condition_expr->getKid(1), needs_cloning) +
                operator_str +
                generate_tfhe_code(condition_expr->getKid(0), needs_cloning) +
                closing_character;
            break;

        // Case for handling signed less-than-or-equal-to expressions.
        case klee::Expr::Sle:
            operator_str = using_operators ? " > " : ".gt(";
            code =
                generate_tfhe_code(condition_expr->getKid(1), needs_cloning) +
                operator_str +
                generate_tfhe_code(condition_expr->getKid(0), needs_cloning) +
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
        s << "BivariatePBS(" << this->condition_to_string() << ", "
          << this->value_in_condition_to_string() << ")" << this->is_complete_to_string();
        return s.str();
    }

    friend std::ostream &operator<<(std::ostream &os,
                                    std::shared_ptr<BivariatePBS> const &univariate_pbs) {
        os << univariate_pbs->to_string_debug();
        return os;
    }
};
}  // namespace tfhe
}  // namespace targets
}  // namespace synapse
