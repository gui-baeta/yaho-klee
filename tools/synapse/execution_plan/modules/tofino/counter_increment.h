#pragma once

#include "ignore.h"
#include "tofino_module.h"

namespace synapse {
namespace targets {
namespace tofino {

class CounterIncrement : public TofinoModule {
protected:
  CounterRef counter;
  klee::ref<klee::Expr> index;
  klee::ref<klee::Expr> value;

public:
  CounterIncrement()
      : TofinoModule(ModuleType::Tofino_CounterIncrement, "CounterIncrement") {}

  CounterIncrement(BDD::Node_ptr node, CounterRef _counter,
                   klee::ref<klee::Expr> _index, klee::ref<klee::Expr> _value)
      : TofinoModule(ModuleType::Tofino_CounterIncrement, "CounterIncrement",
                     node),
        counter(_counter), index(_index), value(_value) {
    assert(counter);
  }

private:
  bool can_implement(const ExecutionPlan &ep, addr_t obj) const {
    auto mb = ep.get_memory_bank<TofinoMemoryBank>(Tofino);
    auto compatible = mb->check_implementation_compatibility(
        obj, {DataStructure::Type::COUNTER});
    return compatible;
  }

  void save_decision(const ExecutionPlan &ep, CounterRef counter) const {
    auto objs = counter->get_objs();
    assert(objs.size() == 1);
    auto obj = *objs.begin();

    auto mb = ep.get_memory_bank();
    mb->save_placement_decision(obj, PlacementDecision::Counter);

    auto tmb = ep.get_memory_bank<TofinoMemoryBank>(Tofino);
    tmb->save_implementation(counter);
  }

  CounterRef get_or_build_counter(const ExecutionPlan &ep, BDD::Node_ptr node,
                                  addr_t obj,
                                  std::pair<bool, uint64_t> max_value,
                                  klee::ref<klee::Expr> value) const {
    auto mb = ep.get_memory_bank<TofinoMemoryBank>(Tofino);
    auto impls = mb->get_implementations(obj);

    assert(impls.size() <= 1);

    if (impls.size() == 0) {
      auto vector_config = BDD::symbex::get_vector_config(ep.get_bdd(), obj);
      auto _capacity = vector_config.capacity;
      auto _counter = Counter::build(obj, {node->get_id()}, _capacity,
                                     value->getWidth(), max_value);
      return _counter;
    }

    assert(impls[0]->get_type() == DataStructure::COUNTER);
    auto _counter = std::dynamic_pointer_cast<Counter>(impls[0]);
    _counter->add_nodes({node->get_id()});

    return _counter;
  }

  processing_result_t process(const ExecutionPlan &ep,
                              BDD::Node_ptr node) override {
    processing_result_t result;

    auto casted = BDD::cast_node<BDD::Call>(node);

    if (!casted) {
      return result;
    }

    auto call = casted->get_call();

    // We don't care about vector returns, they are handled by the counter read
    // module (it makes sure to ignore them).
    if (call.function_name != BDD::symbex::FN_VECTOR_BORROW) {
      return result;
    }

    assert(!call.args[BDD::symbex::FN_VECTOR_ARG_VECTOR].expr.isNull());
    auto _vector = call.args[BDD::symbex::FN_VECTOR_ARG_VECTOR].expr;
    auto _vector_addr = kutil::expr_addr_to_obj_addr(_vector);

    if (!can_implement(ep, _vector_addr)) {
      return result;
    }

    auto counter_data = is_counter(ep, _vector_addr);

    if (!counter_data.valid) {
      return result;
    }

    auto found_it = std::find_if(
        counter_data.writes.begin(), counter_data.writes.end(),
        [&](BDD::Node_ptr write) { return write->get_id() == node->get_id(); });

    if (found_it == counter_data.writes.end()) {
      return result;
    }

    assert(!call.args[BDD::symbex::FN_VECTOR_ARG_INDEX].expr.isNull());
    assert(!call.extra_vars[BDD::symbex::FN_VECTOR_EXTRA].second.isNull());

    auto _index = call.args[BDD::symbex::FN_VECTOR_ARG_INDEX].expr;
    auto _borrowed_cell = call.extra_vars[BDD::symbex::FN_VECTOR_EXTRA].second;

    auto _counter = get_or_build_counter(
        ep, node, _vector_addr, counter_data.max_value, _borrowed_cell);

    auto new_module = std::make_shared<CounterIncrement>(node, _counter, _index,
                                                         _borrowed_cell);
    auto new_ep = ep.add_leaf(new_module, node->get_next());

    save_decision(new_ep, _counter);

    result.module = new_module;
    result.next_eps.push_back(new_ep);

    // Graphviz::visualize(new_ep);

    return result;
  }

public:
  virtual void visit(ExecutionPlanVisitor &visitor,
                     const ExecutionPlanNode *ep_node) const override {
    visitor.visit(ep_node, this);
  }

  virtual Module_ptr clone() const override {
    auto cloned = new CounterIncrement(node, counter, index, value);
    return std::shared_ptr<Module>(cloned);
  }

  virtual bool equals(const Module *other) const override {
    if (other->get_type() != type) {
      return false;
    }

    auto other_cast = static_cast<const CounterIncrement *>(other);

    if (!counter->equals(other_cast->get_counter().get())) {
      return false;
    }

    return true;
  }

  CounterRef get_counter() const { return counter; }
  klee::ref<klee::Expr> get_index() const { return index; }
  klee::ref<klee::Expr> get_value() const { return value; }
};

} // namespace tofino
} // namespace targets
} // namespace synapse
