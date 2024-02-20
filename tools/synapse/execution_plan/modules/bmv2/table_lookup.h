#pragma once

#include "../module.h"
#include "ignore.h"

namespace synapse {
namespace targets {
namespace bmv2 {

class TableLookup : public Module {
public:
  struct key_t {
    klee::ref<klee::Expr> expr;
    klee::ref<klee::Expr> condition;

    key_t(klee::ref<klee::Expr> _expr, klee::ref<klee::Expr> _condition)
        : expr(_expr), condition(_condition) {}

    key_t(klee::ref<klee::Expr> _expr) : expr(_expr) {}
  };

  struct param_t {
    std::vector<klee::ref<klee::Expr>> exprs;

    param_t(klee::ref<klee::Expr> _expr) { exprs.push_back(_expr); }

    void add_expr(klee::ref<klee::Expr> _expr) {
      for (auto expr : exprs) {
        assert(expr->getWidth() == _expr->getWidth());
      }

      exprs.push_back(_expr);
    }
  };

private:
  uint64_t table_id;
  klee::ref<klee::Expr> obj;
  std::vector<key_t> keys;
  std::vector<param_t> params;
  std::vector<std::string> map_has_this_key_labels;
  std::string bdd_function;

public:
  TableLookup()
      : Module(ModuleType::BMv2_TableLookup, TargetType::BMv2, "TableLookup") {}

  TableLookup(BDD::Node_ptr node, uint64_t _table_id,
              klee::ref<klee::Expr> _obj, const std::vector<key_t> &_keys,
              const std::vector<param_t> &_params,
              const std::vector<std::string> &_map_has_this_key_labels,
              const std::string &_bdd_function)
      : Module(ModuleType::BMv2_TableLookup, TargetType::BMv2, "TableLookup",
               node),
        table_id(_table_id), obj(_obj), keys(_keys), params(_params),
        map_has_this_key_labels(_map_has_this_key_labels),
        bdd_function(_bdd_function) {}

private:
  bool multiple_queries_to_this_table(BDD::Node_ptr current_node,
                                      uint64_t _table_id) const {
    assert(current_node);
    auto node = current_node->get_prev();

    unsigned int counter = 0;

    while (node) {
      if (node->get_type() != BDD::Node::NodeType::CALL) {
        node = node->get_prev();
        continue;
      }

      auto call_node = static_cast<const BDD::Call *>(node.get());
      auto call = call_node->get_call();

      if (call.function_name != BDD::symbex::FN_MAP_GET &&
          call.function_name != BDD::symbex::FN_MAP_PUT &&
          call.function_name != BDD::symbex::FN_VECTOR_BORROW) {
        node = node->get_prev();
        continue;
      }

      uint64_t this_table_id = 0;
      if (call.function_name == BDD::symbex::FN_MAP_GET ||
          call.function_name == BDD::symbex::FN_MAP_PUT) {
        assert(!call.args[BDD::symbex::FN_MAP_ARG_MAP].expr.isNull());
        this_table_id = kutil::solver_toolbox.value_from_expr(
            call.args[BDD::symbex::FN_MAP_ARG_MAP].expr);
      } else if (call.function_name == BDD::symbex::FN_VECTOR_BORROW) {
        assert(!call.args[BDD::symbex::FN_VECTOR_ARG_VECTOR].expr.isNull());
        this_table_id = kutil::solver_toolbox.value_from_expr(
            call.args[BDD::symbex::FN_VECTOR_ARG_VECTOR].expr);
      }

      if (this_table_id == _table_id) {
        counter++;
      }

      if (counter > 1) {
        return true;
      }

      node = node->get_prev();
    }

    return false;
  }

  std::pair<bool, TableLookup *>
  can_be_merged(const ExecutionPlan &ep, BDD::Node_ptr node,
                klee::ref<klee::Expr> _obj) const {
    std::pair<bool, TableLookup *> result = std::make_pair(false, nullptr);

    auto mb = ep.get_memory_bank();
    auto reorder_data = mb->get_reorder_data(node->get_id());

    if (!reorder_data.valid) {
      return result;
    }

    auto active_leaf = ep.get_active_leaf();
    assert(active_leaf);

    auto module = active_leaf->get_module();
    assert(module);

    if (module->get_type() != Module::ModuleType::BMv2_TableLookup) {
      return result;
    }

    auto prev_table_lookup = static_cast<TableLookup *>(module.get());
    auto prev_obj = prev_table_lookup->obj;

    auto eq = kutil::solver_toolbox.are_exprs_always_equal(_obj, prev_obj);

    if (eq) {
      result = std::make_pair(true, prev_table_lookup);
    }

    return result;
  }

