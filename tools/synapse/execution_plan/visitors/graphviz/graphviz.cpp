#include "graphviz.h"

#include "../../../heuristics/heuristic.h"
#include "../../../log.h"
#include "../../../search_space.h"
#include "../../execution_plan.h"
#include "../../modules/modules.h"
#include "../visitor.h"

#include <ctime>
#include <fstream>
#include <limits>
#include <math.h>
#include <unistd.h>

#define DEFAULT_VISIT_PRINT_MODULE_NAME(M)                                     \
  void Graphviz::visit(const M *node) {                                        \
    function_call(node->get_node(), node->get_target(), node->get_name());     \
  }

namespace synapse {

void find_and_replace(
    std::string &str,
    const std::vector<std::pair<std::string, std::string>> &replacements) {
  for (const auto &replacement : replacements) {
    auto before = replacement.first;
    auto after = replacement.second;

    std::string::size_type n = 0;
    while ((n = str.find(before, n)) != std::string::npos) {
      str.replace(n, before.size(), after);
      n += after.size();
    }
  }
}

void sanitize_html_label(std::string &label) {
  find_and_replace(label, {
                              {"{", "&#123;"},
                              {"}", "&#125;"},
                              {"[", "&#91;"},
                              {"]", "&#93;"},
                              {"<", "&lt;"},
                              {">", "&gt;"},
                              {"\n", "<br/>"},
                          });
}

std::string Graphviz::get_rand_fname() const {
  std::stringstream ss;
  static unsigned counter = 1;

  ss << prefix;

  srand((unsigned)std::time(NULL) * getpid() + counter);

  for (int i = 0; i < fname_len; i++) {
    ss << alphanum[rand() % (strlen(alphanum) - 1)];
  }

  ss << ".gv";

  counter++;
  return ss.str();
}

void Graphviz::open() {
  std::string file_path = __FILE__;
  std::string dir_path = file_path.substr(0, file_path.rfind("/"));
  std::string script = "open_graph.sh";
  std::string cmd = dir_path + "/" + script + " " + fpath;

  static int counter = 0;

  for (auto bdd_fpath : bdd_fpaths) {
    cmd += " " + bdd_fpath;
    counter++;
  }

#ifndef NDEBUG
  std::cerr << "Opening " << fpath << "\n";
#endif

  system(cmd.c_str());
}

void Graphviz::function_call(BDD::BDDNode_ptr node, TargetType target,
                             std::string label) {
  assert(node_colors.find(target) != node_colors.end());
  ofs << "[label=\"";

  if (node) {
    ofs << "[";
    ofs << node->get_id();
    ofs << "] ";
  }

  ofs << label << "\", ";
  ofs << "color=" << node_colors[target] << "];";
  ofs << "\n";
}

Graphviz::rgb_t Graphviz::get_color(float f) const {
  Graphviz::rgb_t rgb;

  // float to RGB colormap : long rainbow
  // source: https://www.particleincell.com/2014/colormap/

  float group, color_value;
  color_value = 255 * modf(f * 5.0f, &group);

  int int_group = (int)group;
  int int_color_value = (int)color_value;

  switch (int_group) {
  case 0:
    rgb.r = 255;
    rgb.g = int_color_value;
    rgb.b = 0;
    break;
  case 1:
    rgb.r = 255 - int_color_value;
    rgb.g = 255;
    rgb.b = 0;
    break;
  case 2:
    rgb.r = 0;
    rgb.g = 255;
    rgb.b = int_color_value;
    break;
  case 3:
    rgb.r = 0;
    rgb.g = 255 - int_color_value;
    rgb.b = 255;
    break;
  case 4:
    rgb.r = int_color_value;
    rgb.g = 0;
    rgb.b = 255;
    break;
  case 5:
    rgb.r = 255;
    rgb.g = 0;
    rgb.b = 255;
    break;
  }

  return rgb;
}

void Graphviz::dump_bdd(const BDD::BDD &bdd,
                        const std::unordered_set<BDD::node_id_t> &processed,
                        const BDD::Node *next) {
  std::string leaf_fpath = get_rand_fname();
  bdd_fpaths.push_back(leaf_fpath);
  std::ofstream leaf_ofs;

  leaf_ofs.open(leaf_fpath);

  leaf_ofs << "digraph bdd_next {\n";
  leaf_ofs << "layout=\"dot\";\n";
  // leaf_ofs << "ratio=\"fill\";\n";
  // leaf_ofs << "size=\"12,12!\";\n";
  // leaf_ofs << "margin=0;\n";
  leaf_ofs << "node [shape=box,style=filled];\n";

  BDD::GraphvizGenerator bdd_graphviz(leaf_ofs, processed, next);

  assert(bdd.get_process());
  bdd.get_process()->visit(bdd_graphviz);
  leaf_ofs << "}\n";

  leaf_ofs.flush();
  leaf_ofs.close();
}

std::string Graphviz::get_bdd_node_name(const BDD::Node *node) const {
  assert(node);
  std::stringstream ss;

  switch (node->get_type()) {
  case BDD::Node::NodeType::BRANCH: {
    auto branch = static_cast<const BDD::Branch *>(node);
    ss << "if(";
    ss << kutil::expr_to_string(branch->get_condition(), true);
    ss << ")";
    break;
  }
  case BDD::Node::NodeType::CALL: {
    auto call = static_cast<const BDD::Call *>(node);
    ss << call->get_call().function_name;
    int i = 0;
    for (auto arg : call->get_call().args) {
      if (i > 0) {
        ss << ", ";
      }
      ss << kutil::expr_to_string(arg.second.expr, true);
      i++;
    }
    break;
  }
  case BDD::Node::NodeType::RETURN_PROCESS: {
    auto return_process = static_cast<const BDD::ReturnProcess *>(node);

    switch (return_process->get_return_operation()) {
    case BDD::ReturnProcess::Operation::BCAST: {
      ss << "broadcast()";
      break;
    }
    case BDD::ReturnProcess::Operation::DROP: {
      ss << "drop()";
      break;
    }
    case BDD::ReturnProcess::Operation::FWD: {
      ss << "forward(";
      ss << return_process->get_return_value();
      ss << ")";
      break;
    }
    default:
      assert(false);
    }

    break;
  }
  case BDD::Node::NodeType::RETURN_INIT:
    Log::err() << "return init\n";
    [[fallthrough]];
  case BDD::Node::NodeType::RETURN_RAW:
    Log::err() << "return raw\n";
    assert(false);
  }

  return ss.str();
}

void Graphviz::visualize(const ExecutionPlan &ep, bool interrupt) {
  if (ep.get_root()) {
    Graphviz gv;
    ep.visit(gv);
    gv.open();

    if (interrupt) {
      std::cout << "Press Enter to continue ";
      std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    }
  }
}

void Graphviz::visualize(const SearchSpace &search_space, bool interrupt) {
  Graphviz gv;
  gv.visit(search_space);
  gv.open();

  if (interrupt) {
    std::cout << "Press Enter to continue ";
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
  }
}

std::string stringify_score(const Score &score) {
  auto score_builder = std::stringstream();
  score_builder << score;
  auto score_str = score_builder.str();
  sanitize_html_label(score_str);

  return score_str;
}

std::string stringify_bdd_node(BDD::BDDNode_ptr node) {
  assert(node);

  constexpr int MAX_STR_SIZE = 250;

  auto node_builder = std::stringstream();

  node_builder << node->get_id();
  node_builder << ": ";

  switch (node->get_type()) {
  case BDD::Node::NodeType::CALL: {
    auto call_node = BDD_CAST_CALL(node);
    node_builder << call_node->get_call().function_name;
  } break;
  case BDD::Node::NodeType::BRANCH: {
    auto branch_node = BDD_CAST_BRANCH(node);
    auto condition = branch_node->get_condition();
    node_builder << "if (";
    node_builder << kutil::expr_to_string(condition);
    node_builder << ")";
  } break;
  case BDD::Node::NodeType::RETURN_PROCESS: {
    auto return_process = BDD_CAST_RETURN_PROCESS(node);

    switch (return_process->get_return_operation()) {
    case BDD::ReturnProcess::Operation::BCAST: {
      node_builder << "broadcast()";
    } break;
    case BDD::ReturnProcess::Operation::DROP: {
      node_builder << "drop()";
    } break;
    case BDD::ReturnProcess::Operation::FWD: {
      node_builder << "forward(";
      node_builder << return_process->get_return_value();
      node_builder << ")";
    } break;
    default:
      assert(false);
    }
  } break;
  default:
    assert(false);
  }

  auto node_str = node_builder.str();

  if (node_str.size() > MAX_STR_SIZE) {
    node_str = node_str.substr(0, MAX_STR_SIZE);
    node_str += " [...]";
  }

  sanitize_html_label(node_str);

  return node_str;
}

void visit_definitions(std::ofstream &ofs,
                       const std::map<TargetType, std::string> &node_colors,
                       const SearchSpace &search_space,
                       ss_node_ref search_space_node) {
  assert(search_space_node);

  auto node_id = search_space_node->node_id;
  auto next_nodes = search_space_node->next;
  auto data = search_space_node->data;
  auto target = search_space_node->data.target;
  auto target_color = node_colors.at(target);

  auto indent = [&](int lvl) { ofs << std::string(lvl, '\t'); };

  indent(1);
  ofs << node_id;
  ofs << " [label=<\n";

  indent(2);
  ofs << "<table";

  if (search_space.is_winner(search_space_node)) {
    ofs << " border=\"4\"";
    ofs << " bgcolor=\"blue\"";
    ofs << " color=\"green\"";
  }
  ofs << ">\n";

  // First row

  indent(3);
  ofs << "<tr>\n";

  indent(4);
  ofs << "<td ";
  ofs << "bgcolor=\"" << target_color << "\"";
  ofs << ">";
  ofs << "EP: " << data.execution_plan_id;
  ofs << "</td>\n";

  indent(4);
  ofs << "<td ";
  ofs << "bgcolor=\"" << target_color << "\"";
  ofs << ">";

  ofs << stringify_score(data.score);
  ofs << "</td>\n";

  indent(3);
  ofs << "</tr>\n";

  // Second row

  indent(3);
  ofs << "<tr>\n";

  indent(4);
  ofs << "<td";
  ofs << " bgcolor=\"" << target_color << "\"";
  ofs << " colspan=\"2\"";
  ofs << ">";
  if (data.module) {
    ofs << data.module->get_name();
  } else {
    ofs << "ROOT";
  }
  ofs << "</td>\n";

  indent(3);
  ofs << "</tr>\n";

  // Third row

  indent(3);
  ofs << "<tr>\n";

  indent(4);
  ofs << "<td ";
  ofs << " bgcolor=\"" << target_color << "\"";
  ofs << " colspan=\"2\"";
  ofs << ">";
  if (data.node) {
    ofs << stringify_bdd_node(data.node);
  }
  ofs << "</td>\n";

  indent(3);
  ofs << "</tr>\n";

  indent(2);
  ofs << "</table>\n";

  indent(1);
  ofs << ">]\n\n";

  for (auto next : next_nodes) {
    visit_definitions(ofs, node_colors, search_space, next);
  }
}

void visit_links(std::ofstream &ofs, ss_node_ref search_space_node) {
  assert(search_space_node);

  auto node_id = search_space_node->node_id;
  auto next_nodes = search_space_node->next;

  for (auto next : next_nodes) {
    ofs << node_id;
    ofs << " -> ";
    ofs << next->node_id;
    ofs << ";\n";
  }

  for (auto next : next_nodes) {
    visit_links(ofs, next);
  }
}

void Graphviz::visit(const SearchSpace &search_space) {
  ofs << "digraph SearchSpace {\n";

  ofs << "\tlayout=\"dot\";\n";
  ofs << "\tnode [shape=none];\n";

  auto root = search_space.get_root();

  visit_definitions(ofs, node_colors, search_space, root);
  visit_links(ofs, root);

  ofs << "}";
  ofs.flush();

  bdd_fpaths.clear();
}

void Graphviz::visit(ExecutionPlan ep) {
  ofs << "digraph ExecutionPlan {\n";
  // ofs << "ratio=\"fill\";\n";
  ofs << "layout=\"dot\";";
  // ofs << "size=\"12,12!\";\n";
  // ofs << "margin=0;\n";
  ofs << "node [shape=record,style=filled];\n";

  ExecutionPlanVisitor::visit(ep);

  ofs << "}\n";
  ofs.flush();

  auto bdd = ep.get_bdd();
  auto processed = ep.get_processed_bdd_nodes();
  const BDD::Node *next_node = nullptr;

  if (ep.get_next_node()) {
    next_node = ep.get_next_node().get();
  }

  bdd_fpaths.clear();
  dump_bdd(bdd, processed, next_node);
}

void Graphviz::visit(const ExecutionPlanNode *ep_node) {
  auto mod = ep_node->get_module();
  auto next = ep_node->get_next();
  auto id = ep_node->get_id();

  ofs << id << " ";
  ExecutionPlanVisitor::visit(ep_node);

  for (auto branch : next) {
    ofs << id << " -> " << branch->get_id() << ";"
        << "\n";
  }
}

void Graphviz::log(const ExecutionPlanNode *ep_node) const {
  // do nothing
}

/********************************************
 *
 *                x86 BMv2
 *
 ********************************************/

DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86_bmv2::MapGet)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86_bmv2::CurrentTime)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86_bmv2::PacketBorrowNextChunk)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86_bmv2::PacketGetMetadata)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86_bmv2::PacketReturnChunk)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86_bmv2::If)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86_bmv2::Then)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86_bmv2::Else)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86_bmv2::Forward)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86_bmv2::Broadcast)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86_bmv2::Drop)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86_bmv2::ExpireItemsSingleMap)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86_bmv2::RteEtherAddrHash)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86_bmv2::DchainRejuvenateIndex)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86_bmv2::VectorBorrow)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86_bmv2::VectorReturn)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86_bmv2::DchainAllocateNewIndex)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86_bmv2::MapPut)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86_bmv2::PacketGetUnreadLength)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86_bmv2::SetIpv4UdpTcpChecksum)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86_bmv2::DchainIsIndexAllocated)

