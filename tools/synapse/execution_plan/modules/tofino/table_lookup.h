#pragma once

#include "table.h"

namespace synapse {
namespace targets {
namespace tofino {

class TableLookup : public TableModule {
public:
  TableLookup() : TableModule(ModuleType::Tofino_TableLookup, "TableLookup") {}

  TableLookup(BDD::Node_ptr node, TableRef _table)
      : TableModule(ModuleType::Tofino_TableLookup, "TableLookup", node,
                    _table) {}

protected:
  extracted_data_t extract_data(const ExecutionPlan &ep,
                                BDD::Node_ptr node) const {
    auto extractors = {
        &TableLookup::extract_from_map_get,
        &TableLookup::extract_from_vector_borrow,
    };

    extracted_data_t data;

    auto casted = BDD::cast_node<BDD::Call>(node);

    if (!casted) {
      return data;
    }

    for (auto &extractor : extractors) {
      data = (this->*extractor)(casted);

      if (data.valid) {
        break;
      }
    }

    auto coalescing_data = get_map_coalescing_data_t(ep, data.obj);

    if (coalescing_data.valid && can_coalesce(ep, coalescing_data)) {
      if (casted->get_call().function_name != BDD::symbex::FN_MAP_GET) {
        Graphviz::visualize(ep);
      }

      assert(casted->get_call().function_name == BDD::symbex::FN_MAP_GET);
      coalesce_with_incoming_vector_nodes(casted, coalescing_data, data);
    }

    return data;
  }

  TableRef build_table(const extracted_data_t &data) const {
    auto table = Table::build("map", data.keys, data.values, data.hit,
                              {data.obj}, data.nodes);
    return table;
  }

  processing_result_t process(const ExecutionPlan &ep,
                              BDD::Node_ptr node) override {
    processing_result_t result;

    if (already_coalesced(ep, DataStructure::Type::TABLE, node)) {
      return ignore(ep, node);
    }

    auto data = extract_data(ep, node);

    if (!data.valid) {
      return result;
    }

    auto table = build_table(data);

    if (!table) {
      return result;
    }

    if (!check_compatible_placements_decisions(ep, table->get_objs(),
                                               DataStructure::Type::TABLE)) {
      return result;
    }

    auto new_module = std::make_shared<TableLookup>(node, table);
    auto new_ep = ep.add_leaf(new_module, node->get_next());

    save_decision(new_ep, table);

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
    auto cloned = new TableLookup(node, table);
    return std::shared_ptr<Module>(cloned);
  }

  virtual bool equals(const Module *other) const override {
    if (other->get_type() != type) {
      return false;
    }

    auto other_cast = static_cast<const TableLookup *>(other);

    if (!table->equals(other_cast->get_table().get())) {
      return false;
    }

    return true;
  }

  TableRef get_table() const { return table; }
};

} // namespace tofino
} // namespace targets
} // namespace synapse
