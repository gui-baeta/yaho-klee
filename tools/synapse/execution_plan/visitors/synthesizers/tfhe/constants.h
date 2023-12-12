#pragma once

namespace synapse {
namespace synthesizer {
namespace tfhe {

constexpr char BOILERPLATE_FILE[] = "boilerplate.c";

constexpr char MARKER_GLOBAL_STATE[] = "GLOBAL STATE";
constexpr char MARKER_NF_INIT[] = "NF INIT";
constexpr char MARKER_NF_PROCESS[] = "NF PROCESS";

constexpr char DEVICE_VAR_LABEL[] = "device";
constexpr char PACKET_VAR_LABEL[] = "buffer";
constexpr char PACKET_LEN_VAR_LABEL[] = "packet_length";
constexpr char TIME_VAR_LABEL[] = "now";
constexpr char MBUF_VAR_LABEL[] = "mbuf";

constexpr char FLOOD_DEVICE[] = "FLOOD_FRAME";

constexpr char HEADER_BASE_LABEL[] = "hdr";
constexpr char MAP_BASE_LABEL[] = "map";
constexpr char VECTOR_BASE_LABEL[] = "vector";
constexpr char DCHAIN_BASE_LABEL[] = "dchain";
constexpr char SKETCH_BASE_LABEL[] = "sketch";
constexpr char NUM_FREED_FLOWS_BASE_LABEL[] = "num_freed_flows";
constexpr char CONTAINS_BASE_LABEL[] = "contains";
constexpr char INDEX_OUT_BASE_LABEL[] = "index_out";
constexpr char OUT_OF_SPACE_BASE_LABEL[] = "out_of_space";
constexpr char IS_INDEX_ALLOCATED_BASE_LABEL[] = "is_index_allocated";
constexpr char VALUE_OUT_BASE_LABEL[] = "value_out";
constexpr char INDEX_BASE_LABEL[] = "index";
constexpr char CHECKSUM_BASE_LABEL[] = "checksum";
constexpr char OVERFLOW_BASE_LABEL[] = "overflow";
constexpr char TRASH_BASE_LABEL[] = "trash";
constexpr char OBJ_BASE_LABEL[] = "obj";
constexpr char LOADBALANCED_FLOW_HASH_BASE_LABEL[] = "lb_hash";
constexpr char PREFERED_BACKEND_BASE_LABEL[] = "prefered_backend";
constexpr char CHOSEN_BACKEND_BASE_LABEL[] = "chosen_backend";
constexpr char HASH_BASE_LABEL[] = "hash";

constexpr char KEY_EQ_MACRO[] = "KEY_EQ";
constexpr char KEY_HASH_MACRO[] = "KEY_HASH";
constexpr char ELEM_INIT_MACRO[] = "INIT_ELEM";
constexpr char KEY_EQ_FN_NAME_SUFFIX[] = "_key_eq";
constexpr char KEY_HASH_FN_NAME_SUFFIX[] = "_key_hash";
constexpr char ELEM_INIT_FN_NAME_SUFFIX[] = "_init_elem";

constexpr char FN_SET_IPV4_TCPUDP_CHECKSUM[] = "set_rte_ipv4_udptcp_checksum";

} // namespace tfhe
} // namespace synthesizer
} // namespace synapse