/********************************************
 *
 *                   BMv2
 *
 ********************************************/

DEFAULT_VISIT_PRINT_MODULE_NAME(targets::bmv2::SendToController)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::bmv2::Ignore)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::bmv2::SetupExpirationNotifications)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::bmv2::If)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::bmv2::Then)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::bmv2::Else)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::bmv2::EthernetConsume)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::bmv2::EthernetModify)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::bmv2::TableLookup)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::bmv2::IPv4Consume)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::bmv2::IPv4Modify)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::bmv2::TcpUdpConsume)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::bmv2::TcpUdpModify)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::bmv2::IPOptionsConsume)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::bmv2::IPOptionsModify)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::bmv2::Drop)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::bmv2::Forward)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::bmv2::VectorReturn)

/********************************************
 *
 *                  Tofino
 *
 ********************************************/

DEFAULT_VISIT_PRINT_MODULE_NAME(targets::tofino::Ignore)

void Graphviz::visit(const targets::tofino::If *node) {
  std::stringstream label_builder;

  auto bdd_node = node->get_node();
  auto target = node->get_target();
  auto conditions = node->get_conditions();

  label_builder << "if(";
  for (auto i = 0u; i < conditions.size(); i++) {
    auto condition = conditions[i];

    if (i > 0) {
      label_builder << "\n&& ";
    }

    label_builder << kutil::expr_to_string(condition, true) << "\n";
  }
  label_builder << ")";

  auto label = label_builder.str();
  find_and_replace(label, {{"\n", "\n"}});

  function_call(bdd_node, target, label);
}

