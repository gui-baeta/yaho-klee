#pragma once

#include <klee/Expr.h>

#include <string>
#include <unordered_map>
#include <unordered_set>

#include "call-paths-to-bdd.h"
#include "klee-util.h"

namespace synapse {
namespace targets {
namespace tfhe {

enum ds_type_t {
  MAP,
  VECTOR,
  DCHAIN,
  SKETCH,
  CHT,
};

struct ds_t {
  ds_type_t type;
  addr_t addr;
  BDD::node_id_t node_id;

  ds_t(ds_type_t _type, addr_t _addr, BDD::node_id_t _node_id)
      : type(_type), addr(_addr), node_id(_node_id) {}

  bool matches(ds_type_t _type, addr_t _addr) const {
    if (type != _type) {
      return false;
    }

    return matches(_addr);
  }

  bool matches(addr_t _addr) const {
    if (addr != _addr) {
      return false;
    }

    return true;
  }
};

} // namespace tfhe
} // namespace targets
} // namespace synapse