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
    function_call(node->get_target(), node->get_name());                       \
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

Graphviz::Graphviz(const std::string &path, const SearchSpace *_search_space)
    : fpath(path), search_space(_search_space) {
  node_colors = std::map<TargetType, std::string>{
      {TargetType::BMv2, "darkolivegreen2"},
      {TargetType::Tofino, "cornflowerblue"},
      {TargetType::Netronome, "gold"},
      {TargetType::FPGA, "coral1"},
      {TargetType::x86_BMv2, "darkorange2"},
      {TargetType::x86_Tofino, "firebrick2"},
  };

  ofs.open(fpath);
  assert(ofs);
}

Graphviz::Graphviz(const std::string &path) : Graphviz(path, nullptr) {}

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

  if (search_space) {
    cmd += " " + search_space_fpath;
  }

  system(cmd.c_str());
}

void Graphviz::function_call(TargetType target, std::string label) {
  assert(node_colors.find(target) != node_colors.end());
  ofs << "[label=\"" << label << "\", ";
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

void Graphviz::dump_search_space() const {
  assert(search_space);
  assert(search_space->get_root());

  std::ofstream search_space_ofs;

  search_space_ofs.open(search_space_fpath);

  search_space_ofs << "digraph SearchSpace {\n";
  search_space_ofs << "layout=\"twopi\";";
  // search_space_ofs << "ratio=\"fill\";\n";
  // search_space_ofs << "size=\"12,12!\";\n";
  // search_space_ofs << "margin=0;\n";
  search_space_ofs << "node [shape=ellipse,style=filled];\n";

  std::vector<search_space_node_t *> nodes;
  nodes.push_back(search_space->get_root().get());

  while (nodes.size()) {
    auto node = nodes[0];
    nodes.erase(nodes.begin());

    search_space_ofs << node->execution_plan_id;
    // search_space_ofs << " [color=\"#";
    // auto color = get_color(node->score);
    // search_space_ofs << std::setw(2) << std::setfill('0') << std::hex;
    // search_space_ofs << color.r;
    // search_space_ofs << std::setw(2) << std::setfill('0') << std::hex;
    // search_space_ofs << color.g;
    // search_space_ofs << std::setw(2) << std::setfill('0') << std::hex;
    // search_space_ofs << color.b;
    // search_space_ofs << std::dec;
    // search_space_ofs << "\"";

    search_space_ofs << " [label=\"";
    search_space_ofs << node->score;
    search_space_ofs << "\"";

    if (node->m) {
      assert(node->m->get_node());
      search_space_ofs << ", tooltip=\""
                       << get_bdd_node_name(node->m->get_node().get()) << " -> "
                       << node->m->get_target_name()
                       << "::" << node->m->get_name() << "\"";
      // search_space_ofs << ", label=\"" << node->m->get_target_name()
      //                  << "::" << node->m->get_name() << "\"";
    }
    search_space_ofs << "];\n";

    if (node->prev) {
      search_space_ofs << node->prev->execution_plan_id << " -> "
                       << node->execution_plan_id << ";\n";
    }

    for (auto leaf : node->space) {
      nodes.push_back(leaf.get());
    }
  }

  search_space_ofs << "}\n";

  search_space_ofs.close();
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

void Graphviz::visualize(const ExecutionPlan &ep, SearchSpace &_search_space,
                         bool interrupt) {
  if (!ep.get_root()) {
    return;
  }

  Graphviz gv(&_search_space);
  ep.visit(gv);
  gv.open();

  if (interrupt) {
    std::cout << "\nPress Enter to continue ";
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
  }
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

  if (search_space) {
    dump_search_space();
  }
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

  function_call(target, label);
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

void Graphviz::visit(const targets::tofino::TableLookup *node) {
  std::stringstream label_builder;

  auto target = node->get_target();
  auto name = node->get_name();

  auto table_name = node->get_table_name();
  auto nodes = node->get_nodes();
  auto objs = node->get_objs();
  auto keys = node->get_keys();
  auto params = node->get_params();
  auto contains = node->get_contains_symbols();

  label_builder << name << "\n";

  label_builder << "  table: " << table_name << "\n";

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
    for (auto meta : key.meta) {
      label_builder << meta.symbol << ",";
    }
    label_builder << "]";
  }
  label_builder << "]\n";

  label_builder << "  params (" << params.size() << "): [";
  for (auto param : params) {
    label_builder << "\n";
    label_builder << "    ";
    label_builder << "[";
    label_builder << "objs:[";
    for (auto obj : param.objs) {
      label_builder << obj << ",";
    }
    label_builder << "]";
    label_builder << "exprs:[";
    for (auto expr : param.exprs) {
      label_builder << kutil::expr_to_string(expr, true) << ",";
    }
    label_builder << "]";
    label_builder << "]";
  }
  label_builder << "]\n";

  label_builder << "  hit: [";
  for (auto c : contains) {
    label_builder << c.label << ",";
  }
  label_builder << "]\n";

  auto label = label_builder.str();
  find_and_replace(label, {{"\n", "\\l"}});

  function_call(target, label);
}

void Graphviz::visit(const targets::tofino::TableLookupSimple *node) {
  auto simple_table = static_cast<const targets::tofino::TableLookup *>(node);
  visit(simple_table);
}

DEFAULT_VISIT_PRINT_MODULE_NAME(targets::tofino::RegisterRead)
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