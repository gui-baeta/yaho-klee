#pragma once

#include "call-paths-to-bdd.h"

#include "../../target.h"
#include "../visitor.h"

#include <vector>

#define DECLARE_VISIT(M)                                                       \
  void visit(const ExecutionPlanNode *ep_node, const M *node) override;

namespace synapse {

class SearchSpace;

class Graphviz : public ExecutionPlanVisitor {
private:
  std::ofstream ofs;
  std::string fpath;

  std::vector<std::string> bdd_fpaths;

  std::map<TargetType, std::string> node_colors;

  constexpr static int fname_len = 15;
  constexpr static const char *prefix = "/tmp/";
  constexpr static const char *alphanum = "0123456789"
                                          "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                          "abcdefghijklmnopqrstuvwxyz";

  std::string get_rand_fname() const;
  void open();

public:
  Graphviz(const std::string &path) : fpath(path) {
    node_colors = std::map<TargetType, std::string>{
        {TargetType::BMv2, "darkolivegreen2"},
        {TargetType::Tofino, "cornflowerblue"},
        {TargetType::x86_BMv2, "darkorange2"},
        {TargetType::x86_Tofino, "firebrick2"},
        {TargetType::x86, "cadetblue1"},
    };

    ofs.open(fpath);
    assert(ofs);
  }

  Graphviz() : Graphviz(get_rand_fname()) {}

private:
  void function_call(const ExecutionPlanNode *ep_node, BDD::Node_ptr node,
                     TargetType target, const std::string &label);
  void branch(const ExecutionPlanNode *ep_node, BDD::Node_ptr node,
              TargetType target, const std::string &label);

  struct rgb_t {
    int r;
    int g;
    int b;
  };

  rgb_t get_color(float f) const;

  void dump_bdd(const BDD::BDD &bdd,
                const std::unordered_set<uint64_t> &processed,
                const BDD::Node *next);

  std::string get_bdd_node_name(const BDD::Node *node) const;

  void dump_search_space() const;

public:
  static void visualize(const ExecutionPlan &ep, bool interrupt = true);
  static void visualize(const SearchSpace &search_space, bool interrupt = true);

  ~Graphviz() { ofs.close(); }

  void visit(ExecutionPlan ep) override;
  void visit(const ExecutionPlanNode *ep_node) override;
  void visit(const SearchSpace &search_space);

  void log(const ExecutionPlanNode *ep_node) const override;

  /********************************************
   *
   *                x86 BMv2
   *
   ********************************************/

  DECLARE_VISIT(targets::x86_bmv2::MapGet)
  DECLARE_VISIT(targets::x86_bmv2::CurrentTime)
  DECLARE_VISIT(targets::x86_bmv2::PacketBorrowNextChunk)
  DECLARE_VISIT(targets::x86_bmv2::PacketGetMetadata)
  DECLARE_VISIT(targets::x86_bmv2::PacketReturnChunk)
  DECLARE_VISIT(targets::x86_bmv2::If)
  DECLARE_VISIT(targets::x86_bmv2::Then)
  DECLARE_VISIT(targets::x86_bmv2::Else)
  DECLARE_VISIT(targets::x86_bmv2::Forward)
  DECLARE_VISIT(targets::x86_bmv2::Broadcast)
  DECLARE_VISIT(targets::x86_bmv2::Drop)
  DECLARE_VISIT(targets::x86_bmv2::ExpireItemsSingleMap)
  DECLARE_VISIT(targets::x86_bmv2::RteEtherAddrHash)
  DECLARE_VISIT(targets::x86_bmv2::DchainRejuvenateIndex)
  DECLARE_VISIT(targets::x86_bmv2::VectorBorrow)
  DECLARE_VISIT(targets::x86_bmv2::VectorReturn)
  DECLARE_VISIT(targets::x86_bmv2::DchainAllocateNewIndex)
  DECLARE_VISIT(targets::x86_bmv2::MapPut)
  DECLARE_VISIT(targets::x86_bmv2::PacketGetUnreadLength)
  DECLARE_VISIT(targets::x86_bmv2::SetIpv4UdpTcpChecksum)
  DECLARE_VISIT(targets::x86_bmv2::DchainIsIndexAllocated)

