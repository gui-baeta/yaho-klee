#include "execution_plan_node.h"
#include "../log.h"
#include "modules/module.h"
#include "visitors/visitor.h"

namespace synapse {

ep_node_id_t ExecutionPlanNode::counter = 0;

ExecutionPlanNode::ExecutionPlanNode(Module_ptr _module)
    : id(counter++), module(_module) {}

ExecutionPlanNode::ExecutionPlanNode(const ExecutionPlanNode *ep_node)
    : id(counter++), module(ep_node->module) {}

void ExecutionPlanNode::set_next(Branches _next) { next = _next; }
void ExecutionPlanNode::set_next(ExecutionPlanNode_ptr _next) {
    next.push_back(_next);
}
void ExecutionPlanNode::set_prev(ExecutionPlanNode_ptr _prev) { prev = _prev; }

const Module_ptr &ExecutionPlanNode::get_module() const { return module; }
void ExecutionPlanNode::replace_module(Module_ptr _module) { module = _module; }

// Get the child nodes.
// If there are two child nodes, the first node is the then arm and the second
// node is the else arm.
const Branches &ExecutionPlanNode::get_next() const { return next; }

bool ExecutionPlanNode::has_next() const {
    return !next.empty();
}

const ExecutionPlanNode_ptr &ExecutionPlanNode::get_next_sequential_ep_node()
    const {
    if (this->next.size() != 1) {
        std::cerr << "ExecutionPlanNode::get_next_sequential_ep_node() called "
                      "on a node with "
                   << this->next.size() << " next nodes. Expected 1.";
        assert(false);
    }

    return this->next[0];
}

ExecutionPlanNode_ptr ExecutionPlanNode::get_prev() const { return prev; }

ep_node_id_t ExecutionPlanNode::get_id() const { return id; }
void ExecutionPlanNode::set_id(ep_node_id_t _id) { id = _id; }

bool ExecutionPlanNode::is_terminal_node() const { return next.size() == 0; }

void ExecutionPlanNode::replace_next(ExecutionPlanNode_ptr before,
                                     ExecutionPlanNode_ptr after) {
    for (auto &branch : next) {
        if (branch->get_id() == before->get_id()) {
            branch = after;
            return;
        }
    }

    assert(false && "Before ExecutionPlanNode not found");
}

void ExecutionPlanNode::replace_prev(ExecutionPlanNode_ptr _prev) {
    prev = _prev;
}

void ExecutionPlanNode::replace_node(BDD::Node_ptr node) {
    module->replace_node(node);
}

ExecutionPlanNode_ptr ExecutionPlanNode::clone(bool recursive) const {
    // TODO: we are losing BDD traversal information.
    // That should also be cloned.

    auto cloned_node = ExecutionPlanNode::build(this);

    // The constructor increments the ID, let's fix that
    cloned_node->set_id(id);

    if (recursive) {
        auto next_clones = Branches();

        for (auto n : next) {
            auto cloned_next = n->clone(true);
            next_clones.push_back(cloned_next);
        }

        cloned_node->set_next(next_clones);
    }

    return cloned_node;
}

void ExecutionPlanNode::visit(ExecutionPlanVisitor &visitor) const {
    visitor.visit(this);
}

ExecutionPlanNode_ptr ExecutionPlanNode::build(Module_ptr _module) {
    ExecutionPlanNode *epn = new ExecutionPlanNode(_module);
    return std::shared_ptr<ExecutionPlanNode>(epn);
}

ExecutionPlanNode_ptr ExecutionPlanNode::build(
    const ExecutionPlanNode *ep_node) {
    ExecutionPlanNode *epn = new ExecutionPlanNode(ep_node);
    return std::shared_ptr<ExecutionPlanNode>(epn);
}
BDD::Node_ptr ExecutionPlanNode::get_node() const {
    return this->module->get_node();
}

/// Find the node of the given module type that is any child of this one
ExecutionPlanNode_ptr ExecutionPlanNode::find_node_by_module_type(
    int type) const {
    if (this->module->get_type() == type) {
        return std::shared_ptr<ExecutionPlanNode>(
            const_cast<ExecutionPlanNode *>(this));
    }

    for (auto &branch : this->next) {
        auto result = branch->find_node_by_module_type(type);
        if (result) {
            return result;
        }
    }

    return nullptr;
}

ExecutionPlanNode_ptr ExecutionPlanNode::find_node_by_id(
    ep_node_id_t id) const {
    if (this->id == id) {
        return std::shared_ptr<ExecutionPlanNode>(
            const_cast<ExecutionPlanNode *>(this));
    }

    for (auto &branch : this->next) {
        auto result = branch->find_node_by_id(id);
        if (result) {
            return result;
        }
    }

    return nullptr;
}

ExecutionPlanNode* ExecutionPlanNode::find_node_by_ep_node_id(ep_node_id_t id) {
    if (this->id == id) {
        return this;
    }

    for (auto &branch : this->next) {
        auto result = branch->find_node_by_ep_node_id(id);
        if (result) {
            return result;
        }
    }

    return nullptr;
}


const ExecutionPlanNode* ExecutionPlanNode::find_node_by_bdd_node_id(BDD::node_id_t id) const {
    if (this->module->get_node()->get_id() == id) {
        return this;
    }

    for (auto &branch : this->next) {
        auto result = branch->find_node_by_bdd_node_id(id);
        if (result) {
            return result;
        }
    }

    return nullptr;
}

ExecutionPlanNode* ExecutionPlanNode::find_node_by_bdd_node_id(BDD::node_id_t id) {
    if (this->module->get_node()->get_id() == id) {
        return this;
    }

    for (auto &branch : this->next) {
        auto result = branch->find_node_by_bdd_node_id(id);
        if (result) {
            return result;
        }
    }

    return nullptr;
}

void ExecutionPlanNode::set_completed() {
    this->module->set_completed();
}

void ExecutionPlanNode::set_other_condition(klee::ref<klee::Expr> _other_condition) {
    this->module->set_other_condition(_other_condition);
}

std::string ExecutionPlanNode::get_module_name() const {
    return this->get_module()->get_name();
}
int ExecutionPlanNode::get_module_type() const {
    return this->get_module()->get_type();
}

klee::ref<klee::Expr> ExecutionPlanNode::get_module_condition() const {
    return this->get_module()->get_expr();
}

}  // namespace synapse