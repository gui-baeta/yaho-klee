#pragma once

#include "call-paths-to-bdd.h"
#include "klee-util.h"

namespace BDD {
namespace symbex {

constexpr char CHUNK[] = "packet_chunks";
constexpr char PORT2[] = "device";
constexpr char CPU_CODE_PATH[] = "cpu_code_path";

constexpr char TIME[] = "next_time";
constexpr char RECEIVED_PACKET[] = "received_a_packet";
constexpr char BUFFER_LENGTH[] = "data_len";
constexpr char PACKET_LENGTH[] = "pkt_len";
constexpr char PORT[] = "VIGOR_DEVICE";

constexpr klee::Expr::Width PORT_SYMBOL_SIZE = 32;
constexpr klee::Expr::Width PACKET_LENGTH_SYMBOL_SIZE = 32;

constexpr char MAP_TYPE[] = "struct Map";
constexpr char VECTOR_TYPE[] = "struct Vector";
constexpr char DCHAIN_TYPE[] = "struct DoubleChain";
constexpr char SKETCH_TYPE[] = "struct Sketch";

constexpr char FN_RTE_SET_CHECKSUM[] = "rte_ipv4_udptcp_cksum";
constexpr char RTE_IPV4_TYPE[] = "struct rte_ipv4_hdr";
constexpr char RTE_L4_TYPE[] = "void";

constexpr char FN_CURRENT_TIME[] = "current_time";
constexpr char FN_ETHER_HASH[] = "rte_ether_addr_hash";
constexpr char FN_ETHER_HASH_ARG_OBJ[] = "obj";
constexpr char FN_EXPIRE_MAP[] = "expire_items_single_map";
constexpr char EXPIRE_MAP_FREED_FLOWS[] = "number_of_freed_flows";

constexpr char FN_EXPIRE_MAP_ITERATIVELY[] =
    "expire_items_single_map_iteratively";
constexpr char EXPIRE_MAP_ITERATIVELY_MAP[] = "map";
constexpr char EXPIRE_MAP_ITERATIVELY_VECTOR[] = "vector";
constexpr char EXPIRE_MAP_ITERATIVELY_START[] = "start";
constexpr char EXPIRE_MAP_ITERATIVELY_N_ELEMS[] = "n_elems";

constexpr char FN_SET_CHECKSUM[] = "nf_set_rte_ipv4_udptcp_checksum";
constexpr char FN_SET_CHECKSUM_ARG_IP[] = "ip_header";
constexpr char FN_SET_CHECKSUM_ARG_L4[] = "l4_header";
constexpr char FN_SET_CHECKSUM_ARG_PACKET[] = "packet";
constexpr char CHECKSUM[] = "checksum";

constexpr char FN_BORROW_SECRET[] = "packet_borrow_next_secret";
constexpr char FN_BORROW_CHUNK[] = "packet_borrow_next_chunk";
constexpr char FN_RETURN_CHUNK[] = "packet_return_chunk";
constexpr char FN_GET_UNREAD_LEN[] = "packet_get_unread_length";

constexpr char FN_BORROW_ARG_PACKET[] = "p";
constexpr char FN_BORROW_ARG_CHUNK[] = "chunk";

constexpr char FN_DCHAIN_ALLOCATE[] = "dchain_allocate";
constexpr char FN_DCHAIN_REJUVENATE[] = "dchain_rejuvenate_index";
constexpr char FN_DCHAIN_ALLOCATE_NEW_INDEX[] = "dchain_allocate_new_index";
constexpr char FN_DCHAIN_IS_ALLOCATED[] = "dchain_is_index_allocated";
constexpr char FN_DCHAIN_FREE_INDEX[] = "dchain_free_index";
constexpr char FN_DCHAIN_ALLOCATE_ARG_CHAIN_OUT[] = "chain_out";
constexpr char FN_DCHAIN_ALLOCATE_ARG_INDEX_RANGE[] = "index_range";
constexpr char FN_DCHAIN_ARG_CHAIN[] = "chain";
constexpr char FN_DCHAIN_ARG_TIME[] = "time";
constexpr char FN_DCHAIN_ARG_INDEX[] = "index";
constexpr char FN_DCHAIN_ARG_OUT[] = "index_out";
constexpr char DCHAIN_OUT_OF_SPACE[] = "out_of_space";
constexpr char DCHAIN_NEW_INDEX[] = "new_index";
constexpr char DCHAIN_IS_INDEX_ALLOCATED[] = "dchain_is_index_allocated";

constexpr char FN_MAP_ALLOCATE[] = "map_allocate";
constexpr char FN_MAP_GET[] = "map_get";
constexpr char FN_MAP_PUT[] = "map_put";
constexpr char FN_MAP_ERASE[] = "map_erase";
constexpr char FN_MAP_ARG_CAPACITY[] = "capacity";
constexpr char FN_MAP_ARG_KEY_EQUAL[] = "keq";
constexpr char FN_MAP_ARG_KEY_HASH[] = "khash";
constexpr char FN_MAP_ARG_MAP_OUT[] = "map_out";
constexpr char FN_MAP_ARG_MAP[] = "map";
constexpr char FN_MAP_ARG_KEY[] = "key";
constexpr char FN_MAP_ARG_VALUE[] = "value";
constexpr char FN_MAP_ARG_OUT[] = "value_out";
constexpr char FN_MAP_ARG_TRASH[] = "trash";
constexpr char MAP_HAS_THIS_KEY[] = "map_has_this_key";
constexpr char MAP_ALLOCATED_INDEX[] = "allocated_index";

constexpr char FN_VECTOR_ALLOCATE[] = "vector_allocate";
constexpr char FN_VECTOR_BORROW[] = "vector_borrow";
constexpr char FN_VECTOR_RETURN[] = "vector_return";
constexpr char FN_VECTOR_ARG_CAPACITY[] = "capacity";
constexpr char FN_VECTOR_ARG_ELEM_SIZE[] = "elem_size";
constexpr char FN_VECTOR_ARG_INIT_ELEM[] = "init_elem";
constexpr char FN_VECTOR_ARG_VECTOR_OUT[] = "vector_out";
constexpr char FN_VECTOR_ARG_VECTOR[] = "vector";
constexpr char FN_VECTOR_ARG_INDEX[] = "index";
constexpr char FN_VECTOR_ARG_VALUE[] = "value";
constexpr char FN_VECTOR_ARG_OUT[] = "val_out";
constexpr char FN_VECTOR_EXTRA[] = "borrowed_cell";
constexpr char VECTOR_VALUE_SYMBOL[] = "vector_data_reset";

constexpr char FN_CHT_FILL[] = "cht_fill_cht";
constexpr char FN_CHT_FIND_BACKEND[] = "cht_find_preferred_available_backend";
constexpr char FN_CHT_ARG_CAPACITY[] = "backend_capacity";
constexpr char FN_CHT_ARG_HEIGHT[] = "cht_height";
constexpr char FN_CHT_ARG_CHT[] = "cht";
constexpr char FN_CHT_ARG_ACTIVE[] = "active_backends";
constexpr char FN_CHT_ARG_CHOSEN[] = "chosen_backend";
constexpr char FN_CHT_ARG_HASH[] = "hash";
constexpr char CHT_BACKEND_FOUND_SYMBOL[] = "prefered_backend_found";
constexpr char CHT_CHOSEN_SYMBOL[] = "chosen_backend";

constexpr char FN_SKETCH_ALLOCATE[] = "sketch_allocate";
constexpr char FN_SKETCH_COMPUTE_HASHES[] = "sketch_compute_hashes";
constexpr char FN_SKETCH_EXPIRE[] = "sketch_expire";
constexpr char FN_SKETCH_REFRESH[] = "sketch_refresh";
constexpr char FN_SKETCH_FETCH[] = "sketch_fetch";
constexpr char FN_SKETCH_TOUCH_BUCKETS[] = "sketch_touch_buckets";
constexpr char FN_SKETCH_ARG_CAPACITY[] = "capacity";
constexpr char FN_SKETCH_ARG_KEY[] = "key";
constexpr char FN_SKETCH_ARG_THRESHOLD[] = "threshold";
constexpr char FN_SKETCH_ARG_KEY_HASH[] = "khash";
constexpr char FN_SKETCH_ARG_SKETCH[] = "sketch";
constexpr char FN_SKETCH_ARG_SKETCH_OUT[] = "sketch_out";
constexpr char FN_SKETCH_ARG_TIME[] = "time";
constexpr char SKETCH_OVERFLOW_SYMBOL[] = "overflow";
constexpr char SKETCH_SUCCESS_SYMBOL[] = "success";

constexpr char FN_BORROW_CHUNK_ARG_LEN[] = "length";
constexpr char FN_BORROW_CHUNK_EXTRA[] = "the_chunk";

constexpr char FN_EXPIRE_MAP_ARG_CHAIN[] = "chain";
constexpr char FN_EXPIRE_MAP_ARG_VECTOR[] = "vector";
constexpr char FN_EXPIRE_MAP_ARG_MAP[] = "map";
constexpr char FN_EXPIRE_MAP_ARG_TIME[] = "time";

constexpr char FN_LOADBALANCEDFLOW_HASH[] = "LoadBalancedFlow_hash";
constexpr char FN_LOADBALANCEDFLOW_HASH_ARG_OBJ[] = "obj";
constexpr char LOADBALANCEDFLOW_HASH_SYMBOL[] = "LoadBalancedFlow_hash";

constexpr char FN_HASH_OBJ[] = "hash_obj";
constexpr char FN_HASH_OBJ_ARG_OBJ[] = "obj";
constexpr char FN_HASH_OBJ_ARG_SIZE[] = "size";
constexpr char HASH_OBJ_HASH_SYMBOL[] = "hash";

constexpr char KLEE_EXPR_IPV4_CONDITION[] =
    "(Eq (w32 0) (Or w32 (ZExt w32 (Eq false (Eq (w16 8) (ReadLSB w16 (w32 12) "
    "packet_chunks)))) (ZExt w32 (Ult (ZExt w64 (Extract w16 0 (Add w32 (w32 "
    "4294967282) (ZExt w32 (ReadLSB w16 (w32 0) pkt_len))))) (w64 20)))))";

constexpr char KLEE_EXPR_IPV4_OPTIONS_CONDITION_TO_IGNORE[] =
    "(Eq (w32 0) (Or w32 (ZExt w32 (Slt (ZExt w32 (Extract w8 0 (And w32 (ZExt "
    "w32 (Read w8 (w32 41) packet_chunks)) (w32 15)))) (w32 5))) (ZExt w32 "
    "(Slt (ZExt w32 (Extract w16 0 (Add w32 (w32 4294967282) (ZExt w32 "
    "(ReadLSB w16 (w32 0) pkt_len))))) (ZExt w32 (Extract w16 0 (Or w32 (Shl "
    "w32 (And w32 N0:(ZExt w32 (ReadLSB w16 (w32 43) packet_chunks)) (w32 "
    "255)) (w32 8)) (AShr w32 (And w32 N0 (w32 65280)) (w32 8)))))))))";

constexpr char KLEE_EXPR_IPV4_OPTIONS_CONDITION[] =
    "(Eq (w32 0) (And w32 (ZExt w32 (Eq false (Eq (w16 0) N0:(Extract w16 0 "
    "(Extract w32 0 (Mul w64 (w64 4) (SExt w64 (Extract w32 0 (Add w64 (w64 "
    "18446744073709551611) (SExt w64 (ZExt w32 (Extract w8 0 (And w32 (ZExt "
    "w32 (Read w8 (w32 41) packet_chunks)) (w32 15)))))))))))))) (ZExt w32 "
    "(Ule (ZExt w64 N0) (Add w64 (w64 18446744073709551596) (ZExt w64 (Extract "
    "w16 0 (Add w32 (w32 4294967282) (ZExt w32 (ReadLSB w16 (w32 0) "
    "pkt_len))))))))))";

constexpr char KLEE_EXPR_TCPUDP_AFTER_IPV4_OPTIONS_CONDITION[] =
    "(Eq (w32 0) (Or w32 (ZExt w32 (Eq (w32 0) (Or w32 (ZExt w32 (Eq (w8 6) "
    "N0:(Read w8 (w32 50) packet_chunks))) (ZExt w32 (Eq (w8 17) N0))))) (ZExt "
    "w32 (Ult (ZExt w64 (Sub w32 (ZExt w32 (ReadLSB w16 (w32 0) pkt_len)) "
    "(Extract w32 0 (Add w64 (w64 34) (ZExt w64 (Extract w16 0 (Extract w32 0 "
    "(Mul w64 (w64 4) (SExt w64 (Extract w32 0 (Add w64 (w64 "
    "18446744073709551611) (SExt w64 (ZExt w32 (Extract w8 0 (And w32 (ZExt "
    "w32 (Read w8 (w32 41) packet_chunks)) (w32 15)))))))))))))))) (w64 4)))))";

constexpr char KLEE_EXPR_TCPUDP_CONDITION[] =
    "(Eq (w32 0) (Or w32 (ZExt w32 (Eq (w32 0) (Or w32 (ZExt w32 (Eq (w8 6) "
    "N0:(Read w8 (w32 50) packet_chunks))) (ZExt w32 (Eq (w8 17) N0))))) (ZExt "
    "w32 (Ult (ZExt w64 (Add w32 (w32 4294967262) (ZExt w32 (ReadLSB w16 (w32 "
    "0) pkt_len)))) (w64 4)))))";

std::pair<bool, addr_t> get_obj_from_call(const Call *call);

struct dchain_config_t {
  uint64_t index_range;
};

struct map_config_t {
  uint64_t capacity;
  bits_t key_size;
  std::string key_eq;   // function ptr
  std::string key_hash; // function ptr
};

struct vector_config_t {
  uint64_t capacity;
  uint64_t elem_size;
  std::string init_elem; // function ptr
};

struct sketch_config_t {
  uint64_t capacity;
  uint64_t threshold;
  bits_t key_size;
  std::string key_hash; // function ptr
};

struct cht_config_t {
  uint64_t capacity;
  uint64_t height;
};

dchain_config_t get_dchain_config(const BDD &bdd, addr_t dchain_addr);
map_config_t get_map_config(const BDD &bdd, addr_t map_addr);
vector_config_t get_vector_config(const BDD &bdd, addr_t vector_addr);
sketch_config_t get_sketch_config(const BDD &bdd, addr_t sketch_addr);
cht_config_t get_cht_config(const BDD &bdd, addr_t cht_addr);

} // namespace symbex
} // namespace BDD