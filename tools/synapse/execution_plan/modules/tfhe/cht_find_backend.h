#pragma once

#include "tfhe_module.h"

namespace synapse {
namespace targets {
namespace tfhe {

class ChtFindBackend : public tfheModule {
private:
  klee::ref<klee::Expr> hash;
  addr_t cht_addr;
  addr_t active_backends_addr;
  klee::ref<klee::Expr> cht_height;
  klee::ref<klee::Expr> backend_capacity;
  klee::ref<klee::Expr> chosen_backend;
  BDD::symbol_t found;

public:
  ChtFindBackend()
      : tfheModule(ModuleType::tfhe_ChtFindBackend, "ChtFindBackend") {}

  ChtFindBackend(BDD::Node_ptr node, klee::ref<klee::Expr> _hash,
                 addr_t _cht_addr, addr_t _active_backends_addr,
                 klee::ref<klee::Expr> _cht_height,
                 klee::ref<klee::Expr> _backend_capacity,
                 klee::ref<klee::Expr> _chosen_backend,
                 const BDD::symbol_t &_found)
      : tfheModule(ModuleType::tfhe_ChtFindBackend, "ChtFindBackend", node),
        hash(_hash), cht_addr(_cht_addr),
        active_backends_addr(_active_backends_addr), cht_height(_cht_height),
        backend_capacity(_backend_capacity), chosen_backend(_chosen_backend),
        found(_found) {}

private:
  processing_result_t process(const ExecutionPlan &ep,
                              BDD::Node_ptr node) override {
    processing_result_t result;

    auto casted = BDD::cast_node<BDD::Call>(node);

    if (!casted) {
      return result;
    }

    auto call = casted->get_call();

    if (call.function_name != BDD::symbex::FN_CHT_FIND_BACKEND) {
      return result;
    }

    assert(!call.args[BDD::symbex::FN_CHT_ARG_HASH].expr.isNull());
    assert(!call.args[BDD::symbex::FN_CHT_ARG_CHT].expr.isNull());
    assert(!call.args[BDD::symbex::FN_CHT_ARG_ACTIVE].expr.isNull());
    assert(!call.args[BDD::symbex::FN_CHT_ARG_HEIGHT].expr.isNull());
    assert(!call.args[BDD::symbex::FN_CHT_ARG_CAPACITY].expr.isNull());
    assert(!call.args[BDD::symbex::FN_CHT_ARG_CHOSEN].out.isNull());

    auto _hash = call.args[BDD::symbex::FN_CHT_ARG_HASH].expr;
    auto _cht = call.args[BDD::symbex::FN_CHT_ARG_CHT].expr;
    auto _active = call.args[BDD::symbex::FN_CHT_ARG_ACTIVE].expr;
    auto _height = call.args[BDD::symbex::FN_CHT_ARG_HEIGHT].expr;
    auto _capacity = call.args[BDD::symbex::FN_CHT_ARG_CAPACITY].expr;
    auto _chosen = call.args[BDD::symbex::FN_CHT_ARG_CHOSEN].out;

    auto _cht_addr = kutil::expr_addr_to_obj_addr(_cht);
    auto _active_addr = kutil::expr_addr_to_obj_addr(_active);

    auto _generated_symbols = casted->get_local_generated_symbols();
    auto _found =
        BDD::get_symbol(_generated_symbols, BDD::symbex::CHT_BACKEND_FOUND_SYMBOL);

    // cht is actually a vector
    save_vector(ep, _cht_addr);
    save_cht(ep, _cht_addr);

    auto new_module =
        std::make_shared<ChtFindBackend>(node, _hash, _cht_addr, _active_addr,
                                         _height, _capacity, _chosen, _found);
    auto new_ep = ep.add_leaf(new_module, node->get_next());

    result.module = new_module;
    result.next_eps.push_back(new_ep);

    return result;
  }

public:
  virtual void visit(ExecutionPlanVisitor &visitor,
                     const ExecutionPlanNode *ep_node) const override {
    visitor.visit(ep_node, this);
  }

  virtual Module_ptr clone() const override {
    auto cloned =
        new ChtFindBackend(node, hash, cht_addr, active_backends_addr,
                           cht_height, backend_capacity, chosen_backend, found);
    return std::shared_ptr<Module>(cloned);
  }

  virtual bool equals(const Module *other) const override {
    if (other->get_type() != type) {
      return false;
    }

    auto other_cast = static_cast<const ChtFindBackend *>(other);

    if (!kutil::solver_toolbox.are_exprs_always_equal(hash,
                                                      other_cast->get_hash())) {
      return false;
    }

    if (cht_addr != other_cast->get_cht_addr()) {
      return false;
    }

    if (active_backends_addr != other_cast->get_active_backends_addr()) {
      return false;
    }

    if (!kutil::solver_toolbox.are_exprs_always_equal(
            cht_height, other_cast->get_cht_height())) {
      return false;
    }

    if (!kutil::solver_toolbox.are_exprs_always_equal(
            backend_capacity, other_cast->get_backend_capacity())) {
      return false;
    }

    if (!kutil::solver_toolbox.are_exprs_always_equal(
            chosen_backend, other_cast->get_chosen_backend())) {
      return false;
    }

    if (found.label != other_cast->get_found().label) {
      return false;
    }

    return true;
  }

  klee::ref<klee::Expr> get_hash() const { return hash; }
  addr_t get_cht_addr() const { return cht_addr; }
  addr_t get_active_backends_addr() const { return active_backends_addr; }
  klee::ref<klee::Expr> get_cht_height() const { return cht_height; }

  klee::ref<klee::Expr> get_backend_capacity() const {
    return backend_capacity;
  }

  klee::ref<klee::Expr> get_chosen_backend() const { return chosen_backend; }

  const BDD::symbol_t &get_found() const { return found; }
};
} // namespace tfhe
} // namespace targets
} // namespace synapse
