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
  void Graphviz::visit(const ExecutionPlanNode *ep_node, const M *node) {      \
    function_call(ep_node, node->get_node(), node->get_target(),               \
                  node->get_name());                                           \
  }

#define DEFAULT_BRANCH_VISIT_PRINT_MODULE_NAME(M)                              \
  void Graphviz::visit(const ExecutionPlanNode *ep_node, const M *node) {      \
    branch(ep_node, node->get_node(), node->get_target(), node->get_name());   \
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
  find_and_replace(
      label, {
                 {"&", "&amp;"}, // Careful, this needs to be the first
                                 // one, otherwise we mess everything up. Notice
                                 // that all the others use ampersands.
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

void Graphviz::function_call(const ExecutionPlanNode *ep_node,
                             BDD::Node_ptr node, TargetType target,
                             const std::string &label) {
  assert(node_colors.find(target) != node_colors.end());
  ofs << "[label=\"";

  ofs << "[";
  ofs << ep_node->get_id();
  ofs << "] ";

  ofs << label << "\", ";
  ofs << "color=" << node_colors[target] << "];";
  ofs << "\n";
}

void Graphviz::branch(const ExecutionPlanNode *ep_node, BDD::Node_ptr node,
                      TargetType target, const std::string &label) {
  assert(node_colors.find(target) != node_colors.end());
  ofs << "[shape=Mdiamond, label=\"";

  ofs << "[";
  ofs << ep_node->get_id();
  ofs << "] ";

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

  BDD::bdd_visualizer_opts_t opts;
  opts.processed.nodes = processed;
  opts.processed.next = next;
  BDD::GraphvizGenerator bdd_graphviz(leaf_ofs, opts);

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
    ss << kutil::pretty_print_expr(branch->get_condition());
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

std::string stringify_bdd_node(BDD::Node_ptr node) {
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
    node_builder << kutil::pretty_print_expr(condition);
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
  ofs << "EP: " << data.execution_plan;
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
  auto processed = ep.get_meta().processed_nodes;
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
DEFAULT_BRANCH_VISIT_PRINT_MODULE_NAME(targets::x86_bmv2::If)
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
DEFAULT_BRANCH_VISIT_PRINT_MODULE_NAME(targets::bmv2::If)
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

void Graphviz::visit(const ExecutionPlanNode *ep_node,
                     const targets::tofino::If *node) {
  std::stringstream label_builder;

  auto bdd_node = node->get_node();
  auto target = node->get_target();
  auto conditions = node->get_conditions();

  for (auto i = 0u; i < conditions.size(); i++) {
    auto condition = conditions[i];

    if (i > 0) {
      label_builder << "\n&& ";
    }

    label_builder << kutil::pretty_print_expr(condition) << "\n";
  }

  auto label = label_builder.str();
  find_and_replace(label, {{"\n", "\\n"}});

  branch(ep_node, bdd_node, target, label);
}

DEFAULT_VISIT_PRINT_MODULE_NAME(targets::tofino::Then)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::tofino::Else)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::tofino::Forward)

void Graphviz::visit(const ExecutionPlanNode *ep_node,
                     const targets::tofino::ParseCustomHeader *node) {
  std::stringstream label_builder;

  auto bdd_node = node->get_node();
  auto target = node->get_target();
  auto size = node->get_size();

  label_builder << "Parse header [";
  label_builder << size;
  label_builder << " bits (";
  label_builder << size / 8;
  label_builder << " bytes)]";

  auto label = label_builder.str();
  find_and_replace(label, {{"\n", "\\n"}});

  function_call(ep_node, bdd_node, target, label);
}

void Graphviz::visit(const ExecutionPlanNode *ep_node,
                     const targets::tofino::ParserCondition *node) {
  std::stringstream label_builder;

  auto bdd_node = node->get_node();
  auto target = node->get_target();
  auto condition = node->get_condition();
  auto apply_is_valid = node->get_apply_is_valid();

  label_builder << "Parser condition ";
  label_builder << " [apply: " << apply_is_valid << "]";
  label_builder << "\n";
  label_builder << kutil::pretty_print_expr(condition);

  auto label = label_builder.str();
  find_and_replace(label, {{"\n", "\\n"}});

  branch(ep_node, bdd_node, target, label);
}

DEFAULT_VISIT_PRINT_MODULE_NAME(targets::tofino::ModifyCustomHeader)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::tofino::IPv4TCPUDPChecksumsUpdate)

void Graphviz::visit(const ExecutionPlanNode *ep_node,
                     const targets::tofino::TableModule *node) {
  std::stringstream label_builder;

  auto bdd_node = node->get_node();
  auto target = node->get_target();
  auto table = node->get_table();

  assert(table);

  table->dump(label_builder);

  auto label = label_builder.str();
  find_and_replace(label, {{"\n", "\\l"}});
  function_call(ep_node, bdd_node, target, label);
}

void Graphviz::visit(const ExecutionPlanNode *ep_node,
                     const targets::tofino::TableLookup *node) {
  visit(ep_node, static_cast<const targets::tofino::TableModule *>(node));
}

void Graphviz::visit(const ExecutionPlanNode *ep_node,
                     const targets::tofino::TableRejuvenation *node) {
  visit(ep_node, static_cast<const targets::tofino::TableModule *>(node));
}

void Graphviz::visit(const ExecutionPlanNode *ep_node,
                     const targets::tofino::TableIsAllocated *node) {
  visit(ep_node, static_cast<const targets::tofino::TableModule *>(node));
}

DEFAULT_VISIT_PRINT_MODULE_NAME(targets::tofino::IntegerAllocatorAllocate)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::tofino::IntegerAllocatorRejuvenate)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::tofino::IntegerAllocatorQuery)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::tofino::Drop)