  /********************************************
   *
   *                   BMv2
   *
   ********************************************/

  DECLARE_VISIT(targets::bmv2::SendToController)
  DECLARE_VISIT(targets::bmv2::Ignore)
  DECLARE_VISIT(targets::bmv2::SetupExpirationNotifications)
  DECLARE_VISIT(targets::bmv2::If)
  DECLARE_VISIT(targets::bmv2::Then)
  DECLARE_VISIT(targets::bmv2::Else)
  DECLARE_VISIT(targets::bmv2::EthernetConsume)
  DECLARE_VISIT(targets::bmv2::EthernetModify)
  DECLARE_VISIT(targets::bmv2::TableLookup)
  DECLARE_VISIT(targets::bmv2::IPv4Consume)
  DECLARE_VISIT(targets::bmv2::IPv4Modify)
  DECLARE_VISIT(targets::bmv2::TcpUdpConsume)
  DECLARE_VISIT(targets::bmv2::TcpUdpModify)
  DECLARE_VISIT(targets::bmv2::IPOptionsConsume)
  DECLARE_VISIT(targets::bmv2::IPOptionsModify)
  DECLARE_VISIT(targets::bmv2::Drop)
  DECLARE_VISIT(targets::bmv2::Forward)
  DECLARE_VISIT(targets::bmv2::VectorReturn)

  /********************************************
   *
   *                  Tofino
   *
   ********************************************/

  DECLARE_VISIT(targets::tofino::Ignore)
  DECLARE_VISIT(targets::tofino::If)
  DECLARE_VISIT(targets::tofino::Then)
  DECLARE_VISIT(targets::tofino::Else)
  DECLARE_VISIT(targets::tofino::Forward)
  DECLARE_VISIT(targets::tofino::ParseCustomHeader)
  DECLARE_VISIT(targets::tofino::ModifyCustomHeader)
  DECLARE_VISIT(targets::tofino::ParserCondition)
  DECLARE_VISIT(targets::tofino::IPv4TCPUDPChecksumsUpdate)
  DECLARE_VISIT(targets::tofino::TableModule)
  DECLARE_VISIT(targets::tofino::TableLookup)
  DECLARE_VISIT(targets::tofino::TableRejuvenation)
  DECLARE_VISIT(targets::tofino::TableIsAllocated)
  DECLARE_VISIT(targets::tofino::IntegerAllocatorAllocate)
  DECLARE_VISIT(targets::tofino::IntegerAllocatorRejuvenate)
  DECLARE_VISIT(targets::tofino::IntegerAllocatorQuery)
  DECLARE_VISIT(targets::tofino::Drop)
  DECLARE_VISIT(targets::tofino::SendToController)
  DECLARE_VISIT(targets::tofino::SetupExpirationNotifications)
  DECLARE_VISIT(targets::tofino::CounterRead)
  DECLARE_VISIT(targets::tofino::CounterIncrement)
  DECLARE_VISIT(targets::tofino::HashObj)

  /********************************************
   *
   *                x86 Tofino
   *
   ********************************************/

