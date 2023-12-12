#pragma once

#include "../internals/internals.h"

#include "current_time.h"
#include "dchain_allocate.h"
#include "dchain_allocate_new_index.h"
#include "dchain_free_index.h"
#include "dchain_is_index_allocated.h"
#include "dchain_rejuvenate_index.h"
#include "expire_items_single_map.h"
#include "expire_items_single_map_iteratively.h"
#include "map_allocate.h"
#include "map_get.h"
#include "map_put.h"
#include "nf_set_rte_ipv4_tcpudp_checksum.h"
#include "packet_borrow_next_chunk.h"
#include "packet_borrow_next_secret.h"
#include "packet_return_chunk.h"
#include "vector_allocate.h"
#include "vector_borrow.h"
#include "vector_return.h"

namespace BDD {
namespace emulation {

inline operations_t get_operations() {
  return {
      current_time(),
      packet_borrow_next_chunk(),
      packet_borrow_next_secret(),
      packet_return_chunk(),
      map_allocate(),
      vector_allocate(),
      dchain_allocate(),
      expire_items_single_map(),
      map_get(),
      dchain_allocate_new_index(),
      vector_borrow(),
      vector_return(),
      map_put(),
      dchain_rejuvenate_index(),
      dchain_is_index_allocated(),
      nf_set_rte_ipv4_tcpudp_checksum(),
      dchain_free_index(),
      expire_items_single_map_iteratively(),
  };
}

} // namespace emulation
} // namespace BDD