void Graphviz::visit(const ExecutionPlanNode *ep_node,
                     const targets::tofino::SendToController *node) {
  std::stringstream label_builder;

  auto bdd_node = node->get_node();
  auto target = node->get_target();
  auto cpu_path = node->get_cpu_code_path();
  auto state = node->get_dataplane_state();

  label_builder << "Send To Controller\n";
  label_builder << "\tPath ID: " << cpu_path << "\n";
  label_builder << "\tDataplane state:\n";
  for (auto s : state) {
    label_builder << "\t" << s.label << "\n";
  }

  auto label = label_builder.str();
  find_and_replace(label, {{"\n", "\\l"}});

  function_call(ep_node, bdd_node, target, label);
}

DEFAULT_VISIT_PRINT_MODULE_NAME(targets::tofino::SetupExpirationNotifications)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::tofino::CounterRead)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::tofino::CounterIncrement)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::tofino::HashObj)

/********************************************
 *
 *                x86 Tofino
 *
 ********************************************/

DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86_tofino::Ignore)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86_tofino::PacketParseEthernet)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86_tofino::PacketModifyEthernet)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86_tofino::ForwardThroughTofino)

void Graphviz::visit(const ExecutionPlanNode *ep_node,
                     const targets::x86_tofino::PacketParseCPU *node) {
  std::stringstream label_builder;

  auto bdd_node = node->get_node();
  auto target = node->get_target();
  auto state = node->get_dataplane_state();

  label_builder << "Parse CPU Header\n";
  label_builder << "  Dataplane state:\n";
  for (auto s : state) {
    label_builder << "    [";
    label_builder << s.label;
    label_builder << "]\n";
  }

  auto label = label_builder.str();
  find_and_replace(label, {{"\n", "\\l"}});

  function_call(ep_node, bdd_node, target, label);
}

DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86_tofino::PacketParseIPv4)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86_tofino::PacketModifyIPv4)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86_tofino::PacketParseIPv4Options)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86_tofino::PacketModifyIPv4Options)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86_tofino::PacketParseTCPUDP)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86_tofino::PacketModifyTCPUDP)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86_tofino::PacketModifyChecksums)
DEFAULT_BRANCH_VISIT_PRINT_MODULE_NAME(targets::x86_tofino::If)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86_tofino::Then)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86_tofino::Else)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86_tofino::Drop)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86_tofino::MapGet)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86_tofino::MapPut)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86_tofino::MapErase)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86_tofino::EtherAddrHash)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86_tofino::DchainAllocateNewIndex)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86_tofino::DchainIsIndexAllocated)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86_tofino::DchainRejuvenateIndex)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86_tofino::DchainFreeIndex)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86_tofino::PacketParseTCP)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86_tofino::PacketModifyTCP)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86_tofino::PacketParseUDP)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86_tofino::PacketModifyUDP)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86_tofino::HashObj)

/********************************************
 *
 *                     x86
 *
 ********************************************/

DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86::MapGet)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86::CurrentTime)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86::PacketBorrowNextChunk)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86::PacketReturnChunk)
DEFAULT_BRANCH_VISIT_PRINT_MODULE_NAME(targets::x86::If)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86::Then)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86::Else)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86::Forward)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86::Broadcast)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86::Drop)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86::ExpireItemsSingleMap)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86::ExpireItemsSingleMapIteratively)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86::RteEtherAddrHash)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86::DchainRejuvenateIndex)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86::VectorBorrow)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86::VectorReturn)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86::DchainAllocateNewIndex)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86::MapPut)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86::PacketGetUnreadLength)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86::SetIpv4UdpTcpChecksum)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86::DchainIsIndexAllocated)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86::SketchComputeHashes)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86::SketchExpire)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86::SketchFetch)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86::SketchRefresh)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86::SketchTouchBuckets)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86::MapErase)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86::DchainFreeIndex)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86::LoadBalancedFlowHash)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86::ChtFindBackend)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::x86::HashObj)

/********************************************
 *
 *                     tfhe
 *
 ********************************************/
// TODO GUI add modules here
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::tfhe::MapGet)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::tfhe::CurrentTime)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::tfhe::PacketBorrowNextChunk)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::tfhe::PacketBorrowNextSecret)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::tfhe::PacketReturnChunk)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::tfhe::TruthTablePBS)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::tfhe::Conditional) // FIXME This might be a problem
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::tfhe::MonoPBS) // FIXME This might be a problem
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::tfhe::TernarySum)
DEFAULT_BRANCH_VISIT_PRINT_MODULE_NAME(targets::tfhe::If)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::tfhe::Then)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::tfhe::Else)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::tfhe::Forward)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::tfhe::Broadcast)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::tfhe::Drop)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::tfhe::ExpireItemsSingleMap)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::tfhe::ExpireItemsSingleMapIteratively)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::tfhe::RteEtherAddrHash)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::tfhe::DchainRejuvenateIndex)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::tfhe::VectorBorrow)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::tfhe::VectorReturn)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::tfhe::DchainAllocateNewIndex)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::tfhe::MapPut)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::tfhe::PacketGetUnreadLength)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::tfhe::SetIpv4UdpTcpChecksum)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::tfhe::DchainIsIndexAllocated)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::tfhe::SketchComputeHashes)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::tfhe::SketchExpire)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::tfhe::SketchFetch)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::tfhe::SketchRefresh)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::tfhe::SketchTouchBuckets)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::tfhe::MapErase)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::tfhe::DchainFreeIndex)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::tfhe::LoadBalancedFlowHash)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::tfhe::ChtFindBackend)
DEFAULT_VISIT_PRINT_MODULE_NAME(targets::tfhe::HashObj)

} // namespace synapse