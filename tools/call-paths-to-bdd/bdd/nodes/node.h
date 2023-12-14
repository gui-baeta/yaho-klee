#pragma once

#include <iostream>
#include <unordered_set>
#include <vector>

#include "load-call-paths.h"
#include "symbol.h"

namespace BDD {

class Node;
class BDDVisitor;

typedef std::shared_ptr<Node> Node_ptr;
typedef uint64_t node_id_t;

class Node {
public:
  enum NodeType { BRANCH, CALL, RETURN_INIT, RETURN_PROCESS, RETURN_RAW };

protected:
  node_id_t id;
  NodeType type;

  Node_ptr next;
  Node_ptr prev;

  klee::ConstraintManager constraints;

public:
  Node(node_id_t _id, NodeType _type, klee::ConstraintManager _constraints)
      : id(_id), type(_type), constraints(_constraints) {}

  Node(node_id_t _id, NodeType _type, const Node_ptr &_next,
       const Node_ptr &_prev, klee::ConstraintManager _constraints)
      : id(_id), type(_type), next(_next), prev(_prev),
        constraints(_constraints) {}

  void replace_next(const Node_ptr &_next) {
    assert(_next);
    next = _next;
  }

  void add_next(const Node_ptr &_next) {
    assert(next == nullptr);
    assert(_next);
    next = _next;
  }

  void replace_prev(const Node_ptr &_prev) {
    assert(_prev);
    prev = _prev;
  }

  void add_prev(const Node_ptr &_prev) {
    assert(prev == nullptr);
    assert(_prev);
    prev = _prev;
  }

  virtual void disconnect() {
    prev = nullptr;
    next = nullptr;
  }

  const Node_ptr &get_next() const { return next; }
  const Node_ptr &get_next() { return next; }

  const Node_ptr &get_prev() const { return prev; }
  const Node_ptr &get_prev() { return prev; }

  NodeType get_type() const { return type; }
  node_id_t get_id() const { return id; }

  unsigned count_children(bool recursive = true) const;
  unsigned count_code_paths() const;

  const klee::ConstraintManager &get_node_constraints() const {
    return constraints;
  }

  void set_node_constraints(const klee::ConstraintManager &_constraints) {
    constraints = _constraints;
  }

  klee::ConstraintManager get_constraints() const;

  symbols_t get_generated_symbols() const;

  // Get generated symbols, but no further than any of these node IDs
  symbols_t get_generated_symbols(
      const std::unordered_set<node_id_t> &furthest_back_nodes) const;

  void update_id(node_id_t new_id);
  void process_call_paths(std::vector<call_path_t *> call_paths);
  bool is_reachable_by_node(node_id_t id) const;

  virtual symbols_t get_local_generated_symbols() const;
  virtual std::vector<node_id_t> get_terminating_node_ids() const;
  virtual Node_ptr clone(bool recursive = false) const = 0;
  virtual void recursive_update_ids(node_id_t &new_id) = 0;
  virtual void visit(BDDVisitor &visitor) const = 0;
  virtual std::string dump(bool one_liner = false) const = 0;
  virtual std::string dump_recursive(int lvl = 0) const;

  std::string hash(bool recursive = false) const;

  static std::string process_call_path_filename(std::string call_path_filename);

protected:
  friend class Call;
  friend class Branch;
  friend class ReturnRaw;
  friend class ReturnInit;
  friend class ReturnProcess;

  virtual std::string get_gv_name() const {
    std::stringstream ss;
    ss << id;
    return ss.str();
  }

  friend class SymbolFactory;
};
} // namespace BDD