#pragma once

#include <sstream>
#include <vector>

#include "../../../../log.h"
#include "../../../execution_plan.h"
#include "../code_builder.h"
#include "../synthesizer.h"
#include "../util.h"

#include "constants.h"
#include "transpiler.h"

#include "domain/stack.h"
#include "domain/variable.h"

namespace synapse {
namespace synthesizer {
namespace tfhe {

namespace target = synapse::targets::tfhe;

class tfheGenerator : public Synthesizer {
private:
  CodeBuilder global_state_builder;
  CodeBuilder nf_init_builder;
  CodeBuilder nf_process_builder;

  Transpiler transpiler;

  stack_t vars;
  PendingIfs pending_ifs;

public:
  tfheGenerator()
      : Synthesizer(GET_BOILERPLATE_PATH(BOILERPLATE_FILE)),
        global_state_builder(get_indentation_level(MARKER_GLOBAL_STATE)),
        nf_init_builder(get_indentation_level(MARKER_NF_INIT)),
        nf_process_builder(get_indentation_level(MARKER_NF_PROCESS)),
        transpiler(*this), pending_ifs(nf_process_builder) {}

  std::string transpile(klee::ref<klee::Expr> expr);
  virtual void generate(ExecutionPlan &target_ep) override { visit(target_ep); }

  variable_query_t search_variable(std::string symbol) const;
  variable_query_t search_variable(klee::ref<klee::Expr> expr) const;

  void init_state(ExecutionPlan ep);

  void visit(ExecutionPlan ep) override;
  void visit(const ExecutionPlanNode *ep_node) override;

  // TODO GUI add modules here
  void visit(const ExecutionPlanNode *ep_node,
             const target::MapGet *node) override;
  void visit(const ExecutionPlanNode *ep_node,
             const target::CurrentTime *node) override;
  void visit(const ExecutionPlanNode *ep_node,
             const target::PacketBorrowNextChunk *node) override;
  void visit(const ExecutionPlanNode *ep_node,
             const target::PacketBorrowNextSecret *node) override; // TODO GUI continue
  void visit(const ExecutionPlanNode *ep_node,
             const target::TruthTablePBS *node) override; // TODO GUI continue
  void visit(const ExecutionPlanNode *ep_node,
             const target::Conditional *node) override; // TODO GUI continue
  void visit(const ExecutionPlanNode *ep_node,
             const target::MonoPBS *node) override; // TODO GUI continue
  void visit(const ExecutionPlanNode *ep_node,
             const target::TernarySum *node) override; // TODO GUI continue
  void visit(const ExecutionPlanNode *ep_node,
             const target::PacketReturnChunk *node) override;
  void visit(const ExecutionPlanNode *ep_node,
             const target::If *node) override;
  void visit(const ExecutionPlanNode *ep_node,
             const target::Then *node) override;
  void visit(const ExecutionPlanNode *ep_node,
             const target::Else *node) override;
  void visit(const ExecutionPlanNode *ep_node,
             const target::Forward *node) override;
  void visit(const ExecutionPlanNode *ep_node,
             const target::Broadcast *node) override;
  void visit(const ExecutionPlanNode *ep_node,
             const target::Drop *node) override;
  void visit(const ExecutionPlanNode *ep_node,
             const target::ExpireItemsSingleMap *node) override;
  void visit(const ExecutionPlanNode *ep_node,
             const target::ExpireItemsSingleMapIteratively *node) override;
  void visit(const ExecutionPlanNode *ep_node,
             const target::RteEtherAddrHash *node) override;
  void visit(const ExecutionPlanNode *ep_node,
             const target::DchainRejuvenateIndex *node) override;
  void visit(const ExecutionPlanNode *ep_node,
             const target::VectorBorrow *node) override;
  void visit(const ExecutionPlanNode *ep_node,
             const target::VectorReturn *node) override;
  void visit(const ExecutionPlanNode *ep_node,
             const target::DchainAllocateNewIndex *node) override;
  void visit(const ExecutionPlanNode *ep_node,
             const target::MapPut *node) override;
  void visit(const ExecutionPlanNode *ep_node,
             const target::PacketGetUnreadLength *node) override;
  void visit(const ExecutionPlanNode *ep_node,
             const target::SetIpv4UdpTcpChecksum *node) override;
  void visit(const ExecutionPlanNode *ep_node,
             const target::DchainIsIndexAllocated *node) override;
  void visit(const ExecutionPlanNode *ep_node,
             const target::DchainFreeIndex *node) override;
  void visit(const ExecutionPlanNode *ep_node,
             const target::SketchComputeHashes *node) override;
  void visit(const ExecutionPlanNode *ep_node,
             const target::SketchExpire *node) override;
  void visit(const ExecutionPlanNode *ep_node,
             const target::SketchFetch *node) override;
  void visit(const ExecutionPlanNode *ep_node,
             const target::SketchRefresh *node) override;
  void visit(const ExecutionPlanNode *ep_node,
             const target::SketchTouchBuckets *node) override;
  void visit(const ExecutionPlanNode *ep_node,
             const target::MapErase *node) override;
  void visit(const ExecutionPlanNode *ep_node,
             const target::LoadBalancedFlowHash *node) override;
  void visit(const ExecutionPlanNode *ep_node,
             const target::ChtFindBackend *node) override;
  void visit(const ExecutionPlanNode *ep_node,
             const target::HashObj *node) override;

private:
  void map_init(addr_t addr, const BDD::symbex::map_config_t &cfg);
  void vector_init(addr_t addr, const BDD::symbex::vector_config_t &cfg);
  void dchain_init(addr_t addr, const BDD::symbex::dchain_config_t &cfg);
  void sketch_init(addr_t addr, const BDD::symbex::sketch_config_t &cfg);
  void cht_init(addr_t addr, const BDD::symbex::cht_config_t &cfg);
};

} // namespace tfhe
} // namespace synthesizer
} // namespace synapse