  bool process_map_get(const ExecutionPlan &ep, BDD::Node_ptr node,
                       const BDD::Call *casted, processing_result_t &result) {
    auto call = casted->get_call();

    if (call.function_name != BDD::symbex::FN_MAP_GET) {
      return false;
    }

    assert(call.function_name == BDD::symbex::FN_MAP_GET);
    assert(!call.args[BDD::symbex::FN_MAP_ARG_MAP].expr.isNull());
    assert(!call.args[BDD::symbex::FN_MAP_ARG_KEY].in.isNull());
    assert(!call.args[BDD::symbex::FN_MAP_ARG_OUT].out.isNull());

    auto _map = call.args[BDD::symbex::FN_MAP_ARG_MAP].expr;
    auto _key = call.args[BDD::symbex::FN_MAP_ARG_KEY].in;
    auto _value = call.args[BDD::symbex::FN_MAP_ARG_OUT].out;

    assert(_map->getKind() == klee::Expr::Kind::Constant);
    auto _map_value = kutil::solver_toolbox.value_from_expr(_map);

    if (multiple_queries_to_this_table(node, _map_value)) {
      return false;
    }

    auto symbols = casted->get_local_generated_symbols();
    assert(symbols.size() == 2);

    auto symbols_it = symbols.begin();
    assert(symbols_it->label_base == BDD::symbex::MAP_HAS_THIS_KEY);
    auto _map_has_this_key_label = symbols_it->label;

    auto merged_response = can_be_merged(ep, node, _map);
    if (merged_response.first) {
      auto _table_id = _map_value;

      auto mb = ep.get_memory_bank();
      auto reorder_data = mb->get_reorder_data(node->get_id());
      assert(reorder_data.valid);
      auto _key_condition = reorder_data.condition;

      auto _keys = merged_response.second->keys;
      _keys.emplace_back(_key, _key_condition);

      auto _params = merged_response.second->params;
      // TODO: maybe it's not the last one? look for the right one
      _params.back().add_expr(_value);

      auto _map_has_this_key_labels =
          merged_response.second->map_has_this_key_labels;
      _map_has_this_key_labels.push_back(_map_has_this_key_label);

      auto new_module = std::make_shared<TableLookup>(
          node, _table_id, _map, _keys, _params, _map_has_this_key_labels,
          call.function_name);

      auto new_ep = ep.replace_leaf(new_module, node->get_next());

      result.next_eps.push_back(new_ep);
    }

    auto _table_id = node->get_id();

    std::vector<key_t> _keys;

    auto mb = ep.get_memory_bank();
    auto reorder_data = mb->get_reorder_data(node->get_id());

    if (reorder_data.valid) {
      auto _key_condition = reorder_data.condition;
      _keys.emplace_back(_key, _key_condition);
    } else {
      _keys.emplace_back(_key);
    }

    std::vector<param_t> _params{_value};
    std::vector<std::string> _map_has_this_key_labels{_map_has_this_key_label};

    auto new_module = std::make_shared<TableLookup>(
        node, _table_id, _map, _keys, _params, _map_has_this_key_labels,
        call.function_name);

    auto new_ep = ep.add_leaf(new_module, node->get_next());

    result.module = new_module;
    result.next_eps.push_back(new_ep);

    return true;
  }

