#pragma once

#include "tfhe_module.h"

namespace synapse {
namespace targets {
namespace tfhe {

class ExpireItemsSingleMapIteratively : public tfheModule {
private:
  addr_t vector_addr;
  addr_t map_addr;
  klee::ref<klee::Expr> start;
  klee::ref<klee::Expr> n_elems;

public:
  ExpireItemsSingleMapIteratively()
      : tfheModule(ModuleType::tfhe_ExpireItemsSingleMapIteratively,
                  "ExpireItemsSingleMapIteratively") {}

  ExpireItemsSingleMapIteratively(BDD::Node_ptr node, addr_t _vector_addr,
                                  addr_t _map_addr,
                                  klee::ref<klee::Expr> _start,
                                  klee::ref<klee::Expr> _n_elems)
      : tfheModule(ModuleType::tfhe_ExpireItemsSingleMapIteratively,
                  "ExpireItemsSingleMapIteratively", node),
        vector_addr(_vector_addr), map_addr(_map_addr), start(_start),
        n_elems(_n_elems) {}

private:
  processing_result_t process(const ExecutionPlan &ep,
                              BDD::Node_ptr node) override {
    processing_result_t result;

    auto casted = BDD::cast_node<BDD::Call>(node);

    if (!casted) {
      return result;
    }

    auto call = casted->get_call();

    if (call.function_name == BDD::symbex::FN_EXPIRE_MAP_ITERATIVELY) {
      assert(!call.args[BDD::symbex::EXPIRE_MAP_ITERATIVELY_MAP].expr.isNull());
      assert(!call.args[BDD::symbex::EXPIRE_MAP_ITERATIVELY_VECTOR].expr.isNull());
      assert(!call.args[BDD::symbex::EXPIRE_MAP_ITERATIVELY_START].expr.isNull());
      assert(!call.args[BDD::symbex::EXPIRE_MAP_ITERATIVELY_N_ELEMS].expr.isNull());
      assert(!call.ret.isNull());

      auto _map = call.args[BDD::symbex::EXPIRE_MAP_ITERATIVELY_MAP].expr;
      auto _vector = call.args[BDD::symbex::EXPIRE_MAP_ITERATIVELY_VECTOR].expr;
      auto _start = call.args[BDD::symbex::EXPIRE_MAP_ITERATIVELY_START].expr;
      auto _n_elems = call.args[BDD::symbex::EXPIRE_MAP_ITERATIVELY_N_ELEMS].expr;

      auto _map_addr = kutil::expr_addr_to_obj_addr(_map);
      auto _vector_addr = kutil::expr_addr_to_obj_addr(_vector);

      auto new_module = std::make_shared<ExpireItemsSingleMapIteratively>(
          node, _vector_addr, _map_addr, _start, _n_elems);
      auto new_ep = ep.add_leaves(new_module, node->get_next());

      result.module = new_module;
      result.next_eps.push_back(new_ep);
    }

    return result;
  }

public:
  virtual void visit(ExecutionPlanVisitor &visitor,
                     const ExecutionPlanNode *ep_node) const override {
    visitor.visit(ep_node, this);
  }

  virtual Module_ptr clone() const override {
    auto cloned = new ExpireItemsSingleMapIteratively(
        node, map_addr, vector_addr, start, n_elems);
    return std::shared_ptr<Module>(cloned);
  }

  virtual bool equals(const Module *other) const override {
    if (other->get_type() != type) {
      return false;
    }

    auto other_cast =
        static_cast<const ExpireItemsSingleMapIteratively *>(other);

    if (vector_addr != other_cast->get_vector_addr()) {
      return false;
    }

    if (map_addr != other_cast->get_map_addr()) {
      return false;
    }

    if (!kutil::solver_toolbox.are_exprs_always_equal(
            start, other_cast->get_start())) {
      return false;
    }

    if (!kutil::solver_toolbox.are_exprs_always_equal(
            n_elems, other_cast->get_n_elems())) {
      return false;
    }

    return true;
  }

  addr_t get_vector_addr() const { return vector_addr; }
  addr_t get_map_addr() const { return map_addr; }
  const klee::ref<klee::Expr> &get_start() const { return start; }
  const klee::ref<klee::Expr> &get_n_elems() const { return n_elems; }
};
} // namespace tfhe
} // namespace targets
} // namespace synapse