DEFAULT_VISIT_PRINT_MODULE_NAME(targets::tofino::IfHeaderValid)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::tofino::Then)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::tofino::Else)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::tofino::Forward)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::tofino::EthernetConsume)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::tofino::EthernetModify)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::tofino::IPv4Consume)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::tofino::IPv4Modify)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::tofino::IPv4OptionsConsume)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::tofino::IPv4OptionsModify)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::tofino::TCPUDPConsume)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::tofino::TCPUDPModify)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::tofino::IPv4TCPUDPChecksumsUpdate)

void Graphviz::visit_table(const targets::tofino::TableLookup *node,
                           bool simple) {
  std::stringstream label_builder;

  auto bdd_node = node->get_node();
  auto target = node->get_target();
  auto name = node->get_name();

  auto table = node->get_table();
  auto nodes = table->get_nodes();
  auto objs = table->get_objs();
  auto keys = table->get_keys();
  auto params = table->get_params();
  auto contains = table->get_hit();

  label_builder << name << "\n";

  label_builder << "  table: " << table->get_name() << "\n";
  label_builder << "  simple: " << simple << "\n";

  label_builder << "  expiring: ";
  label_builder << table->is_managing_expirations();
  label_builder << "\n";

  label_builder << "  nodes: [";
  for (auto node : nodes) {
    label_builder << node << ",";
  }
  label_builder << "]\n";

  label_builder << "  objs: [";
  for (auto obj : objs) {
    label_builder << obj << ",";
  }
  label_builder << "]\n";

  label_builder << "  keys (" << keys.size() << "): [";
  for (auto key : keys) {
    label_builder << "\n";
    label_builder << "    ";
    label_builder << "[";
    if (key.meta.size()) {
      for (auto meta : key.meta) {
        label_builder << meta.symbol << ",";
      }
    } else {
      label_builder << kutil::expr_to_string(key.expr, true) << ",";
    }
    label_builder << "]";
  }
  label_builder << "]\n";

  label_builder << "  params (" << params.size() << "): [";
  for (auto param : params) {
    label_builder << "\n";
    label_builder << "    ";
    label_builder << "\\{";
    label_builder << "objs:[";
    for (auto obj : param.objs) {
      label_builder << obj << ",";
    }
    label_builder << "]";
    label_builder << ",exprs:[";
    for (auto expr : param.exprs) {
      label_builder << kutil::expr_to_string(expr, true) << ",";
    }
    label_builder << "]";
    label_builder << "\\}";
  }
  label_builder << "]\n";

  label_builder << "  hit: [";
  for (auto c : contains) {
    label_builder << c.label << ",";
  }
  label_builder << "]\n";

  auto label = label_builder.str();
  find_and_replace(label, {{"\n", "\\l"}});

  function_call(bdd_node, target, label);
}

