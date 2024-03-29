#include "node.h"
#include "branch.h"
#include "call.h"

#include "../symbol-factory.h"

#include "llvm/Support/MD5.h"

#include <iomanip>

namespace BDD {

// Get generated symbols, but no further than this node
symbols_t Node::get_generated_symbols(
    const std::unordered_set<node_id_t> &furthest_back_nodes) const {
  symbols_t symbols;
  const Node *node = this;

  // hack: symbols always known
  klee::ref<klee::Expr> empty_expr;
  symbols.emplace("VIGOR_DEVICE", "VIGOR_DEVICE", empty_expr);
  symbols.emplace("pkt_len", "pkt_len", empty_expr);
  symbols.emplace("data_len", "data_len", empty_expr);
  symbols.emplace("received_a_packet", "received_a_packet", empty_expr);

  while (node) {
    if (furthest_back_nodes.size()) {
      auto found_it = std::find(furthest_back_nodes.begin(),
                                furthest_back_nodes.end(), node->get_id());
      if (found_it != furthest_back_nodes.end()) {
        break;
      }
    }

    if (node->get_type() == Node::NodeType::CALL) {
      const Call *call = static_cast<const Call *>(node);
      auto more_symbols = call->get_local_generated_symbols();

      for (auto symbol : more_symbols) {
        symbols.insert(symbol);
      }
    }

    node = node->get_prev().get();
  }

  return symbols;
}

symbols_t Node::get_generated_symbols() const {
  std::unordered_set<node_id_t> furthest_back_nodes;
  return get_generated_symbols(furthest_back_nodes);
}

symbols_t Node::get_local_generated_symbols() const { return symbols_t(); }

std::vector<node_id_t> Node::get_terminating_node_ids() const {
  if (!next) {
    return std::vector<node_id_t>{id};
  }

  return next->get_terminating_node_ids();
}

bool Node::is_reachable_by_node(node_id_t id) const {
  auto node = this;

  while (node) {
    if (node->get_id() == id) {
      return true;
    }

    node = node->get_prev().get();
  }

  return false;
}

void Node::update_id(node_id_t new_id) {
  SymbolFactory factory;
  auto symbols = factory.get_symbols(this);

  id = new_id;

  if (symbols.size() == 0) {
    return;
  }

  kutil::RenameSymbols renamer;
  for (auto symbol : symbols) {
    auto new_label = factory.translate_label(symbol.label_base, this);

    if (new_label == symbol.label) {
      continue;
    }

    renamer.add_translation(symbol.label, new_label);
  }

  if (renamer.get_translations().size() == 0) {
    return;
  }

  factory.translate(this, this, renamer);
}

unsigned Node::count_children(bool recursive) const {
  std::vector<const Node *> children;
  const Node *node = this;

  if (node->get_type() == Node::NodeType::BRANCH) {
    auto branch_node = static_cast<const Branch *>(node);

    children.push_back(branch_node->get_on_true().get());
    children.push_back(branch_node->get_on_false().get());
  } else if (node->get_next()) {
    children.push_back(node->get_next().get());
  }

  unsigned n = children.size();

  while (recursive && children.size()) {
    node = children[0];
    children.erase(children.begin());

    if (node->get_type() == Node::NodeType::BRANCH) {
      auto branch_node = static_cast<const Branch *>(node);

      children.push_back(branch_node->get_on_true().get());
      children.push_back(branch_node->get_on_false().get());

      n += 2;
    } else if (node->get_next()) {
      children.push_back(node->get_next().get());
      n++;
    }
  }

  return n;
}

unsigned Node::count_code_paths() const {
  std::vector<const Node *> nodes{this};
  const Node *node;
  unsigned paths = 0;

  while (nodes.size()) {
    node = nodes[0];
    nodes.erase(nodes.begin());

    switch (node->get_type()) {
    case Node::NodeType::BRANCH: {
      auto branch_node = static_cast<const Branch *>(node);

      nodes.push_back(branch_node->get_on_true().get());
      nodes.push_back(branch_node->get_on_false().get());
    } break;
    case Node::NodeType::CALL: {
      nodes.push_back(node->get_next().get());
    } break;
    case Node::NodeType::RETURN_INIT:
    case Node::NodeType::RETURN_PROCESS:
    case Node::NodeType::RETURN_RAW:
      paths++;
      break;
    }
  }

  return paths;
}

std::string Node::process_call_path_filename(std::string call_path_filename) {
  std::string dir_delim = "/";
  std::string ext_delim = ".";

  auto dir_found = call_path_filename.find_last_of(dir_delim);
  if (dir_found != std::string::npos) {
    call_path_filename =
        call_path_filename.substr(dir_found + 1, call_path_filename.size());
  }

  auto ext_found = call_path_filename.find_last_of(ext_delim);
  if (ext_found != std::string::npos) {
    call_path_filename = call_path_filename.substr(0, ext_found);
  }

  return call_path_filename;
}

std::string Node::dump_recursive(int lvl) const {
  std::stringstream result;

  auto pad = std::string(lvl * 2, ' ');

  result << pad << dump(true) << "\n";

  if (next) {
    result << next->dump_recursive(lvl + 1);
  }

  return result.str();
}

klee::ConstraintManager Node::get_constraints() const {
  klee::ConstraintManager accumulated;

  const Node *node = this;

  for (auto c : node->get_node_constraints()) {
    accumulated.addConstraint(c);
  }

  const Node *prev = node->get_prev().get();

  while (prev) {
    if (prev->get_type() == NodeType::BRANCH) {
      auto prev_branch = static_cast<const Branch *>(prev);

      auto on_true = prev_branch->get_on_true();
      auto on_false = prev_branch->get_on_false();

      assert(on_true);
      assert(on_false);

      auto condition = prev_branch->get_condition();
      assert(!condition.isNull());

      if (on_true->get_id() == node->get_id()) {
        accumulated.addConstraint(condition);
      } else {
        assert(on_false->get_id() == node->get_id());
        auto not_condition = kutil::solver_toolbox.exprBuilder->Not(condition);
        accumulated.addConstraint(not_condition);
      }
    }

    for (auto c : prev->get_node_constraints()) {
      accumulated.addConstraint(c);
    }

    node = prev;
    prev = prev->get_prev().get();
  }

  return accumulated;
}

std::string Node::hash(bool recursive) const {
  std::stringstream input;
  std::stringstream output;
  std::vector<const Node *> nodes;

  if (recursive) {
    nodes.push_back(this);
  } else {
    input << id << ":";
  }

  while (nodes.size()) {
    auto node = nodes[0];
    nodes.erase(nodes.begin());

    input << node->get_id() << ":";

    switch (node->get_type()) {
    case Node::NodeType::BRANCH: {
      auto branch_node = static_cast<const Branch *>(node);

      nodes.push_back(branch_node->get_on_true().get());
      nodes.push_back(branch_node->get_on_false().get());
    } break;
    case Node::NodeType::CALL: {
      nodes.push_back(node->get_next().get());
    } break;
    default:
      break;
    }
  }

  llvm::MD5 checksum;
  checksum.update(input.str());

  llvm::MD5::MD5Result result;
  checksum.final(result);

  output << std::hex << std::setfill('0');

  for (auto byte : result) {
    output << std::hex << std::setw(2) << static_cast<int>(byte);
  }

  return output.str();
}

} // namespace BDD