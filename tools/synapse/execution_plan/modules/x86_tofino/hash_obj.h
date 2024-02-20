#pragma once

#include "../module.h"
#include "memory_bank.h"

namespace synapse {
namespace targets {
namespace x86_tofino {

class HashObj : public Module {
private:
  klee::ref<klee::Expr> input;
  bits_t size;
  klee::ref<klee::Expr> hash;

public:
  HashObj()
      : Module(ModuleType::x86_Tofino_HashObj, TargetType::x86_Tofino,
               "HashObj") {}

  HashObj(BDD::Node_ptr node, klee::ref<klee::Expr> _input, bits_t _size,
          klee::ref<klee::Expr> _hash)
      : Module(ModuleType::x86_Tofino_HashObj, TargetType::x86_Tofino,
               "HashObj", node),
        input(_input), size(_size), hash(_hash) {}

private:
  processing_result_t process(const ExecutionPlan &ep,
                              BDD::Node_ptr node) override {
    processing_result_t result;

    auto casted = BDD::cast_node<BDD::Call>(node);

    if (!casted) {
      return result;
    }

    auto call = casted->get_call();

    if (call.function_name == BDD::symbex::FN_HASH_OBJ) {
      assert(!call.args[BDD::symbex::FN_HASH_OBJ_ARG_OBJ].expr.isNull());
      assert(!call.args[BDD::symbex::FN_HASH_OBJ_ARG_OBJ].in.isNull());
      assert(!call.args[BDD::symbex::FN_HASH_OBJ_ARG_SIZE].expr.isNull());
      assert(!call.ret.isNull());

      auto _input = call.args[BDD::symbex::FN_HASH_OBJ_ARG_OBJ].in;
      auto _size = call.args[BDD::symbex::FN_HASH_OBJ_ARG_SIZE].expr;
      auto _hash = call.ret;

      assert(kutil::is_constant(_size));
      auto _size_value = kutil::solver_toolbox.value_from_expr(_size);

      auto new_module =
          std::make_shared<HashObj>(node, _input, _size_value, _hash);
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
    auto cloned = new HashObj(node, input, size, hash);
    return std::shared_ptr<Module>(cloned);
  }

  virtual bool equals(const Module *other) const override {
    if (other->get_type() != type) {
      return false;
    }

    auto other_cast = static_cast<const HashObj *>(other);

    if (!kutil::solver_toolbox.are_exprs_always_equal(
            input, other_cast->get_input())) {
      return false;
    }

    if (size != other_cast->get_size()) {
      return false;
    }

    if (!kutil::solver_toolbox.are_exprs_always_equal(hash,
                                                      other_cast->get_hash())) {
      return false;
    }

    return true;
  }

  klee::ref<klee::Expr> get_input() const { return input; }
  bits_t get_size() const { return size; }
  klee::ref<klee::Expr> get_hash() const { return hash; }
};
} // namespace x86_tofino
} // namespace targets
} // namespace synapse
