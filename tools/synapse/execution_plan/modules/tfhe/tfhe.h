#pragma once

#include "../../target.h"
#include "../module.h"

#include "broadcast.h"
#include "cht_find_backend.h"
#include "packet_borrow_next_chunk.h"
#include "packet_borrow_next_secret.h"

// This need to come before MonoPBS since MonoPBS uses them
#include "operation.h"
#include "conditional.h"
#include "change.h"
#include "no_change.h"
// ---

#include "mono_pbs.h"
#include "aided_univariate_pbs.h"
#include "current_time.h"
#include "dchain_allocate_new_index.h"
#include "dchain_free_index.h"
#include "dchain_is_index_allocated.h"
#include "dchain_rejuvenate_index.h"
#include "drop.h"
#include "else.h"
#include "expire_items_single_map.h"
#include "expire_items_single_map_iteratively.h"
#include "forward.h"
#include "hash_obj.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#include "if.h"
#pragma GCC diagnostic pop

#include "load_balanced_flow_hash.h"
#include "map_erase.h"
#include "map_get.h"
#include "map_put.h"
#include "nf_set_rte_ipv4_udptcp_checksum.h"
#include "packet_get_unread_length.h"
#include "packet_return_chunk.h"
#include "no_op_packet_return_chunk.h"
#include "rte_ether_addr_hash.h"
#include "sketch_compute_hashes.h"
#include "sketch_expire.h"
#include "sketch_fetch.h"
#include "sketch_refresh.h"
#include "sketch_touch_buckets.h"
#include "then.h"
#include "truth_table_pbs.h"
#include "vector_borrow.h"
#include "vector_return.h"

#include "memory_bank.h"

namespace synapse {
namespace targets {
namespace tfhe {

class tfheTarget : public Target {
public:
  tfheTarget() // TODO GUI add module here
      : Target(TargetType::tfhe,
               {
                   MODULE(MapGet),
                   MODULE(CurrentTime),
                   MODULE(PacketBorrowNextChunk),
                   MODULE(PacketBorrowNextSecret),
                   MODULE(PacketReturnChunk),
                   MODULE(NoOpPacketReturnChunk),
                   MODULE(TruthTablePBS),
                   MODULE(Conditional),
                   MODULE(MonoPBS),
                   MODULE(Change),
                   MODULE(NoChange),
                   MODULE(AidedUnivariatePBS),
                   MODULE(Operation),
                   MODULE(If),
                   MODULE(Then),
                   MODULE(Else),
                   MODULE(Forward),
                   MODULE(Broadcast),
                   MODULE(Drop),
                   MODULE(ExpireItemsSingleMap),
                   MODULE(ExpireItemsSingleMapIteratively),
                   MODULE(RteEtherAddrHash),
                   MODULE(DchainRejuvenateIndex),
                   MODULE(VectorBorrow),
                   MODULE(VectorReturn),
                   MODULE(DchainAllocateNewIndex),
                   MODULE(DchainFreeIndex),
                   MODULE(MapPut),
                   MODULE(PacketGetUnreadLength),
                   MODULE(SetIpv4UdpTcpChecksum),
                   MODULE(DchainIsIndexAllocated),
                   MODULE(SketchComputeHashes),
                   MODULE(SketchExpire),
                   MODULE(SketchFetch),
                   MODULE(SketchRefresh),
                   MODULE(SketchTouchBuckets),
                   MODULE(MapErase),
                   MODULE(LoadBalancedFlowHash),
                   MODULE(ChtFindBackend),
                   MODULE(HashObj),
               },
               TargetMemoryBank_ptr(new tfheMemoryBank())) {}

  static Target_ptr build() { return Target_ptr(new tfheTarget()); }
};

} // namespace tfhe
} // namespace targets
} // namespace synapse