  DECLARE_VISIT(targets::x86_tofino::Ignore)
  DECLARE_VISIT(targets::x86_tofino::PacketParseCPU)
  DECLARE_VISIT(targets::x86_tofino::PacketParseEthernet)
  DECLARE_VISIT(targets::x86_tofino::PacketModifyEthernet)
  DECLARE_VISIT(targets::x86_tofino::ForwardThroughTofino)
  DECLARE_VISIT(targets::x86_tofino::PacketParseIPv4)
  DECLARE_VISIT(targets::x86_tofino::PacketModifyIPv4)
  DECLARE_VISIT(targets::x86_tofino::PacketParseIPv4Options)
  DECLARE_VISIT(targets::x86_tofino::PacketModifyIPv4Options)
  DECLARE_VISIT(targets::x86_tofino::PacketParseTCPUDP)
  DECLARE_VISIT(targets::x86_tofino::PacketModifyTCPUDP)
  DECLARE_VISIT(targets::x86_tofino::PacketModifyChecksums)
  DECLARE_VISIT(targets::x86_tofino::If)
  DECLARE_VISIT(targets::x86_tofino::Then)
  DECLARE_VISIT(targets::x86_tofino::Else)
  DECLARE_VISIT(targets::x86_tofino::Drop)
  DECLARE_VISIT(targets::x86_tofino::MapGet)
  DECLARE_VISIT(targets::x86_tofino::MapPut)
  DECLARE_VISIT(targets::x86_tofino::MapErase)
  DECLARE_VISIT(targets::x86_tofino::EtherAddrHash)
  DECLARE_VISIT(targets::x86_tofino::DchainAllocateNewIndex)
  DECLARE_VISIT(targets::x86_tofino::DchainIsIndexAllocated)
  DECLARE_VISIT(targets::x86_tofino::DchainRejuvenateIndex)
  DECLARE_VISIT(targets::x86_tofino::DchainFreeIndex)
  DECLARE_VISIT(targets::x86_tofino::PacketParseTCP)
  DECLARE_VISIT(targets::x86_tofino::PacketModifyTCP)
  DECLARE_VISIT(targets::x86_tofino::PacketParseUDP)
  DECLARE_VISIT(targets::x86_tofino::PacketModifyUDP)
  DECLARE_VISIT(targets::x86_tofino::HashObj)

  /********************************************
   *
   *                  x86
   *
   ********************************************/

  DECLARE_VISIT(targets::x86::MapGet)
  DECLARE_VISIT(targets::x86::CurrentTime)
  DECLARE_VISIT(targets::x86::PacketBorrowNextChunk)
  DECLARE_VISIT(targets::x86::PacketReturnChunk)
  DECLARE_VISIT(targets::x86::If)
  DECLARE_VISIT(targets::x86::Then)
  DECLARE_VISIT(targets::x86::Else)
  DECLARE_VISIT(targets::x86::Forward)
  DECLARE_VISIT(targets::x86::Broadcast)
  DECLARE_VISIT(targets::x86::Drop)
  DECLARE_VISIT(targets::x86::ExpireItemsSingleMap)
  DECLARE_VISIT(targets::x86::ExpireItemsSingleMapIteratively)
  DECLARE_VISIT(targets::x86::RteEtherAddrHash)
  DECLARE_VISIT(targets::x86::DchainRejuvenateIndex)
  DECLARE_VISIT(targets::x86::VectorBorrow)
  DECLARE_VISIT(targets::x86::VectorReturn)
  DECLARE_VISIT(targets::x86::DchainAllocateNewIndex)
  DECLARE_VISIT(targets::x86::MapPut)
  DECLARE_VISIT(targets::x86::PacketGetUnreadLength)
  DECLARE_VISIT(targets::x86::SetIpv4UdpTcpChecksum)
  DECLARE_VISIT(targets::x86::DchainIsIndexAllocated)
  DECLARE_VISIT(targets::x86::SketchComputeHashes)
  DECLARE_VISIT(targets::x86::SketchExpire)
  DECLARE_VISIT(targets::x86::SketchFetch)
  DECLARE_VISIT(targets::x86::SketchRefresh)
  DECLARE_VISIT(targets::x86::SketchTouchBuckets)
  DECLARE_VISIT(targets::x86::MapErase)
  DECLARE_VISIT(targets::x86::DchainFreeIndex)
  DECLARE_VISIT(targets::x86::LoadBalancedFlowHash)
  DECLARE_VISIT(targets::x86::ChtFindBackend)
  DECLARE_VISIT(targets::x86::HashObj)
};
} // namespace synapse
