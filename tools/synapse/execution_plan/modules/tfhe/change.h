#pragma once

#include "tfhe_module.h"

#include "../../../tfhe_generate_code.h"

#include "else.h"
#include "then.h"

namespace synapse {
namespace targets {
namespace tfhe {

class Change : public tfheModule {
private:
    klee::ref<klee::Expr> change;

    /// The value that was changed
    int changed_value = 0;

public:
    Change() : tfheModule(ModuleType::tfhe_Change, "Change") {}

    Change(BDD::Node_ptr node, klee::ref<klee::Expr> _change, int _changed_value)
        : tfheModule(ModuleType::tfhe_Change, "Change", node),
          change(_change), changed_value(_changed_value) {}

private:
    processing_result_t process(const ExecutionPlan &ep,
                                BDD::Node_ptr node) override {
        processing_result_t result;

        /* Does nothing since this Module is used indirectly by other Modules */

        return result;
    }

public:
    virtual void visit(ExecutionPlanVisitor &visitor,
                       const ExecutionPlanNode *ep_node) const override {
        visitor.visit(ep_node, this);
    }

    virtual Module_ptr clone() const override {
        auto cloned =
            new Change(node, this->change, this->changed_value);
        return std::shared_ptr<Module>(cloned);
    }

    virtual bool equals(const Module *other) const override {
        if (other->get_type() != this->type) {
            return false;
        }

        auto other_cast = static_cast<const Change *>(other);

        if (!(kutil::solver_toolbox.are_exprs_always_equal(
                  this->change, other_cast->get_change()))) {
            return false;
        }

        if (this->changed_value != other_cast->get_changed_value()) {
            return false;
        }

        return true;
    }

    const klee::ref<klee::Expr> &get_change() const {
        return this->change;
    }

    int get_changed_value() const { return this->changed_value; }

    std::string changed_value_to_string() const {
        return std::string("val") + std::to_string(this->changed_value);
    }
    std::string change_to_string() const {
        return generate_tfhe_code(this->change, true);
    }

    std::string to_string() const {
        std::ostringstream s;

        std::string value = this->changed_value_to_string();
        std::string change_str = this->change_to_string();

        s << "let " << value << " = " << change_str << ";" << std::endl;

        return s.str();
    }

    std::string to_string_debug() const {
        std::string str;
        llvm::raw_string_ostream s(str);
        s << "Change(" << this->get_change() << ", "
          << this->changed_value_to_string() << ")";
        return s.str();
    }

    friend std::ostream &operator<<(std::ostream &os,
                                    std::shared_ptr<Change> const &change) {
        os << change->to_string();
        return os;
    }
};
}  // namespace tfhe
}  // namespace targets
}  // namespace synapse