void Graphviz::visit(const targets::tofino::TableLookup *node) {
  visit_table(node, false);
}

void Graphviz::visit(const targets::tofino::TableLookupSimple *node) {
  visit_table(node, true);
}

DEFAULT_VISIT_PRINT_MODULE_NAME(targets::tofino::RegisterRead)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::tofino::IntegerAllocatorAllocate)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::tofino::IntegerAllocatorRejuvenate)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::tofino::IntegerAllocatorQuery)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::tofino::Drop)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::tofino::SendToController)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::tofino::SetupExpirationNotifications)

/********************************************
 *
 *                x86 Tofino
 *
 ********************************************/

DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86_tofino::Ignore)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86_tofino::PacketParseCPU)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86_tofino::PacketParseEthernet)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86_tofino::PacketModifyEthernet)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86_tofino::ForwardThroughTofino)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86_tofino::PacketParseIPv4)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86_tofino::PacketModifyIPv4)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86_tofino::PacketParseIPv4Options)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86_tofino::PacketModifyIPv4Options)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86_tofino::PacketParseTCPUDP)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86_tofino::PacketModifyTCPUDP)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86_tofino::PacketModifyChecksums)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86_tofino::If)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86_tofino::Then)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86_tofino::Else)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86_tofino::Drop)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86_tofino::MapGet)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86_tofino::MapPut)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86_tofino::EtherAddrHash)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86_tofino::DchainAllocateNewIndex)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86_tofino::DchainIsIndexAllocated)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86_tofino::DchainRejuvenateIndex)

} // namespace synapse