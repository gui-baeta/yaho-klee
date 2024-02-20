#pragma once

#include "../module.h"

namespace synapse {
namespace targets {
namespace x86_bmv2 {

class ExpireItemsSingleMap : public Module {
private:
  klee::ref<klee::Expr> dchain_addr;
  klee::ref<klee::Expr> vector_addr;
  klee::ref<klee::Expr> map_addr;
  klee::ref<klee::Expr> time;
  klee::ref<klee::Expr> number_of_freed_flows;

  BDD::symbols_t generated_symbols;

public:
  ExpireItemsSingleMap()
      : Module(ModuleType::x86_BMv2_ExpireItemsSingleMap, TargetType::x86_BMv2,
               "Expire") {}

  ExpireItemsSingleMap(BDD::Node_ptr node, klee::ref<klee::Expr> _dchain_addr,
                       klee::ref<klee::Expr> _vector_addr,
                       klee::ref<klee::Expr> _map_addr,
                       klee::ref<klee::Expr> _time,
                       klee::ref<klee::Expr> _number_of_freed_flows,
                       BDD::symbols_t _generated_symbols)
      : Module(ModuleType::x86_BMv2_ExpireItemsSingleMap, TargetType::x86_BMv2,
               "Expire", node),
        dchain_addr(_dchain_addr), vector_addr(_vector_addr),
        map_addr(_map_addr), time(_time),
        number_of_freed_flows(_number_of_freed_flows),
        generated_symbols(_generated_symbols) {}

private:
  processing_result_t process(const ExecutionPlan &ep,
                              BDD::Node_ptr node) override {
    processing_result_t result;

    auto casted = BDD::cast_node<BDD::Call>(node);

    if (!casted) {
      return result;
    }

    auto call = casted->get_call();

    if (call.function_name == BDD::symbex::FN_EXPIRE_MAP) {
      assert(!call.args[BDD::symbex::FN_EXPIRE_MAP_ARG_CHAIN].expr.isNull());
      assert(!call.args[BDD::symbex::FN_EXPIRE_MAP_ARG_VECTOR].expr.isNull());
      assert(!call.args[BDD::symbex::FN_EXPIRE_MAP_ARG_MAP].expr.isNull());
      assert(!call.args[BDD::symbex::FN_EXPIRE_MAP_ARG_TIME].expr.isNull());
      assert(!call.ret.isNull());

      auto _dchain_addr = call.args[BDD::symbex::FN_EXPIRE_MAP_ARG_CHAIN].expr;
      auto _vector_addr = call.args[BDD::symbex::FN_EXPIRE_MAP_ARG_VECTOR].expr;
      auto _map_addr = call.args[BDD::symbex::FN_EXPIRE_MAP_ARG_MAP].expr;
      auto _time = call.args[BDD::symbex::FN_EXPIRE_MAP_ARG_TIME].expr;
      auto _number_of_freed_flows = call.ret;
      auto _generated_symbols = casted->get_local_generated_symbols();

      auto new_module = std::make_shared<ExpireItemsSingleMap>(
          node, _dchain_addr, _vector_addr, _map_addr, _time,
          _number_of_freed_flows, _generated_symbols);
      auto new_ep = ep.add_leaf(new_module, node->get_next());

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
    auto cloned =
        new ExpireItemsSingleMap(node, dchain_addr, map_addr, vector_addr, time,
                                 number_of_freed_flows, generated_symbols);
    return std::shared_ptr<Module>(cloned);
  }

  virtual bool equals(const Module *other) const override {
    if (other->get_type() != type) {
      return false;
    }

    auto other_cast = static_cast<const ExpireItemsSingleMap *>(other);

    if (!kutil::solver_toolbox.are_exprs_always_equal(
            dchain_addr, other_cast->get_dchain_addr())) {
      return false;
    }

    if (!kutil::solver_toolbox.are_exprs_always_equal(
            vector_addr, other_cast->get_vector_addr())) {
      return false;
    }

    if (!kutil::solver_toolbox.are_exprs_always_equal(
            map_addr, other_cast->get_map_addr())) {
      return false;
    }

    if (!kutil::solver_toolbox.are_exprs_always_equal(time,
                                                      other_cast->get_time())) {
      return false;
    }

    if (!kutil::solver_toolbox.are_exprs_always_equal(
            number_of_freed_flows, other_cast->get_number_of_freed_flows())) {
      return false;
    }

    if (generated_symbols != other_cast->generated_symbols) {
      return false;
    }

    return true;
  }

  const klee::ref<klee::Expr> &get_dchain_addr() const { return dchain_addr; }
  const klee::ref<klee::Expr> &get_vector_addr() const { return vector_addr; }
  const klee::ref<klee::Expr> &get_map_addr() const { return map_addr; }
  const klee::ref<klee::Expr> &get_time() const { return time; }
  const klee::ref<klee::Expr> &get_number_of_freed_flows() const {
    return number_of_freed_flows;
  }

  const BDD::symbols_t &get_generated_symbols() const {
    return generated_symbols;
  }
};
} // namespace x86_bmv2
} // namespace targets
} // namespace synapse
