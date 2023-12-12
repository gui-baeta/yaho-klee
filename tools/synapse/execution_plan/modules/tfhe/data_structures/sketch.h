#pragma once

#include "data_structure.h"

namespace synapse {
namespace targets {
namespace tfhe {

struct sketch_t : ds_t {
  uint64_t index_range;

  sketch_t(addr_t _addr, BDD::node_id_t _node_id, uint64_t _index_range)
      : ds_t(ds_type_t::DCHAIN, _addr, _node_id), index_range(_index_range) {}
};

} // namespace tfhe
} // namespace targets
} // namespace synapse