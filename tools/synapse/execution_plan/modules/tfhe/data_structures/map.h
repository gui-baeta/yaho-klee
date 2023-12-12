#pragma once

#include "data_structure.h"

namespace synapse {
namespace targets {
namespace tfhe {

struct map_t : ds_t {
  bits_t value_size;

  map_t(addr_t _addr, bits_t _value_size, BDD::node_id_t _node_id)
      : ds_t(ds_type_t::MAP, _addr, _node_id), value_size(_value_size) {}
};

} // namespace tfhe
} // namespace targets
} // namespace synapse