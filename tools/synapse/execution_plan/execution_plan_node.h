#pragma once

#include "call-paths-to-bdd.h"
#include <string>

namespace synapse {

class ExecutionPlanVisitor;

class ExecutionPlanNode;
typedef std::shared_ptr<ExecutionPlanNode> ExecutionPlanNode_ptr;
// For a binary branch, the first element is the then arm and the second element
// is the else arm.
typedef std::vector<ExecutionPlanNode_ptr> Branches;

class Module;
typedef std::shared_ptr<Module> Module_ptr;

typedef uint64_t ep_node_id_t;

class ExecutionPlanNode {
    friend class ExecutionPlan;

private:
    ep_node_id_t id;

    Module_ptr module;
    Branches next;
    ExecutionPlanNode_ptr prev;

private:
    ExecutionPlanNode(Module_ptr _module);
    ExecutionPlanNode(const ExecutionPlanNode *ep_node);

public:
    void set_next(Branches _next);
    void set_next(ExecutionPlanNode_ptr _next);
    void set_prev(ExecutionPlanNode_ptr _prev);

    BDD::Node_ptr get_node() const;

    const Module_ptr &get_module() const;
    void replace_module(Module_ptr _module);

    const Branches &get_next() const;
    bool has_next() const;

    /// Returns the next node in the Execution Plan.
    /// This expects the Execution Plan to be linear and not have branches
    const ExecutionPlanNode_ptr &get_next_sequential_ep_node() const;
    ExecutionPlanNode_ptr get_prev() const;

    ep_node_id_t get_id() const;
    void set_id(ep_node_id_t _id);

    bool is_terminal_node() const;

    void replace_next(ExecutionPlanNode_ptr before,
                      ExecutionPlanNode_ptr after);
    void replace_prev(ExecutionPlanNode_ptr _prev);
    void replace_node(BDD::Node_ptr node);

    ExecutionPlanNode_ptr clone(bool recursive = false) const;
    void visit(ExecutionPlanVisitor &visitor) const;

    static ExecutionPlanNode_ptr build(Module_ptr _module);
    static ExecutionPlanNode_ptr build(const ExecutionPlanNode *ep_node);

    ExecutionPlanNode_ptr find_node_by_module_type(int type) const;
    ExecutionPlanNode_ptr find_node_by_id(ep_node_id_t id) const;
    ExecutionPlanNode* find_node_by_ep_node_id(ep_node_id_t id);
    const ExecutionPlanNode* find_node_by_bdd_node_id(BDD::node_id_t id) const;
    ExecutionPlanNode* find_node_by_bdd_node_id(BDD::node_id_t id);

    void set_completed();
    void set_other_condition(klee::ref<klee::Expr> _other_condition);


    std::string get_module_name() const;
    int get_module_type() const;
    klee::ref<klee::Expr> get_module_condition() const;

private:
    static ep_node_id_t counter;
};
}  // namespace synapse