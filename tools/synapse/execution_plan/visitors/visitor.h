#pragma once

#include <assert.h>
#include <memory>

#define VISIT(MODULE)                                                          \
  virtual void visit(const ExecutionPlanNode *ep_node, const MODULE *m) {      \
    assert(false && "Unexpected module.");                                     \
  }

namespace synapse {

class ExecutionPlan;
class ExecutionPlanNode;

namespace targets {

namespace x86_bmv2 {
class MapGet;
class CurrentTime;
class PacketBorrowNextChunk;
class PacketGetMetadata;
class PacketReturnChunk;
class If;
class Then;
class Else;
class Forward;
class Broadcast;
class Drop;
class ExpireItemsSingleMap;
class RteEtherAddrHash;
class DchainRejuvenateIndex;
class VectorBorrow;
class VectorReturn;
class DchainAllocateNewIndex;
class MapPut;
class PacketGetUnreadLength;
class SetIpv4UdpTcpChecksum;
class DchainIsIndexAllocated;
} // namespace x86_bmv2

namespace bmv2 {
class SendToController;
class ParserConsume;
class Ignore;
class SetupExpirationNotifications;
class If;
class Then;
class Else;
class EthernetConsume;
class EthernetModify;
class TableLookup;
class IPv4Consume;
class IPv4Modify;
class TcpUdpConsume;
class TcpUdpModify;
class IPOptionsConsume;
class IPOptionsModify;
class Drop;
class Forward;
class VectorReturn;
} // namespace bmv2

namespace tofino {
class Ignore;
class If;
class Then;
class Else;
class Forward;
class ParseCustomHeader;
class ModifyCustomHeader;
class ParserCondition;
class IPv4TCPUDPChecksumsUpdate;
class Drop;
class SendToController;
class SetupExpirationNotifications;
class TableModule;
class TableLookup;
class TableRejuvenation;
class TableIsAllocated;
class IntegerAllocatorAllocate;
class IntegerAllocatorRejuvenate;
class IntegerAllocatorQuery;
class CounterRead;
class CounterIncrement;
class HashObj;
} // namespace tofino

namespace x86_tofino {
class Ignore;
class CurrentTime;
class PacketParseCPU;
class PacketParseEthernet;
class PacketModifyEthernet;
class PacketParseIPv4;
class PacketModifyIPv4;
class PacketParseIPv4Options;
class PacketModifyIPv4Options;
class PacketParseTCPUDP;
class PacketModifyTCPUDP;
class PacketParseTCP;
class PacketModifyTCP;
class PacketParseUDP;
class PacketModifyUDP;
class PacketModifyChecksums;
class ForwardThroughTofino;
class If;
class Then;
class Else;
class Drop;
class MapGet;
class MapPut;
class MapErase;
class EtherAddrHash;
class DchainAllocateNewIndex;
class DchainIsIndexAllocated;
class DchainRejuvenateIndex;
class DchainFreeIndex;
class HashObj;
} // namespace x86_tofino

namespace x86 {
class MapGet;
class CurrentTime;
class PacketBorrowNextChunk;
class PacketReturnChunk;
class If;
class Then;
class Else;
class Forward;
class Broadcast;
class Drop;
class ExpireItemsSingleMap;
class ExpireItemsSingleMapIteratively;
class RteEtherAddrHash;
class DchainRejuvenateIndex;
class VectorBorrow;
class VectorReturn;
class DchainAllocateNewIndex;
class MapPut;
class PacketGetUnreadLength;
class SetIpv4UdpTcpChecksum;
class DchainIsIndexAllocated;
class SketchComputeHashes;
class SketchExpire;
class SketchFetch;
class SketchRefresh;
class SketchTouchBuckets;
class MapErase;
class DchainFreeIndex;
class LoadBalancedFlowHash;
class ChtFindBackend;
class HashObj;
} // namespace x86

/* TODO GUI add modules here */
namespace tfhe {
class MapGet;
class CurrentTime;
class PacketBorrowNextChunk;
class PacketBorrowNextSecret;
class PacketReturnChunk;
class TruthTablePBS;
class Conditional;
class MonoPBS;
class TernarySum;
class If;
class Then;
class Else;
class Forward;
class Broadcast;
class Drop;
class ExpireItemsSingleMap;
class ExpireItemsSingleMapIteratively;
class RteEtherAddrHash;
class DchainRejuvenateIndex;
class VectorBorrow;
class VectorReturn;
class DchainAllocateNewIndex;
class MapPut;
class PacketGetUnreadLength;
class SetIpv4UdpTcpChecksum;
class DchainIsIndexAllocated;
class SketchComputeHashes;
class SketchExpire;
class SketchFetch;
class SketchRefresh;
class SketchTouchBuckets;
class MapErase;
class DchainFreeIndex;
class LoadBalancedFlowHash;
class ChtFindBackend;
class HashObj;
} // namespace tfhe

} // namespace targets

class ExecutionPlanVisitor {
public:
  virtual void visit(ExecutionPlan ep);
  virtual void visit(const ExecutionPlanNode *ep_node);

