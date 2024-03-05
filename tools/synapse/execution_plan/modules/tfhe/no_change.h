#pragma once

#include "tfhe_module.h"

#include "../../../tfhe_generate_code.h"

#include "else.h"
#include "then.h"

namespace synapse {
namespace targets {
namespace tfhe {

class NoChange : public tfheModule {
private:
    /* This Module represents a value that didn't change, for a given sequence of nodes */

    /// The value that wasm't changed
    int unchanged_value = 0;

public:
    NoChange() : tfheModule(ModuleType::tfhe_NoChange, "NoChange") {}

    NoChange(BDD::Node_ptr node, int _unchanged_value)
        : tfheModule(ModuleType::tfhe_NoChange, "NoChange", node),
          unchanged_value(_unchanged_value){}

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
            new NoChange(node, this->unchanged_value);
        return std::shared_ptr<Module>(cloned);
    }

    virtual bool equals(const Module *other) const override {
        if (other->get_type() != this->type) {
            return false;
        }

        auto other_cast = static_cast<const NoChange *>(other);

        if (this->unchanged_value != other_cast->get_unchanged_value()) {
            return false;
        }

        return true;
    }

    int get_unchanged_value() const { return this->unchanged_value; }

    std::string to_string() const {
        return std::string("");
    }

    std::string to_string_debug() const {
        std::string str;
        llvm::raw_string_ostream s(str);
        s << "NoChange(" << this->get_unchanged_value() <<")";
        return s.str();
    }

    friend std::ostream &operator<<(std::ostream &os,
                                    std::shared_ptr<NoChange> const &change) {
        os << change->to_string();
        return os;
    }
};
}  // namespace tfhe
}  // namespace targets
}  // namespace synapse