  bool process_vector_borrow(const ExecutionPlan &ep, BDD::Node_ptr node,
                             const BDD::Call *casted,
                             processing_result_t &result) {
    auto call = casted->get_call();

    if (call.function_name != BDD::symbex::FN_VECTOR_BORROW) {
      return false;
    }

    assert(call.function_name == BDD::symbex::FN_VECTOR_BORROW);
    assert(!call.args[BDD::symbex::FN_VECTOR_ARG_VECTOR].expr.isNull());
    assert(!call.args[BDD::symbex::FN_VECTOR_ARG_INDEX].expr.isNull());
    assert(!call.extra_vars[BDD::symbex::FN_VECTOR_EXTRA].second.isNull());

    auto _vector = call.args[BDD::symbex::FN_VECTOR_ARG_VECTOR].expr;
    auto _index = call.args[BDD::symbex::FN_VECTOR_ARG_INDEX].expr;
    auto _borrowed_cell = call.extra_vars[BDD::symbex::FN_VECTOR_EXTRA].second;

    assert(_vector->getKind() == klee::Expr::Kind::Constant);
    auto _vector_value = kutil::solver_toolbox.value_from_expr(_vector);

    if (multiple_queries_to_this_table(node, _vector_value)) {
      return false;
    }

    auto merged_response = can_be_merged(ep, node, _vector);

    if (merged_response.first) {
      auto _table_id = _vector_value;

      auto mb = ep.get_memory_bank();
      auto reorder_data = mb->get_reorder_data(node->get_id());
      assert(reorder_data.valid);
      auto _key_condition = reorder_data.condition;

      auto _keys = merged_response.second->keys;
      _keys.emplace_back(_index, _key_condition);

      auto _params = merged_response.second->params;
      assert(_params.size());
      // TODO: maybe it's not the last one? look for the right one
      _params.back().add_expr(_borrowed_cell);

      auto _map_has_this_key_labels =
          merged_response.second->map_has_this_key_labels;

      auto new_module = std::make_shared<TableLookup>(
          node, _table_id, _vector, _keys, _params, _map_has_this_key_labels,
          call.function_name);

      auto new_ep = ep.replace_leaf(new_module, node->get_next());

      result.next_eps.push_back(new_ep);
    }

    auto _table_id = node->get_id();

    std::vector<key_t> _keys;

    auto mb = ep.get_memory_bank();
    auto reorder_data = mb->get_reorder_data(node->get_id());

    if (reorder_data.valid) {
      auto _key_condition = reorder_data.condition;
      _keys.emplace_back(_index, _key_condition);
    } else {
      _keys.emplace_back(_index);
    }

    std::vector<param_t> _params;
    _params.emplace_back(_borrowed_cell);

    std::vector<std::string> _map_has_this_key_labels;

    auto new_module = std::make_shared<TableLookup>(
        node, _table_id, _vector, _keys, _params, _map_has_this_key_labels,
        call.function_name);

    auto new_ep = ep.add_leaf(new_module, node->get_next());

    result.module = new_module;
    result.next_eps.push_back(new_ep);

    return true;
  }

  processing_result_t process(const ExecutionPlan &ep,
                              BDD::Node_ptr node) override {
    processing_result_t result;

    auto casted = BDD::cast_node<BDD::Call>(node);

    if (!casted) {
      return result;
    }

    if (process_map_get(ep, node, casted, result)) {
      return result;
    }

    if (process_vector_borrow(ep, node, casted, result)) {
      return result;
    }

    return result;
  }

public:
  virtual void visit(ExecutionPlanVisitor &visitor,
                     const ExecutionPlanNode *ep_node) const override {
    visitor.visit(ep_node, this);
  }

  virtual Module_ptr clone() const override {
    auto cloned = new TableLookup(node, table_id, obj, keys, params,
                                  map_has_this_key_labels, bdd_function);
    return std::shared_ptr<Module>(cloned);
  }

  virtual bool equals(const Module *other) const override {
    if (other->get_type() != type) {
      return false;
    }

    auto other_cast = static_cast<const TableLookup *>(other);

    if (table_id != other_cast->get_table_id()) {
      return false;
    }

    if (!kutil::solver_toolbox.are_exprs_always_equal(obj, other_cast->obj)) {
      return false;
    }

    auto other_keys = other_cast->get_keys();

    if (keys.size() != other_keys.size()) {
      return false;
    }

    for (auto i = 0u; i < keys.size(); i++) {
      if (!kutil::solver_toolbox.are_exprs_always_equal(keys[i].expr,
                                                        other_keys[i].expr)) {
        return false;
      }
    }

    auto other_params = other_cast->get_params();

    if (params.size() != other_params.size()) {
      return false;
    }

    for (auto i = 0u; i < params.size(); i++) {
      if (params[i].exprs.size() != other_params[i].exprs.size()) {
        return false;
      }

      for (auto j = 0u; j < params[i].exprs.size(); j++) {
        if (!kutil::solver_toolbox.are_exprs_always_equal(
                params[i].exprs[j], other_params[i].exprs[j])) {
          return false;
        }
      }
    }

    auto other_map_has_this_key_labels =
        other_cast->get_map_has_this_key_labels();

    if (map_has_this_key_labels.size() !=
        other_map_has_this_key_labels.size()) {
      return false;
    }

    for (auto i = 0u; i < map_has_this_key_labels.size(); i++) {
      if (map_has_this_key_labels[i] != other_map_has_this_key_labels[i]) {
        return false;
      }
    }

    if (bdd_function != other_cast->get_bdd_function()) {
      return false;
    }

    return true;
  }

  uint64_t get_table_id() const { return table_id; }
  const klee::ref<klee::Expr> &get_obj() const { return obj; }
  const std::vector<key_t> &get_keys() const { return keys; }
  const std::vector<param_t> &get_params() const { return params; }
  const std::vector<std::string> &get_map_has_this_key_labels() const {
    return map_has_this_key_labels;
  }
  const std::string &get_bdd_function() const { return bdd_function; }
};
} // namespace bmv2
} // namespace targets
} // namespace synapse