  /*************************************
   *
   *              x86 BMv2
   *
   * **********************************/

  VISIT(targets::x86_bmv2::MapGet)
  VISIT(targets::x86_bmv2::CurrentTime)
  VISIT(targets::x86_bmv2::PacketBorrowNextChunk)
  VISIT(targets::x86_bmv2::PacketReturnChunk)
  VISIT(targets::x86_bmv2::PacketGetMetadata)
  VISIT(targets::x86_bmv2::If)
  VISIT(targets::x86_bmv2::Then)
  VISIT(targets::x86_bmv2::Else)
  VISIT(targets::x86_bmv2::Forward)
  VISIT(targets::x86_bmv2::Broadcast)
  VISIT(targets::x86_bmv2::Drop)
  VISIT(targets::x86_bmv2::ExpireItemsSingleMap)
  VISIT(targets::x86_bmv2::RteEtherAddrHash)
  VISIT(targets::x86_bmv2::DchainRejuvenateIndex)
  VISIT(targets::x86_bmv2::VectorBorrow)
  VISIT(targets::x86_bmv2::VectorReturn)
  VISIT(targets::x86_bmv2::DchainAllocateNewIndex)
  VISIT(targets::x86_bmv2::MapPut)
  VISIT(targets::x86_bmv2::PacketGetUnreadLength)
  VISIT(targets::x86_bmv2::SetIpv4UdpTcpChecksum)
  VISIT(targets::x86_bmv2::DchainIsIndexAllocated)

  /*************************************
   *
   *                 BMv2
   *
   * **********************************/

  VISIT(targets::bmv2::SendToController)
  VISIT(targets::bmv2::Ignore)
  VISIT(targets::bmv2::SetupExpirationNotifications)
  VISIT(targets::bmv2::If)
  VISIT(targets::bmv2::Then)
  VISIT(targets::bmv2::Else)
  VISIT(targets::bmv2::EthernetConsume)
  VISIT(targets::bmv2::EthernetModify)
  VISIT(targets::bmv2::TableLookup)
  VISIT(targets::bmv2::IPv4Consume)
  VISIT(targets::bmv2::IPv4Modify)
  VISIT(targets::bmv2::TcpUdpConsume)
  VISIT(targets::bmv2::TcpUdpModify)
  VISIT(targets::bmv2::IPOptionsConsume)
  VISIT(targets::bmv2::IPOptionsModify)
  VISIT(targets::bmv2::Drop)
  VISIT(targets::bmv2::Forward)
  VISIT(targets::bmv2::VectorReturn)

  /*************************************
   *
   *              Tofino
   *
   * **********************************/

  VISIT(targets::tofino::Ignore)
  VISIT(targets::tofino::If)
  VISIT(targets::tofino::Then)
  VISIT(targets::tofino::Else)
  VISIT(targets::tofino::Forward)
  VISIT(targets::tofino::ParseCustomHeader)
  VISIT(targets::tofino::ModifyCustomHeader)
  VISIT(targets::tofino::ParserCondition)
  VISIT(targets::tofino::Drop)
  VISIT(targets::tofino::SendToController)
  VISIT(targets::tofino::SetupExpirationNotifications)
  VISIT(targets::tofino::IPv4TCPUDPChecksumsUpdate)
  VISIT(targets::tofino::TableModule)
  VISIT(targets::tofino::TableLookup)
  VISIT(targets::tofino::TableRejuvenation)
  VISIT(targets::tofino::TableIsAllocated)
  VISIT(targets::tofino::IntegerAllocatorAllocate)
  VISIT(targets::tofino::IntegerAllocatorRejuvenate)
  VISIT(targets::tofino::IntegerAllocatorQuery)
  VISIT(targets::tofino::CounterRead)
  VISIT(targets::tofino::CounterIncrement)
  VISIT(targets::tofino::HashObj)

  /*************************************
   *
   *              x86 Tofino
   *
   * **********************************/

  VISIT(targets::x86_tofino::Ignore)
  VISIT(targets::x86_tofino::CurrentTime)
  VISIT(targets::x86_tofino::PacketParseCPU)
  VISIT(targets::x86_tofino::PacketParseEthernet)
  VISIT(targets::x86_tofino::PacketModifyEthernet)
  VISIT(targets::x86_tofino::PacketParseIPv4)
  VISIT(targets::x86_tofino::PacketModifyIPv4)
  VISIT(targets::x86_tofino::PacketParseIPv4Options)
  VISIT(targets::x86_tofino::PacketModifyIPv4Options)
  VISIT(targets::x86_tofino::PacketParseTCPUDP)
  VISIT(targets::x86_tofino::PacketModifyTCPUDP)
  VISIT(targets::x86_tofino::PacketParseTCP)
  VISIT(targets::x86_tofino::PacketModifyTCP)
  VISIT(targets::x86_tofino::PacketParseUDP)
  VISIT(targets::x86_tofino::PacketModifyUDP)
  VISIT(targets::x86_tofino::PacketModifyChecksums)
  VISIT(targets::x86_tofino::ForwardThroughTofino)
  VISIT(targets::x86_tofino::If)
  VISIT(targets::x86_tofino::Then)
  VISIT(targets::x86_tofino::Else)
  VISIT(targets::x86_tofino::Drop)
  VISIT(targets::x86_tofino::MapGet)
  VISIT(targets::x86_tofino::MapPut)
  VISIT(targets::x86_tofino::EtherAddrHash)
  VISIT(targets::x86_tofino::DchainAllocateNewIndex)
  VISIT(targets::x86_tofino::DchainIsIndexAllocated)
  VISIT(targets::x86_tofino::DchainRejuvenateIndex)
  VISIT(targets::x86_tofino::DchainFreeIndex)
  VISIT(targets::x86_tofino::HashObj)
  VISIT(targets::x86_tofino::MapErase)

