#pragma once

#include "table.h"

namespace synapse {
namespace targets {
namespace tofino {

class TableIsAllocated : public TableModule {
public:
  TableIsAllocated()
      : TableModule(ModuleType::Tofino_TableRejuvenation, "TableIsAllocated") {}

  TableIsAllocated(BDD::Node_ptr node, TableRef _table)
      : TableModule(ModuleType::Tofino_TableRejuvenation, "TableIsAllocated",
                    node, _table) {}

protected:
  extracted_data_t extract_data(const ExecutionPlan &ep,
                                BDD::Node_ptr node) const {
    auto extractors = {
        &TableIsAllocated::extract_from_dchain_is_index_allocated,
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

    return data;
  }

  TableRef build_table(const extracted_data_t &data) const {
    auto table = Table::build("is_allocated", data.keys, data.values, data.hit,
                              {data.obj}, data.nodes);
    return table;
  }

  processing_result_t process(const ExecutionPlan &ep,
                              BDD::Node_ptr node) override {
    processing_result_t result;

    auto data = extract_data(ep, node);

    if (!data.valid) {
      return result;
    }

    if (!check_compatible_placements_decisions(ep, {data.obj},
                                               DataStructure::Type::TABLE)) {
      return result;
    }

    auto table = build_table(data);

    if (!table) {
      return result;
    }

    auto new_module = std::make_shared<TableIsAllocated>(node, table);
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
    auto cloned = new TableIsAllocated(node, table);
    return std::shared_ptr<Module>(cloned);
  }

  virtual bool equals(const Module *other) const override {
    if (other->get_type() != type) {
      return false;
    }

    auto other_cast = static_cast<const TableIsAllocated *>(other);

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
