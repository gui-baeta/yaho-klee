#include "tfhe_generator.h"
#include "klee-util.h"

#include "../../../../log.h"
#include "../../../modules/modules.h"
#include "../util.h"
#include "transpiler.h"

#include <sstream>

#define ADD_NODE_COMMENT(module)                                               \
  {                                                                            \
    if (module->get_node()) {                                                  \
      nf_process_builder.indent();                                             \
      nf_process_builder.append("// node ");                                   \
      nf_process_builder.append((module)->get_node()->get_id());               \
      nf_process_builder.append_new_line();                                    \
    }                                                                          \
  }

namespace synapse {
namespace synthesizer {
void Synthesizer::init_state(ExecutionPlan ep) {
    std::cerr << "Synthesizer::init_state is not implemented. This method shouldn't be called" << std::endl;
    exit(2);
}
}
}

namespace synapse {
namespace synthesizer {
namespace tfhe {

std::string tfheGenerator::transpile(klee::ref<klee::Expr> expr) {
  return transpiler.transpile(expr);
}

variable_query_t tfheGenerator::search_variable(std::string symbol) const {
  auto var = vars.get(symbol);

  if (var.valid) {
    return var;
  }

  return variable_query_t();
}

variable_query_t
tfheGenerator::search_variable(klee::ref<klee::Expr> expr) const {
  auto var = vars.get(expr);

  if (var.valid) {
    return var;
  }

  if (kutil::is_readLSB(expr)) {
    auto symbol = kutil::get_symbol(expr);
    auto variable = search_variable(symbol.second);

    if (variable.valid) {
      return variable;
    }
  }

  return variable_query_t();
}

void tfheGenerator::map_init(addr_t addr, const BDD::symbex::map_config_t &cfg) {
  auto map_label = vars.get_new_label(MAP_BASE_LABEL);
  auto map_var = Variable(map_label, 32);
  map_var.set_addr(addr);

  vars.append(map_var);

  global_state_builder.indent();
  global_state_builder.append(BDD::symbex::MAP_TYPE);
  global_state_builder.append("* ");
  global_state_builder.append(map_label);
  global_state_builder.append(";");
  global_state_builder.append_new_line();

  auto key_eq_fn = map_label + std::string(KEY_EQ_FN_NAME_SUFFIX);
  auto key_hash_fn = map_label + std::string(KEY_HASH_FN_NAME_SUFFIX);

  assert(cfg.key_size % 8 == 0);

  global_state_builder.indent();
  global_state_builder.append(KEY_EQ_MACRO);
  global_state_builder.append("(");
  global_state_builder.append(map_label);
  global_state_builder.append(", ");
  global_state_builder.append(cfg.key_size / 8);
  global_state_builder.append(")");
  global_state_builder.append(";");
  global_state_builder.append_new_line();

  global_state_builder.indent();
  global_state_builder.append(KEY_HASH_MACRO);
  global_state_builder.append("(");
  global_state_builder.append(map_label);
  global_state_builder.append(", ");
  global_state_builder.append(cfg.key_size / 8);
  global_state_builder.append(")");
  global_state_builder.append(";");
  global_state_builder.append_new_line();

  nf_init_builder.indent();
  nf_init_builder.append("if (!");
  nf_init_builder.append(BDD::symbex::FN_MAP_ALLOCATE);
  nf_init_builder.append("(");
  nf_init_builder.append(key_eq_fn);
  nf_init_builder.append(", ");
  nf_init_builder.append(key_hash_fn);
  nf_init_builder.append(", ");
  nf_init_builder.append(cfg.capacity);
  nf_init_builder.append(", ");
  nf_init_builder.append("&");
  nf_init_builder.append(map_label);
  nf_init_builder.append(")) return false;");
  nf_init_builder.append_new_line();
}

void tfheGenerator::vector_init(addr_t addr,
                               const BDD::symbex::vector_config_t &cfg) {
  auto vector_label = vars.get_new_label(VECTOR_BASE_LABEL);
  auto vector_var = Variable(vector_label, 32);
  vector_var.set_addr(addr);

  vars.append(vector_var);

  global_state_builder.indent();
  global_state_builder.append(BDD::symbex::VECTOR_TYPE);
  global_state_builder.append("* ");
  global_state_builder.append(vector_label);
  global_state_builder.append(";");
  global_state_builder.append_new_line();

  auto elem_init_fn = vector_label + std::string(ELEM_INIT_FN_NAME_SUFFIX);

  global_state_builder.indent();
  global_state_builder.append(ELEM_INIT_MACRO);
  global_state_builder.append("(");
  global_state_builder.append(vector_label);
  global_state_builder.append(", ");
  global_state_builder.append(cfg.elem_size);
  global_state_builder.append(")");
  global_state_builder.append(";");
  global_state_builder.append_new_line();

  nf_init_builder.indent();
  nf_init_builder.append("if (!");
  nf_init_builder.append(BDD::symbex::FN_VECTOR_ALLOCATE);
  nf_init_builder.append("(");
  nf_init_builder.append(cfg.elem_size);
  nf_init_builder.append(", ");
  nf_init_builder.append(cfg.capacity);
  nf_init_builder.append(", ");
  nf_init_builder.append(elem_init_fn);
  nf_init_builder.append(", ");
  nf_init_builder.append("&");
  nf_init_builder.append(vector_label);
  nf_init_builder.append(")) return false;");
  nf_init_builder.append_new_line();
}

void tfheGenerator::dchain_init(addr_t addr,
                               const BDD::symbex::dchain_config_t &cfg) {
  auto dchain_label = vars.get_new_label(DCHAIN_BASE_LABEL);
  auto dchain_var = Variable(dchain_label, 32);
  dchain_var.set_addr(addr);

  vars.append(dchain_var);

  global_state_builder.indent();
  global_state_builder.append(BDD::symbex::DCHAIN_TYPE);
  global_state_builder.append("* ");
  global_state_builder.append(dchain_label);
  global_state_builder.append(";");
  global_state_builder.append_new_line();

  nf_init_builder.indent();
  nf_init_builder.append("if (!");
  nf_init_builder.append(BDD::symbex::FN_DCHAIN_ALLOCATE);
  nf_init_builder.append("(");
  nf_init_builder.append(cfg.index_range);
  nf_init_builder.append(", ");
  nf_init_builder.append("&");
  nf_init_builder.append(dchain_label);
  nf_init_builder.append(")) return false;");
  nf_init_builder.append_new_line();
}

void tfheGenerator::sketch_init(addr_t addr,
                               const BDD::symbex::sketch_config_t &cfg) {
  auto sketch_label = vars.get_new_label(SKETCH_BASE_LABEL);
  auto sketch_var = Variable(sketch_label, 32);
  sketch_var.set_addr(addr);

  vars.append(sketch_var);

  global_state_builder.indent();
  global_state_builder.append(BDD::symbex::SKETCH_TYPE);
  global_state_builder.append("* ");
  global_state_builder.append(sketch_label);
  global_state_builder.append(";");
  global_state_builder.append_new_line();

  auto key_hash_fn = sketch_label + std::string(KEY_HASH_FN_NAME_SUFFIX);

  assert(cfg.key_size % 8 == 0);

  global_state_builder.indent();
  global_state_builder.append(KEY_HASH_MACRO);
  global_state_builder.append("(");
  global_state_builder.append(sketch_label);
  global_state_builder.append(", ");
  global_state_builder.append(cfg.key_size / 8);
  global_state_builder.append(")");
  global_state_builder.append(";");
  global_state_builder.append_new_line();

  nf_init_builder.indent();
  nf_init_builder.append("if (!");
  nf_init_builder.append(BDD::symbex::FN_SKETCH_ALLOCATE);
  nf_init_builder.append("(");
  nf_init_builder.append(key_hash_fn);
  nf_init_builder.append(", ");
  nf_init_builder.append(cfg.capacity);
  nf_init_builder.append(", ");
  nf_init_builder.append(cfg.threshold);
  nf_init_builder.append(", ");
  nf_init_builder.append("&");
  nf_init_builder.append(sketch_label);
  nf_init_builder.append(")) return false;");
  nf_init_builder.append_new_line();
}

void tfheGenerator::cht_init(addr_t addr, const BDD::symbex::cht_config_t &cfg) {
  // Actually, the cht is just a vector, and it must be initialized before this
  // call.

  auto cht = vars.get(addr);
  assert(cht.valid);

  nf_init_builder.indent();
  nf_init_builder.append("if (!");
  nf_init_builder.append(BDD::symbex::FN_CHT_FILL);
  nf_init_builder.append("(");
  nf_init_builder.append(cht.var->get_label());
  nf_init_builder.append(", ");
  nf_init_builder.append(cfg.height);
  nf_init_builder.append(", ");
  nf_init_builder.append(cfg.capacity);
  nf_init_builder.append(")) return false;");
  nf_init_builder.append_new_line();
}

void tfheGenerator::init_state(ExecutionPlan ep) {
  auto mb = ep.get_memory_bank<target::tfheMemoryBank>(TargetType::tfhe);

  auto maps = mb->get_map_configs();
  auto vectors = mb->get_vector_configs();
  auto dchains = mb->get_dchain_configs();
  auto sketches = mb->get_sketch_configs();
  auto chts = mb->get_cht_configs();

  for (auto map : maps) {
    map_init(map.first, map.second);
  }

  for (auto vector : vectors) {
    vector_init(vector.first, vector.second);
  }

  for (auto dchain : dchains) {
    dchain_init(dchain.first, dchain.second);
  }

  for (auto sketch : sketches) {
    sketch_init(sketch.first, sketch.second);
  }

  for (auto cht : chts) {
    cht_init(cht.first, cht.second);
  }

  auto packet_len_var =
      Variable(PACKET_LEN_VAR_LABEL, 32, {BDD::symbex::PACKET_LENGTH});
  vars.append(packet_len_var);

  auto device_var = Variable(DEVICE_VAR_LABEL, 16, {BDD::symbex::PORT});
  vars.append(device_var);

  nf_init_builder.indent();
  nf_init_builder.append("return true;");
  nf_init_builder.append_new_line();
}

void tfheGenerator::visit(ExecutionPlan ep) {
  this->init_state(ep);

  ExecutionPlanVisitor::visit(ep);

  std::stringstream global_state_code;
  std::stringstream nf_init_code;
  std::stringstream nf_process_code;

  global_state_builder.dump(global_state_code);
  nf_init_builder.dump(nf_init_code);
  nf_process_builder.dump(nf_process_code);

  fill_mark(MARKER_GLOBAL_STATE, global_state_code.str());
  fill_mark(MARKER_NF_INIT, nf_init_code.str());
  fill_mark(MARKER_NF_PROCESS, nf_process_code.str());
}

void tfheGenerator::visit(const ExecutionPlanNode *ep_node) {
  auto mod = ep_node->get_module();
  auto next = ep_node->get_next();

  log(ep_node);

  ADD_NODE_COMMENT(ep_node->get_module());
  mod->visit(*this, ep_node);

  for (auto branch : next) {
    branch->visit(*this);
  }
}

void tfheGenerator::visit(const ExecutionPlanNode *ep_node,
                         const target::MapGet *node) {
  auto map_addr = node->get_map_addr();
  auto key_addr = node->get_key_addr();
  auto key = node->get_key();
  auto value_out = node->get_value_out();
  auto success = node->get_success();
  auto map_has_this_key = node->get_map_has_this_key();

  auto map = vars.get(map_addr);
  assert(map.valid);

  auto key_label_base = map.var->get_label() + "_key";
  auto key_label = vars.get_new_label(key_label_base);

  assert(key->getWidth() > 0);
  assert(key->getWidth() % 8 == 0);

  nf_process_builder.indent();
  nf_process_builder.append("uint8_t ");
  nf_process_builder.append(key_label);
  nf_process_builder.append("[");
  nf_process_builder.append(key->getWidth() / 8);
  nf_process_builder.append("];");
  nf_process_builder.append_new_line();

  for (bits_t b = 0u; b < key->getWidth(); b += 8) {
    auto byte = kutil::solver_toolbox.exprBuilder->Extract(key, b, 8);
    auto byte_transpiled = transpile(byte);

    nf_process_builder.indent();
    nf_process_builder.append(key_label);
    nf_process_builder.append("[");
    nf_process_builder.append(b / 8);
    nf_process_builder.append("]");
    nf_process_builder.append(" = ");
    nf_process_builder.append(byte_transpiled);
    nf_process_builder.append(";");
    nf_process_builder.append_new_line();
  }

  auto key_var = Variable(key_label, key);
  key_var.set_addr(key_addr);
  key_var.set_is_array();
  vars.append(key_var);

  auto value_label = vars.get_new_label(INDEX_BASE_LABEL);
  auto value_var = Variable(value_label, value_out);
  vars.append(value_var);

  auto contains_label = vars.get_new_label(CONTAINS_BASE_LABEL);
  auto contains_var = Variable(contains_label, map_has_this_key);
  vars.append(contains_var);

  nf_process_builder.indent();
  nf_process_builder.append(value_var.get_type());
  nf_process_builder.append(" ");
  nf_process_builder.append(value_var.get_label());
  nf_process_builder.append(";");
  nf_process_builder.append_new_line();

  nf_process_builder.indent();
  nf_process_builder.append("int ");
  nf_process_builder.append(contains_var.get_label());
  nf_process_builder.append(" = ");

  nf_process_builder.append(BDD::symbex::FN_MAP_GET);
  nf_process_builder.append("(");
  nf_process_builder.append(map.var->get_label());
  nf_process_builder.append(", ");
  nf_process_builder.append("(void*)");
  nf_process_builder.append(key_label);
  nf_process_builder.append(", ");
  nf_process_builder.append("&");
  nf_process_builder.append(value_var.get_label());
  nf_process_builder.append(");");
  nf_process_builder.append_new_line();
}

void tfheGenerator::visit(const ExecutionPlanNode *ep_node,
                         const target::CurrentTime *node) {
  auto time = node->get_time();
  auto time_var = Variable(TIME_VAR_LABEL, time);
  vars.append(time_var);
}

// TODO GUI add modules implementations here

void tfheGenerator::visit(const ExecutionPlanNode *ep_node,
                          const target::PacketBorrowNextSecret *node) {
  auto chunk_addr = node->get_chunk_addr();
  auto chunk = node->get_chunk();
  auto len = node->get_length();

  auto hdr_label = vars.get_new_label(HEADER_BASE_LABEL);
  auto hdr_var = ByteArray(hdr_label, len, chunk, chunk_addr);
  vars.append(hdr_var);

  auto len_transpiled = transpile(len);

  nf_process_builder.indent();
  nf_process_builder.append("void* ");
  nf_process_builder.append(hdr_var.get_label());
  nf_process_builder.append(";");

  nf_process_builder.append_new_line();

  nf_process_builder.indent();
  nf_process_builder.append(BDD::symbex::FN_BORROW_SECRET);
  nf_process_builder.append("(");
  nf_process_builder.append("*");
  nf_process_builder.append(PACKET_VAR_LABEL);
  nf_process_builder.append(", ");
  nf_process_builder.append(len_transpiled);
  nf_process_builder.append(", ");
  nf_process_builder.append("&");
  nf_process_builder.append(hdr_var.get_label());
  nf_process_builder.append(")");
  nf_process_builder.append(";");
  nf_process_builder.append_new_line();
}

void tfheGenerator::visit(const ExecutionPlanNode *ep_node,
                         const target::PacketBorrowNextChunk *node) {
  auto chunk_addr = node->get_chunk_addr();
  auto chunk = node->get_chunk();
  auto len = node->get_length();

  auto hdr_label = vars.get_new_label(HEADER_BASE_LABEL);
  auto hdr_var = ByteArray(hdr_label, len, chunk, chunk_addr);
  vars.append(hdr_var);

  auto len_transpiled = transpile(len);

  nf_process_builder.indent();
  nf_process_builder.append("void* ");
  nf_process_builder.append(hdr_var.get_label());
  nf_process_builder.append(";");

  nf_process_builder.append_new_line();

  nf_process_builder.indent();
  nf_process_builder.append(BDD::symbex::FN_BORROW_CHUNK);
  nf_process_builder.append("(");
  nf_process_builder.append("*");
  nf_process_builder.append(PACKET_VAR_LABEL);
  nf_process_builder.append(", ");
  nf_process_builder.append(len_transpiled);
  nf_process_builder.append(", ");
  nf_process_builder.append("&");
  nf_process_builder.append(hdr_var.get_label());
  nf_process_builder.append(")");
  nf_process_builder.append(";");
  nf_process_builder.append_new_line();
}

void tfheGenerator::visit(const ExecutionPlanNode *ep_node,
                         const target::PacketReturnChunk *node) {
  auto original_chunk = node->get_original_chunk();
  auto modifications = node->get_modifications();

  for (auto mod : modifications) {
    auto byte = mod.byte;
    auto expr = mod.expr;

    auto modified_byte = kutil::solver_toolbox.exprBuilder->Extract(
        original_chunk, byte * 8, klee::Expr::Int8);

    auto transpiled_byte = transpile(modified_byte);
    auto transpiled_expr = transpile(expr);

    nf_process_builder.indent();
    nf_process_builder.append(transpiled_byte);
    nf_process_builder.append(" = ");
    nf_process_builder.append(transpiled_expr);
    nf_process_builder.append(";");
    nf_process_builder.append_new_line();
  }
}

//void tfheGenerator::visit(const ExecutionPlanNode *ep_node,
//                         const target::If *node) {
//  auto _condition = node->get_condition();
//  auto condition = transpile(_condition);
//
//  nf_process_builder.indent();
//  nf_process_builder.append("if (");
//  nf_process_builder.append(condition);
//  nf_process_builder.append(") {");
//  nf_process_builder.append_new_line();
//
//  nf_process_builder.inc_indentation();
//
//  vars.push();
//  pending_ifs.push();
//}

void tfheGenerator::visit(const ExecutionPlanNode *ep_node,
                          const target::If *node) {
  auto _condition = node->get_condition();
//  std::cout << "\n\n\n\n\n\nCondition:\n" << (*_condition).getKid(0) << std::endl;
  auto condition = transpile(_condition);

  nf_process_builder.indent();
  nf_process_builder.append("let fn = || {");
  nf_process_builder.append_new_line();
  nf_process_builder.inc_indentation();
  nf_process_builder.indent();
  nf_process_builder.append("if (");
  nf_process_builder.append(condition);
  nf_process_builder.append(") {");
  nf_process_builder.append_new_line();

  nf_process_builder.inc_indentation();

  vars.push();
}

void tfheGenerator::visit(const ExecutionPlanNode *ep_node,
                          const target::TruthTablePBS *node) {
  // TODO Implement
}

void tfheGenerator::visit(const ExecutionPlanNode *ep_node,
                          const target::Conditional *node) {
    // TODO Implement
    auto _condition = node->get_condition();
    auto condition = transpile(_condition);

    nf_process_builder.indent();
    nf_process_builder.append("if (");
    nf_process_builder.append(condition);
    nf_process_builder.append(") {");
    nf_process_builder.append_new_line();

    nf_process_builder.inc_indentation();

    vars.push();
    pending_ifs.push();
}

void tfheGenerator::visit(const ExecutionPlanNode *ep_node,
                         const target::TernarySum *node) {
  nf_process_builder.indent();
  nf_process_builder.append("HE Operation");
  nf_process_builder.append(";");
  nf_process_builder.append_new_line();
}

void tfheGenerator::visit(const ExecutionPlanNode *ep_node,
                         const target::Then *node) {}

void tfheGenerator::visit(const ExecutionPlanNode *ep_node,
                         const target::Else *node) {
  vars.push();

  nf_process_builder.indent();
  nf_process_builder.append("else {");
  nf_process_builder.append_new_line();
  nf_process_builder.inc_indentation();
}

void tfheGenerator::visit(const ExecutionPlanNode *ep_node,
                         const target::Forward *node) {
  auto port = node->get_port();

  nf_process_builder.indent();
  nf_process_builder.append("return ");
  nf_process_builder.append(port);
  nf_process_builder.append(";");
  nf_process_builder.append_new_line();

  auto closed = pending_ifs.close();

  for (auto i = 0; i < closed; i++) {
    vars.pop();
  }
}

void tfheGenerator::visit(const ExecutionPlanNode *ep_node,
                         const target::Broadcast *node) {
  nf_process_builder.indent();
  nf_process_builder.append("return ");
  nf_process_builder.append(FLOOD_DEVICE);
  nf_process_builder.append(";");
  nf_process_builder.append_new_line();

  auto closed = pending_ifs.close();

  for (auto i = 0; i < closed; i++) {
    vars.pop();
  }
}

void tfheGenerator::visit(const ExecutionPlanNode *ep_node,
                         const target::Drop *node) {
  nf_process_builder.indent();
  nf_process_builder.append("return ");
  nf_process_builder.append(DEVICE_VAR_LABEL);
  nf_process_builder.append(";");
  nf_process_builder.append_new_line();

  auto closed = pending_ifs.close();

  for (auto i = 0; i < closed; i++) {
    vars.pop();
  }
}

void tfheGenerator::visit(const ExecutionPlanNode *ep_node,
                         const target::ExpireItemsSingleMap *node) {
  auto map_addr = node->get_map_addr();
  auto vector_addr = node->get_vector_addr();
  auto dchain_addr = node->get_dchain_addr();
  auto time = node->get_time();
  auto num_freed_flows = node->get_number_of_freed_flows();

  auto map = vars.get(map_addr);
  auto vector = vars.get(vector_addr);
  auto dchain = vars.get(dchain_addr);

  assert(map.valid);
  assert(vector.valid);
  assert(dchain.valid);

  auto time_transpiled = transpile(time);

  auto freed_flows_label = vars.get_new_label(NUM_FREED_FLOWS_BASE_LABEL);
  auto freed_flows_var = Variable(freed_flows_label, num_freed_flows);
  vars.append(freed_flows_var);

  nf_process_builder.indent();
  nf_process_builder.append(freed_flows_var.get_type());
  nf_process_builder.append(" ");
  nf_process_builder.append(freed_flows_var.get_label());
  nf_process_builder.append(" = ");
  nf_process_builder.append(BDD::symbex::FN_EXPIRE_MAP);
  nf_process_builder.append("(");
  nf_process_builder.append(dchain.var->get_label());
  nf_process_builder.append(", ");
  nf_process_builder.append(vector.var->get_label());
  nf_process_builder.append(", ");
  nf_process_builder.append(map.var->get_label());
  nf_process_builder.append(", ");
  nf_process_builder.append(time_transpiled);
  nf_process_builder.append(");");
  nf_process_builder.append_new_line();
}

void tfheGenerator::visit(const ExecutionPlanNode *ep_node,
                         const target::ExpireItemsSingleMapIteratively *node) {
  auto map_addr = node->get_map_addr();
  auto vector_addr = node->get_vector_addr();
  auto start = node->get_start();
  auto n_elems = node->get_n_elems();

  auto map = vars.get(map_addr);
  auto vector = vars.get(vector_addr);

  assert(map.valid);
  assert(vector.valid);

  auto start_transpiled = transpile(start);
  auto n_elems_transpiled = transpile(n_elems);

  nf_process_builder.indent();
  nf_process_builder.append(BDD::symbex::FN_EXPIRE_MAP_ITERATIVELY);
  nf_process_builder.append("(");
  nf_process_builder.append(vector.var->get_label());
  nf_process_builder.append(", ");
  nf_process_builder.append(map.var->get_label());
  nf_process_builder.append(", ");
  nf_process_builder.append(start_transpiled);
  nf_process_builder.append(", ");
  nf_process_builder.append(n_elems_transpiled);
  nf_process_builder.append(");");
  nf_process_builder.append_new_line();
}

void tfheGenerator::visit(const ExecutionPlanNode *ep_node,
                         const target::RteEtherAddrHash *node) {
  assert(false && "TODO");
}

void tfheGenerator::visit(const ExecutionPlanNode *ep_node,
                         const target::DchainRejuvenateIndex *node) {
  auto dchain_addr = node->get_dchain_addr();
  auto index = node->get_index();
  auto time = node->get_time();

  auto dchain = vars.get(dchain_addr);
  assert(dchain.valid);

  auto index_transpiled = transpile(index);
  auto time_transpiled = transpile(time);

  nf_process_builder.indent();
  nf_process_builder.append(BDD::symbex::FN_DCHAIN_REJUVENATE);
  nf_process_builder.append("(");
  nf_process_builder.append(dchain.var->get_label());
  nf_process_builder.append(", ");
  nf_process_builder.append(index_transpiled);
  nf_process_builder.append(", ");
  nf_process_builder.append(time_transpiled);
  nf_process_builder.append(");");
  nf_process_builder.append_new_line();
}

void tfheGenerator::visit(const ExecutionPlanNode *ep_node,
                         const target::DchainFreeIndex *node) {
  auto dchain_addr = node->get_dchain_addr();
  auto index = node->get_index();

  auto dchain = vars.get(dchain_addr);
  assert(dchain.valid);

  auto index_transpiled = transpile(index);

  nf_process_builder.indent();
  nf_process_builder.append(BDD::symbex::FN_DCHAIN_FREE_INDEX);
  nf_process_builder.append("(");
  nf_process_builder.append(dchain.var->get_label());
  nf_process_builder.append(", ");
  nf_process_builder.append(index_transpiled);
  nf_process_builder.append(");");
  nf_process_builder.append_new_line();
}

void tfheGenerator::visit(const ExecutionPlanNode *ep_node,
                         const target::VectorBorrow *node) {
  auto vector_addr = node->get_vector_addr();
  auto index = node->get_index();
  auto value_out = node->get_value_out();
  auto borrowed_cell = node->get_borrowed_cell();

  auto vector = vars.get(vector_addr);
  assert(vector.valid);

  auto index_transpiled = transpile(index);

  auto value_out_label = vars.get_new_label(VALUE_OUT_BASE_LABEL);
  auto value_out_var = ByteArray(value_out_label, borrowed_cell, value_out);
  value_out_var.set_addr(value_out);
  value_out_var.set_is_array();
  vars.append(value_out_var);

  nf_process_builder.indent();
  nf_process_builder.append("uint8_t *");
  nf_process_builder.append(value_out_var.get_label());
  nf_process_builder.append(" = 0;");
  nf_process_builder.append_new_line();

  nf_process_builder.indent();
  nf_process_builder.append(BDD::symbex::FN_VECTOR_BORROW);
  nf_process_builder.append("(");
  nf_process_builder.append(vector.var->get_label());
  nf_process_builder.append(", ");
  nf_process_builder.append(index_transpiled);
  nf_process_builder.append(", ");
  nf_process_builder.append("(void**)(&");
  nf_process_builder.append(value_out_var.get_label());
  nf_process_builder.append(")");
  nf_process_builder.append(");");
  nf_process_builder.append_new_line();
}

void tfheGenerator::visit(const ExecutionPlanNode *ep_node,
                         const target::VectorReturn *node) {
  auto vector_addr = node->get_vector_addr();
  auto index = node->get_index();
  auto value_addr = node->get_value_addr();
  auto modifications = node->get_modifications();

  auto vector = vars.get(vector_addr);
  assert(vector.valid);

  auto index_transpiled = transpile(index);

  auto value_varq = vars.get(value_addr);
  assert(value_varq.valid);

  for (auto modification : modifications) {
    auto new_byte_transpiled = transpile(modification.expr);

    nf_process_builder.indent();
    nf_process_builder.append(value_varq.var->get_label());
    nf_process_builder.append("[");
    nf_process_builder.append(modification.byte);
    nf_process_builder.append("]");
    nf_process_builder.append(" = ");
    nf_process_builder.append(new_byte_transpiled);
    nf_process_builder.append(";");
    nf_process_builder.append_new_line();
  }

  nf_process_builder.indent();
  nf_process_builder.append(BDD::symbex::FN_VECTOR_RETURN);
  nf_process_builder.append("(");
  nf_process_builder.append(vector.var->get_label());
  nf_process_builder.append(", ");
  nf_process_builder.append(index_transpiled);
  nf_process_builder.append(", ");
  nf_process_builder.append(value_varq.var->get_label());
  nf_process_builder.append(");");
  nf_process_builder.append_new_line();
}

void tfheGenerator::visit(const ExecutionPlanNode *ep_node,
                         const target::DchainAllocateNewIndex *node) {
  auto dchain_addr = node->get_dchain_addr();
  auto time = node->get_time();
  auto index_out = node->get_index_out();
  auto out_of_space = node->get_out_of_space();

  auto dchain = vars.get(dchain_addr);
  assert(dchain.valid);

  auto time_transpiled = transpile(time);

  auto index_out_label = vars.get_new_label(INDEX_OUT_BASE_LABEL);
  auto index_out_var = Variable(index_out_label, index_out);
  vars.append(index_out_var);

  auto out_of_space_label = vars.get_new_label(OUT_OF_SPACE_BASE_LABEL);
  auto out_of_space_var = Variable(out_of_space_label, out_of_space);
  vars.append(out_of_space_var);

  nf_process_builder.indent();
  nf_process_builder.append(index_out_var.get_type());
  nf_process_builder.append(" ");
  nf_process_builder.append(index_out_var.get_label());
  nf_process_builder.append(";");
  nf_process_builder.append_new_line();

  nf_process_builder.indent();
  nf_process_builder.append(out_of_space_var.get_type());
  nf_process_builder.append(" ");
  nf_process_builder.append(out_of_space_var.get_label());
  nf_process_builder.append(" = ");
  nf_process_builder.append("!");
  nf_process_builder.append(BDD::symbex::FN_DCHAIN_ALLOCATE_NEW_INDEX);
  nf_process_builder.append("(");
  nf_process_builder.append(dchain.var->get_label());
  nf_process_builder.append(", ");
  nf_process_builder.append("&");
  nf_process_builder.append(index_out_var.get_label());
  nf_process_builder.append(", ");
  nf_process_builder.append(time_transpiled);
  nf_process_builder.append(");");
  nf_process_builder.append_new_line();
}

void tfheGenerator::visit(const ExecutionPlanNode *ep_node,
                         const target::MapPut *node) {
  auto map_addr = node->get_map_addr();
  auto key = node->get_key();
  auto value = node->get_value();

  auto map = vars.get(map_addr);
  assert(map.valid);

  auto key_label_base = map.var->get_label() + "_key";
  auto key_label = vars.get_new_label(key_label_base);

  assert(key->getWidth() > 0);
  assert(key->getWidth() % 8 == 0);

  nf_process_builder.indent();
  nf_process_builder.append("uint8_t ");
  nf_process_builder.append(key_label);
  nf_process_builder.append("[");
  nf_process_builder.append(key->getWidth() / 8);
  nf_process_builder.append("];");
  nf_process_builder.append_new_line();

  for (bits_t b = 0u; b < key->getWidth(); b += 8) {
    auto byte = kutil::solver_toolbox.exprBuilder->Extract(key, b, 8);
    auto byte_transpiled = transpile(byte);

    nf_process_builder.indent();
    nf_process_builder.append(key_label);
    nf_process_builder.append("[");
    nf_process_builder.append(b / 8);
    nf_process_builder.append("]");
    nf_process_builder.append(" = ");
    nf_process_builder.append(byte_transpiled);
    nf_process_builder.append(";");
    nf_process_builder.append_new_line();
  }

  auto value_transpiled = transpile(value);

  nf_process_builder.indent();
  nf_process_builder.append(BDD::symbex::FN_MAP_PUT);
  nf_process_builder.append("(");
  nf_process_builder.append(map.var->get_label());
  nf_process_builder.append(", ");
  nf_process_builder.append("(void*)");
  nf_process_builder.append(key_label);
  nf_process_builder.append(", ");
  nf_process_builder.append(value_transpiled);
  nf_process_builder.append(");");
  nf_process_builder.append_new_line();
}

void tfheGenerator::visit(const ExecutionPlanNode *ep_node,
                         const target::PacketGetUnreadLength *node) {
  assert(false && "TODO");
}

void tfheGenerator::visit(const ExecutionPlanNode *ep_node,
                         const target::SetIpv4UdpTcpChecksum *node) {
  auto ip_hdr_addr = node->get_ip_header_addr();
  auto l4_hdr_addr = node->get_l4_header_addr();
  auto checksum = node->get_checksum();

  auto ip_hdr = vars.get(ip_hdr_addr);
  auto l4_hdr = vars.get(l4_hdr_addr);

  assert(ip_hdr.valid);
  assert(l4_hdr.valid);

  auto checksum_label = vars.get_new_label(CHECKSUM_BASE_LABEL);
  auto checksum_var = Variable(checksum_label, 32, {checksum.label});
  vars.append(checksum_var);

  nf_process_builder.indent();
  nf_process_builder.append(checksum_var.get_type());
  nf_process_builder.append(" ");
  nf_process_builder.append(checksum_var.get_label());
  nf_process_builder.append(" = ");
  nf_process_builder.append(BDD::symbex::FN_RTE_SET_CHECKSUM);
  nf_process_builder.append("(");
  nf_process_builder.append("(");
  nf_process_builder.append(BDD::symbex::RTE_IPV4_TYPE);
  nf_process_builder.append("*)");
  nf_process_builder.append(ip_hdr.var->get_label());
  nf_process_builder.append(", ");
  nf_process_builder.append("(");
  nf_process_builder.append(BDD::symbex::RTE_L4_TYPE);
  nf_process_builder.append("*)");
  nf_process_builder.append(l4_hdr.var->get_label());
  nf_process_builder.append(");");
  nf_process_builder.append_new_line();
}

void tfheGenerator::visit(const ExecutionPlanNode *ep_node,
                         const target::DchainIsIndexAllocated *node) {
  auto dchain_addr = node->get_dchain_addr();
  auto index = node->get_index();
  auto is_allocated = node->get_is_allocated();

  auto dchain = vars.get(dchain_addr);
  assert(dchain.valid);

  auto is_allocated_label = vars.get_new_label(IS_INDEX_ALLOCATED_BASE_LABEL);
  auto is_allocated_var = Variable(is_allocated_label, is_allocated);
  vars.append(is_allocated_var);

  auto index_transpiled = transpile(index);

  nf_process_builder.indent();
  nf_process_builder.append(is_allocated_var.get_type());
  nf_process_builder.append(" ");
  nf_process_builder.append(is_allocated_var.get_label());
  nf_process_builder.append(" = ");
  nf_process_builder.append(BDD::symbex::FN_DCHAIN_IS_ALLOCATED);
  nf_process_builder.append("(");
  nf_process_builder.append(dchain.var->get_label());
  nf_process_builder.append(", ");
  nf_process_builder.append(index_transpiled);
  nf_process_builder.append(");");
  nf_process_builder.append_new_line();
}

void tfheGenerator::visit(const ExecutionPlanNode *ep_node,
                         const target::SketchComputeHashes *node) {
  auto sketch_addr = node->get_sketch_addr();
  auto key = node->get_key();

  auto sketch = vars.get(sketch_addr);
  assert(sketch.valid);

  auto key_label_base = sketch.var->get_label() + "_key";
  auto key_label = vars.get_new_label(key_label_base);

  assert(key->getWidth() > 0);
  assert(key->getWidth() % 8 == 0);

  nf_process_builder.indent();
  nf_process_builder.append("uint8_t ");
  nf_process_builder.append(key_label);
  nf_process_builder.append("[");
  nf_process_builder.append(key->getWidth() / 8);
  nf_process_builder.append("];");
  nf_process_builder.append_new_line();

  for (bits_t b = 0u; b < key->getWidth(); b += 8) {
    auto byte = kutil::solver_toolbox.exprBuilder->Extract(key, b, 8);
    auto byte_transpiled = transpile(byte);

    nf_process_builder.indent();
    nf_process_builder.append(key_label);
    nf_process_builder.append("[");
    nf_process_builder.append(b / 8);
    nf_process_builder.append("]");
    nf_process_builder.append(" = ");
    nf_process_builder.append(byte_transpiled);
    nf_process_builder.append(";");
    nf_process_builder.append_new_line();
  }

  nf_process_builder.indent();
  nf_process_builder.append(BDD::symbex::FN_SKETCH_COMPUTE_HASHES);
  nf_process_builder.append("(");
  nf_process_builder.append(sketch.var->get_label());
  nf_process_builder.append(", ");
  nf_process_builder.append("(void*)");
  nf_process_builder.append(key_label);
  nf_process_builder.append(");");
  nf_process_builder.append_new_line();
}

void tfheGenerator::visit(const ExecutionPlanNode *ep_node,
                         const target::SketchExpire *node) {
  auto sketch_addr = node->get_sketch_addr();
  auto time = node->get_time();

  auto sketch = vars.get(sketch_addr);
  assert(sketch.valid);

  auto time_transpiled = transpile(time);

  nf_process_builder.indent();
  nf_process_builder.append(BDD::symbex::FN_SKETCH_EXPIRE);
  nf_process_builder.append("(");
  nf_process_builder.append(sketch.var->get_label());
  nf_process_builder.append(", ");
  nf_process_builder.append(time_transpiled);
  nf_process_builder.append(");");
  nf_process_builder.append_new_line();
}

void tfheGenerator::visit(const ExecutionPlanNode *ep_node,
                         const target::SketchFetch *node) {
  auto sketch_addr = node->get_sketch_addr();
  auto overflow = node->get_overflow();

  auto sketch = vars.get(sketch_addr);
  assert(sketch.valid);

  auto overflow_label = vars.get_new_label(OVERFLOW_BASE_LABEL);
  auto overflow_var = Variable(overflow_label, overflow);
  vars.append(overflow_var);

  nf_process_builder.indent();
  nf_process_builder.append(overflow_var.get_type());
  nf_process_builder.append(" ");
  nf_process_builder.append(overflow_var.get_label());
  nf_process_builder.append(" = ");
  nf_process_builder.append(BDD::symbex::FN_SKETCH_FETCH);
  nf_process_builder.append("(");
  nf_process_builder.append(sketch.var->get_label());
  nf_process_builder.append(");");
  nf_process_builder.append_new_line();
}

void tfheGenerator::visit(const ExecutionPlanNode *ep_node,
                         const target::SketchRefresh *node) {
  auto sketch_addr = node->get_sketch_addr();
  auto time = node->get_time();

  auto sketch = vars.get(sketch_addr);
  assert(sketch.valid);

  auto time_transpiled = transpile(time);

  nf_process_builder.indent();
  nf_process_builder.append(BDD::symbex::FN_SKETCH_REFRESH);
  nf_process_builder.append("(");
  nf_process_builder.append(sketch.var->get_label());
  nf_process_builder.append(", ");
  nf_process_builder.append(time_transpiled);
  nf_process_builder.append(");");
  nf_process_builder.append_new_line();
}

void tfheGenerator::visit(const ExecutionPlanNode *ep_node,
                         const target::SketchTouchBuckets *node) {
  auto sketch_addr = node->get_sketch_addr();
  auto time = node->get_time();

  auto sketch = vars.get(sketch_addr);
  assert(sketch.valid);

  auto time_transpiled = transpile(time);

  nf_process_builder.indent();
  nf_process_builder.append(BDD::symbex::FN_SKETCH_TOUCH_BUCKETS);
  nf_process_builder.append("(");
  nf_process_builder.append(sketch.var->get_label());
  nf_process_builder.append(", ");
  nf_process_builder.append(time_transpiled);
  nf_process_builder.append(");");
  nf_process_builder.append_new_line();
}

void tfheGenerator::visit(const ExecutionPlanNode *ep_node,
                         const target::MapErase *node) {
  auto map_addr = node->get_map_addr();
  auto key = node->get_key();
  auto trash = node->get_trash();

  auto trash_label = vars.get_new_label(TRASH_BASE_LABEL);
  auto trash_var = ByteArray(trash_label, trash);
  vars.append(trash_var);

  nf_process_builder.indent();
  nf_process_builder.append(trash_var.get_type());
  nf_process_builder.append(" ");
  nf_process_builder.append(trash_var.get_label());
  nf_process_builder.append(";");
  nf_process_builder.append_new_line();

  auto map = vars.get(map_addr);
  assert(map.valid);

  auto key_label_base = map.var->get_label() + "_key";
  auto key_label = vars.get_new_label(key_label_base);

  assert(key->getWidth() > 0);
  assert(key->getWidth() % 8 == 0);

  nf_process_builder.indent();
  nf_process_builder.append("uint8_t ");
  nf_process_builder.append(key_label);
  nf_process_builder.append("[");
  nf_process_builder.append(key->getWidth() / 8);
  nf_process_builder.append("];");
  nf_process_builder.append_new_line();

  for (bits_t b = 0u; b < key->getWidth(); b += 8) {
    auto byte = kutil::solver_toolbox.exprBuilder->Extract(key, b, 8);
    auto byte_transpiled = transpile(byte);

    nf_process_builder.indent();
    nf_process_builder.append(key_label);
    nf_process_builder.append("[");
    nf_process_builder.append(b / 8);
    nf_process_builder.append("]");
    nf_process_builder.append(" = ");
    nf_process_builder.append(byte_transpiled);
    nf_process_builder.append(";");
    nf_process_builder.append_new_line();
  }

  nf_process_builder.indent();
  nf_process_builder.append(BDD::symbex::FN_MAP_ERASE);
  nf_process_builder.append("(");
  nf_process_builder.append(map.var->get_label());
  nf_process_builder.append(", ");
  nf_process_builder.append("(void*)");
  nf_process_builder.append(key_label);
  nf_process_builder.append(", ");
  nf_process_builder.append("(void**)&");
  nf_process_builder.append(trash_var.get_label());
  nf_process_builder.append(");");
  nf_process_builder.append_new_line();
}

void tfheGenerator::visit(const ExecutionPlanNode *ep_node,
                         const target::LoadBalancedFlowHash *node) {
  auto obj = node->get_obj();
  auto hash = node->get_hash();

  auto obj_label = vars.get_new_label(OBJ_BASE_LABEL);

  assert(obj->getWidth() > 0);
  assert(obj->getWidth() % 8 == 0);

  nf_process_builder.indent();
  nf_process_builder.append("uint8_t ");
  nf_process_builder.append(obj_label);
  nf_process_builder.append("[");
  nf_process_builder.append(obj->getWidth() / 8);
  nf_process_builder.append("];");
  nf_process_builder.append_new_line();

  for (bits_t b = 0u; b < obj->getWidth(); b += 8) {
    auto byte = kutil::solver_toolbox.exprBuilder->Extract(obj, b, 8);
    auto byte_transpiled = transpile(byte);

    nf_process_builder.indent();
    nf_process_builder.append(obj_label);
    nf_process_builder.append("[");
    nf_process_builder.append(b / 8);
    nf_process_builder.append("]");
    nf_process_builder.append(" = ");
    nf_process_builder.append(byte_transpiled);
    nf_process_builder.append(";");
    nf_process_builder.append_new_line();
  }

  auto hash_label = vars.get_new_label(LOADBALANCED_FLOW_HASH_BASE_LABEL);
  auto hash_var = Variable(hash_label, hash);
  vars.append(hash_var);

  nf_process_builder.indent();
  nf_process_builder.append(hash_var.get_type());
  nf_process_builder.append(" ");
  nf_process_builder.append(hash_var.get_label());
  nf_process_builder.append(" = ");
  nf_process_builder.append(BDD::symbex::FN_LOADBALANCEDFLOW_HASH);
  nf_process_builder.append("(");
  nf_process_builder.append("(void*)");
  nf_process_builder.append(obj_label);
  nf_process_builder.append(");");
  nf_process_builder.append_new_line();
}

void tfheGenerator::visit(const ExecutionPlanNode *ep_node,
                         const target::ChtFindBackend *node) {
  auto hash = node->get_hash();
  auto cht_addr = node->get_cht_addr();
  auto active_backends_addr = node->get_active_backends_addr();
  auto cht_height = node->get_cht_height();
  auto backend_capacity = node->get_backend_capacity();
  auto chosen_backend = node->get_chosen_backend();
  auto found = node->get_found();

  auto found_label = vars.get_new_label(PREFERED_BACKEND_BASE_LABEL);
  auto found_var = Variable(found_label, 32, {found.label});
  vars.append(found_var);

  auto hash_transpiled = transpile(hash);
  auto cht_height_transpiled = transpile(cht_height);
  auto backend_capacity_transpiled = transpile(backend_capacity);

  auto cht = vars.get(cht_addr);
  assert(cht.valid);

  auto dchain = vars.get(active_backends_addr);
  assert(dchain.valid);

  auto chosen_backend_label = vars.get_new_label(CHOSEN_BACKEND_BASE_LABEL);
  auto chosen_backend_var = Variable(chosen_backend_label, chosen_backend);
  vars.append(chosen_backend_var);

  nf_process_builder.indent();
  nf_process_builder.append(chosen_backend_var.get_type());
  nf_process_builder.append(" ");
  nf_process_builder.append(chosen_backend_var.get_label());
  nf_process_builder.append(";");
  nf_process_builder.append_new_line();

  nf_process_builder.indent();
  nf_process_builder.append(found_var.get_type());
  nf_process_builder.append(" ");
  nf_process_builder.append(found_var.get_label());
  nf_process_builder.append(" = ");
  nf_process_builder.append(BDD::symbex::FN_CHT_FIND_BACKEND);
  nf_process_builder.append("(");
  nf_process_builder.append(hash_transpiled);
  nf_process_builder.append(", ");
  nf_process_builder.append(cht.var->get_label());
  nf_process_builder.append(", ");
  nf_process_builder.append(dchain.var->get_label());
  nf_process_builder.append(", ");
  nf_process_builder.append(cht_height_transpiled);
  nf_process_builder.append(", ");
  nf_process_builder.append(backend_capacity_transpiled);
  nf_process_builder.append(", ");
  nf_process_builder.append("&");
  nf_process_builder.append(chosen_backend_var.get_label());
  nf_process_builder.append(");");
  nf_process_builder.append_new_line();
}

void tfheGenerator::visit(const ExecutionPlanNode *ep_node,
                         const target::HashObj *node) {
  auto obj_addr = node->get_obj_addr();
  auto size = node->get_size();
  auto hash = node->get_hash();

  auto obj = vars.get(obj_addr);
  assert(obj.valid);

  auto hash_label = vars.get_new_label(HASH_BASE_LABEL);
  auto hash_var = Variable(hash_label, hash);
  vars.append(hash_var);

  auto size_transpiled = transpile(size);

  nf_process_builder.indent();
  nf_process_builder.append(hash_var.get_type());
  nf_process_builder.append(" ");
  nf_process_builder.append(hash_var.get_label());
  nf_process_builder.append(" = ");
  nf_process_builder.append(BDD::symbex::FN_HASH_OBJ);
  nf_process_builder.append("(");
  nf_process_builder.append("(void*)");
  nf_process_builder.append(obj.var->get_label());
  nf_process_builder.append(", ");
  nf_process_builder.append(size_transpiled);
  nf_process_builder.append(");");
  nf_process_builder.append_new_line();
}

} // namespace tfhe
} // namespace synthesizer
} // namespace synapse