  /*************************************
   *
   *                x86
   *
   * **********************************/

  VISIT(targets::x86::MapGet)
  VISIT(targets::x86::CurrentTime)
  VISIT(targets::x86::PacketBorrowNextChunk)
  VISIT(targets::x86::PacketReturnChunk)
  VISIT(targets::x86::If)
  VISIT(targets::x86::Then)
  VISIT(targets::x86::Else)
  VISIT(targets::x86::Forward)
  VISIT(targets::x86::Broadcast)
  VISIT(targets::x86::Drop)
  VISIT(targets::x86::ExpireItemsSingleMap)
  VISIT(targets::x86::ExpireItemsSingleMapIteratively)
  VISIT(targets::x86::RteEtherAddrHash)
  VISIT(targets::x86::DchainRejuvenateIndex)
  VISIT(targets::x86::VectorBorrow)
  VISIT(targets::x86::VectorReturn)
  VISIT(targets::x86::DchainAllocateNewIndex)
  VISIT(targets::x86::MapPut)
  VISIT(targets::x86::PacketGetUnreadLength)
  VISIT(targets::x86::SetIpv4UdpTcpChecksum)
  VISIT(targets::x86::DchainIsIndexAllocated)
  VISIT(targets::x86::SketchComputeHashes)
  VISIT(targets::x86::SketchExpire)
  VISIT(targets::x86::SketchFetch)
  VISIT(targets::x86::SketchRefresh)
  VISIT(targets::x86::SketchTouchBuckets)
  VISIT(targets::x86::MapErase)
  VISIT(targets::x86::DchainFreeIndex)
  VISIT(targets::x86::LoadBalancedFlowHash)
  VISIT(targets::x86::ChtFindBackend)
  VISIT(targets::x86::HashObj)

  /*************************************
   *
   *                tfhe
   *
   * **********************************/
  /* TODO GUI add modules here */

  VISIT(targets::tfhe::MapGet)
  VISIT(targets::tfhe::CurrentTime)
  VISIT(targets::tfhe::PacketBorrowNextChunk)
  VISIT(targets::tfhe::PacketReturnChunk)
  VISIT(targets::tfhe::PacketBorrowNextSecret)
  VISIT(targets::tfhe::TruthTablePBS)
  VISIT(targets::tfhe::Conditional)
  VISIT(targets::tfhe::MonoPBS)
  VISIT(targets::tfhe::TernarySum)
  VISIT(targets::tfhe::If)
  VISIT(targets::tfhe::Then)
  VISIT(targets::tfhe::Else)
  VISIT(targets::tfhe::Forward)
  VISIT(targets::tfhe::Broadcast)
  VISIT(targets::tfhe::Drop)
  VISIT(targets::tfhe::ExpireItemsSingleMap)
  VISIT(targets::tfhe::ExpireItemsSingleMapIteratively)
  VISIT(targets::tfhe::RteEtherAddrHash)
  VISIT(targets::tfhe::DchainRejuvenateIndex)
  VISIT(targets::tfhe::VectorBorrow)
  VISIT(targets::tfhe::VectorReturn)
  VISIT(targets::tfhe::DchainAllocateNewIndex)
  VISIT(targets::tfhe::MapPut)
  VISIT(targets::tfhe::PacketGetUnreadLength)
  VISIT(targets::tfhe::SetIpv4UdpTcpChecksum)
  VISIT(targets::tfhe::DchainIsIndexAllocated)
  VISIT(targets::tfhe::SketchComputeHashes)
  VISIT(targets::tfhe::SketchExpire)
  VISIT(targets::tfhe::SketchFetch)
  VISIT(targets::tfhe::SketchRefresh)
  VISIT(targets::tfhe::SketchTouchBuckets)
  VISIT(targets::tfhe::MapErase)
  VISIT(targets::tfhe::DchainFreeIndex)
  VISIT(targets::tfhe::LoadBalancedFlowHash)
  VISIT(targets::tfhe::ChtFindBackend)
  VISIT(targets::tfhe::HashObj)

protected:
  virtual void log(const ExecutionPlanNode *ep_node) const;
};

} // namespace synapse
