#pragma once

#include "../module.h"
#include "ignore.h"
#include "memory_bank.h"

namespace synapse {
namespace targets {
namespace tofino {

class IntegerAllocatorQuery : public Module {
private:
  IntegerAllocatorRef int_allocator;
  klee::ref<klee::Expr> index;
  BDD::symbol_t is_allocated;

public:
  IntegerAllocatorQuery()
      : Module(ModuleType::Tofino_IntegerAllocatorQuery, TargetType::Tofino,
               "IntegerAllocatorQuery") {}

  IntegerAllocatorQuery(BDD::BDDNode_ptr node,
                        IntegerAllocatorRef _int_allocator,
                        klee::ref<klee::Expr> _index,
                        const BDD::symbol_t &_is_allocated)
      : Module(ModuleType::Tofino_IntegerAllocatorQuery, TargetType::Tofino,
               "IntegerAllocatorQuery", node),
        int_allocator(_int_allocator), index(_index),
        is_allocated(_is_allocated) {}

private:
  bool can_place(const ExecutionPlan &ep, obj_addr_t obj,
                 IntegerAllocatorRef &implementation) const {
    auto mb = ep.get_memory_bank<TofinoMemoryBank>(Tofino);

    auto possible = mb->check_implementation_compatibility(
        obj, {
                 DataStructure::Type::INTEGER_ALLOCATOR,
                 DataStructure::Type::TABLE,
             });

    if (!possible) {
      return false;
    }

    auto impls = mb->get_implementations(obj);

    for (auto impl : impls) {
      if (impl->get_type() == DataStructure::INTEGER_ALLOCATOR) {
        implementation = std::dynamic_pointer_cast<IntegerAllocator>(impl);
      }
    }

    return true;
  }

  void save_decision(const ExecutionPlan &ep,
                     DataStructureRef int_allocator) const {
    auto mb = ep.get_memory_bank<TofinoMemoryBank>(Tofino);
    mb->save_implementation(int_allocator);
  }

  processing_result_t process_call(const ExecutionPlan &ep,
                                   BDD::BDDNode_ptr node,
                                   const BDD::Call *casted) override {
    processing_result_t result;
    auto call = casted->get_call();

    if (call.function_name != symbex::FN_DCHAIN_IS_ALLOCATED) {
      return result;
    }

    assert(!call.args[symbex::FN_DCHAIN_ARG_CHAIN].expr.isNull());
    assert(!call.args[symbex::FN_DCHAIN_ARG_INDEX].expr.isNull());
    assert(!call.ret.isNull());

    auto _dchain = call.args[symbex::FN_DCHAIN_ARG_CHAIN].expr;
    auto _index = call.args[symbex::FN_DCHAIN_ARG_INDEX].expr;
    auto _generated_symbols = casted->get_local_generated_symbols();

    auto _dchain_addr = kutil::expr_addr_to_obj_addr(_dchain);
    auto _is_allocated =
        get_symbol(_generated_symbols, symbex::DCHAIN_IS_INDEX_ALLOCATED);

    IntegerAllocatorRef _int_allocator;

    if (!can_place(ep, _dchain_addr, _int_allocator)) {
      return result;
    }

    if (!_int_allocator) {
      _int_allocator = IntegerAllocator::build(_dchain_addr, {node->get_id()});
    } else {
      _int_allocator->add_nodes({node->get_id()});
    }

    auto new_module = std::make_shared<IntegerAllocatorQuery>(
        node, _int_allocator, _index, _is_allocated);
    auto new_ep = ep.add_leaves(new_module, node->get_next());

    save_decision(new_ep, _int_allocator);

    result.module = new_module;
    result.next_eps.push_back(new_ep);

    return result;
  }

public:
  virtual void visit(ExecutionPlanVisitor &visitor) const override {
    visitor.visit(this);
  }

  virtual Module_ptr clone() const override {
    auto cloned =
        new IntegerAllocatorQuery(node, int_allocator, index, is_allocated);
    return std::shared_ptr<Module>(cloned);
  }

  virtual bool equals(const Module *other) const override {
    if (other->get_type() != type) {
      return false;
    }

    auto other_cast = static_cast<const IntegerAllocatorQuery *>(other);

    auto other_int_allocator = other_cast->get_int_allocator();

    if (!int_allocator->equals(other_int_allocator.get())) {
      return false;
    }

    if (!kutil::solver_toolbox.are_exprs_always_equal(
            index, other_cast->get_index())) {
      return false;
    }

    if (is_allocated.label != other_cast->get_is_allocated().label) {
      return false;
    }

    return true;
  }

  IntegerAllocatorRef get_int_allocator() const { return int_allocator; }
  klee::ref<klee::Expr> get_index() const { return index; }
  const BDD::symbol_t &get_is_allocated() const { return is_allocated; }
};

} // namespace tofino
} // namespace targets
} // namespace synapse
