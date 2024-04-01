#pragma once

#include <assert.h>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <math.h>
#include <unistd.h>

#include "call-paths-to-bdd.h"

namespace BDD {

struct color_t {
  uint8_t r;
  uint8_t g;
  uint8_t b;
  uint8_t o;

  color_t(uint8_t _r, uint8_t _g, uint8_t _b) : r(_r), g(_g), b(_b), o(0xff) {}

  color_t(uint8_t _r, uint8_t _g, uint8_t _b, uint8_t _o)
      : r(_r), g(_g), b(_b), o(_o) {}

  std::string to_gv_repr() const {
    std::stringstream ss;

    ss << "#";
    ss << std::hex;

    ss << std::setw(2);
    ss << std::setfill('0');
    ss << (int)r;

    ss << std::setw(2);
    ss << std::setfill('0');
    ss << (int)g;

    ss << std::setw(2);
    ss << std::setfill('0');
    ss << (int)b;

    ss << std::setw(2);
    ss << std::setfill('0');
    ss << (int)o;

    ss << std::dec;

    return ss.str();
  }
};

struct bdd_visualizer_opts_t {
  bool process_only;
  std::unordered_map<node_id_t, std::string> colors_per_node;
  std::pair<bool, std::string> default_color;
  std::unordered_map<node_id_t, std::string> annotations_per_node;

  struct processed_t {
    std::unordered_set<node_id_t> nodes;
    const Node *next;

    processed_t() : next(nullptr) {}
  } processed;

  bdd_visualizer_opts_t() : process_only(true) {}
};

class GraphvizGenerator : public BDDVisitor {
private:
  std::ostream &os;
  bdd_visualizer_opts_t opts;

  const char *COLOR_PROCESSED = "gray";
  const char *COLOR_NEXT = "cyan";

  const char *COLOR_CALL = "cornflowerblue";
  const char *COLOR_BRANCH = "yellow";
  const char *COLOR_RETURN_INIT_SUCCESS = "chartreuse2";
  const char *COLOR_RETURN_INIT_FAILURE = "brown1";
  const char *COLOR_RETURN_PROCESS_FORWARD = "chartreuse2";
  const char *COLOR_RETURN_PROCESS_DROP = "brown1";
  const char *COLOR_RETURN_PROCESS_BCAST = "purple";

public:
  GraphvizGenerator(std::ostream &_os, const bdd_visualizer_opts_t &_opts)
      : os(_os), opts(_opts) {}

  GraphvizGenerator(std::ostream &_os) : os(_os) {}

  void set_opts(const bdd_visualizer_opts_t &_opts) { opts = _opts; }

  bool has_color(node_id_t id) const {
    return opts.colors_per_node.find(id) != opts.colors_per_node.end();
  }

  std::string get_color(const Node *node) const {
    assert(node);
    auto id = node->get_id();

    if (has_color(id)) {
      return opts.colors_per_node.at(id);
    }

    if (opts.default_color.first) {
      return opts.default_color.second;
    }

    if (opts.processed.nodes.find(id) != opts.processed.nodes.end()) {
      return COLOR_PROCESSED;
    }

    if (opts.processed.next && id == opts.processed.next->get_id()) {
      return COLOR_NEXT;
    }

    switch (node->get_type()) {
    case Node::NodeType::CALL:
      return COLOR_CALL;
    case Node::NodeType::BRANCH:
      return COLOR_BRANCH;
    case Node::NodeType::RETURN_INIT: {
      auto return_init_node = static_cast<const ReturnInit *>(node);
      auto ret_value = return_init_node->get_return_value();

      switch (ret_value) {
      case ReturnInit::ReturnType::SUCCESS:
        return COLOR_RETURN_INIT_SUCCESS;
      case ReturnInit::ReturnType::FAILURE:
        return COLOR_RETURN_INIT_FAILURE;
      default:
        assert(false && "Not supposed to be here.");
        std::cerr << "Error: run in debug mode for more details.\n";
        exit(1);
      }
    }
    case Node::NodeType::RETURN_PROCESS: {
      auto return_init_node = static_cast<const ReturnProcess *>(node);
      auto ret_value = return_init_node->get_return_operation();

      switch (ret_value) {
      case ReturnProcess::Operation::FWD:
        return COLOR_RETURN_PROCESS_FORWARD;
      case ReturnProcess::Operation::DROP:
        return COLOR_RETURN_PROCESS_DROP;
      case ReturnProcess::Operation::BCAST:
        return COLOR_RETURN_PROCESS_BCAST;
      default:
        assert(false && "Not supposed to be here.");
        std::cerr << "Error: run in debug mode for more details.\n";
        exit(1);
      }
    }
    default:
      assert(false && "Not supposed to be here.");
      std::cerr << "Error: run in debug mode for more details.\n";
      exit(1);
    }
  }

  static void visualize(const BDD &bdd, bool interrupt = true,
                        bool process_only = true) {
    bdd_visualizer_opts_t opts;
    visualize(bdd, opts);
  }

  static void visualize(const BDD &bdd, const bdd_visualizer_opts_t &opts,
                        bool interrupt = true) {
    auto random_fname_generator = []() {
      constexpr int fname_len = 15;
      constexpr const char *prefix = "/tmp/";
      constexpr const char *alphanum = "0123456789"
                                       "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                       "abcdefghijklmnopqrstuvwxyz";

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
    };

    auto open_graph = [](const std::string &fpath) {
      std::string file_path = __FILE__;
      std::string dir_path = file_path.substr(0, file_path.rfind("/"));
      std::string script = "open_graph.sh";
      std::string cmd = dir_path + "/" + script + " " + fpath;

      auto status = system(cmd.c_str());

      if (status < 0) {
        std::cout << "Error: " << strerror(errno) << '\n';
        assert(false && "Failed to open graph.");
      }
    };

    auto random_fname = random_fname_generator();
    auto file = std::ofstream(random_fname);
    assert(file.is_open());

    GraphvizGenerator gv(file, opts);
    gv.visit(bdd);

    file.close();

    std::cerr << "Visualizing BDD";
    std::cerr << " id=" << bdd.get_id();
    std::cerr << " hash=" << bdd.hash();
    std::cerr << " file=" << random_fname;
    std::cerr << "\n";

    open_graph(random_fname);

    if (interrupt) {
      std::cout << "Press Enter to continue ";
      std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    }
  }

  void visit(const BDD &bdd) override {
    os << "digraph mygraph {\n";
    os << "\tnode [shape=box style=rounded border=0];\n";

    if (!opts.process_only) {
      assert(bdd.get_init());
      visitInitRoot(bdd.get_init().get());
    }

    assert(bdd.get_process());
    visitProcessRoot(bdd.get_process().get());

    os << "}";
  }

  Action visitBranch(const Branch *node) override {
    if (node->get_next()) {
      assert(node->get_on_true()->get_prev());
      assert(node->get_on_true()->get_prev()->get_id() == node->get_id());

      assert(node->get_on_false()->get_prev());
      assert(node->get_on_false()->get_prev()->get_id() == node->get_id());
    }

    auto condition = node->get_condition();

    assert(node->get_on_true());
    node->get_on_true()->visit(*this);

    assert(node->get_on_false());
    node->get_on_false()->visit(*this);

    os << "\t\t" << get_gv_name(node);
    os << " [shape=Mdiamond, label=\"";

    os << node->get_id() << ":";
    os << kutil::pretty_print_expr(condition);

    auto constraints = node->get_node_constraints();
    if (constraints.size()) {
      for (auto c : constraints) {
        os << "\\n{" << kutil::pretty_print_expr(c) << "}";
      }
    }

    if (opts.annotations_per_node.find(node->get_id()) !=
        opts.annotations_per_node.end()) {
      os << "\\n" << opts.annotations_per_node.at(node->get_id());
    }

    os << "\"";

    os << ", fillcolor=\"" << get_color(node) << "\"";
    os << "];\n";

    os << "\t\t" << get_gv_name(node);
    os << " -> ";
    os << get_gv_name(node->get_on_true().get());
    os << " [label=\"True\"];\n";

    os << "\t\t" << get_gv_name(node);
    os << " -> ";
    os << get_gv_name(node->get_on_false().get());
    os << " [label=\"False\"];\n";

    return STOP;
  }

  Action visitCall(const Call *node) override {
    if (node->get_next()) {
      if (!node->get_next()->get_prev()) {
        std::cerr << "ERROR IN " << node->dump(true) << "\n";
        std::cerr << " => " << node->get_next()->dump(true) << "\n";
      }
      assert(node->get_next()->get_prev());
      assert(node->get_next()->get_prev()->get_id() == node->get_id());
    }
    auto call = node->get_call();

    assert(node->get_next());
    node->get_next()->visit(*this);

    os << "\t\t" << get_gv_name(node);
    os << " [label=\"";
    auto i = 0u;

    os << node->get_id() << ":";
    os << call.function_name;
    os << "(";

    i = 0;
    for (const auto &pair : call.args) {
      if (call.args.size() > 1) {
        os << "\\l";
        os << std::string(2, ' ');
      }

      os << pair.first << ":";
      arg_t arg = pair.second;

      if (arg.fn_ptr_name.first) {
        os << arg.fn_ptr_name.second;
      } else {
        os << kutil::pretty_print_expr(arg.expr);

        if (!arg.in.isNull() || !arg.out.isNull()) {
          os << "[";

          if (!arg.in.isNull()) {
            os << kutil::pretty_print_expr(arg.in);
          }

          if (!arg.out.isNull() &&
              (arg.in.isNull() || !kutil::solver_toolbox.are_exprs_always_equal(
                                      arg.in, arg.out))) {
            os << " -> ";
            os << kutil::pretty_print_expr(arg.out);
          }

          os << "]";
        }
      }

      if (i != call.args.size() - 1) {
        os << ",";
      }

      // os << kutil::pretty_print_expr(arg.expr);

      i++;
    }

    os << ")";

    if (!call.ret.isNull()) {
      os << " -> " << kutil::pretty_print_expr(call.ret);
    }

    auto symbols = node->get_local_generated_symbols();
    if (symbols.size()) {
      for (auto s : symbols) {
        os << "\\l=>{" << s.label;

        if (!s.expr.isNull()) {
          os << "[" << kutil::pretty_print_expr(s.expr) << "]";
        }

        os << "}";
      }
    }

    auto constraints = node->get_node_constraints();
    if (constraints.size()) {
      for (auto c : constraints) {
        os << "\\l{" << kutil::pretty_print_expr(c) << "}";
      }
    }

    if (opts.annotations_per_node.find(node->get_id()) !=
        opts.annotations_per_node.end()) {
      os << "\\l" << opts.annotations_per_node.at(node->get_id());
    }

    os << "\"";

    os << ", fillcolor=\"" << get_color(node) << "\"";
    os << "];\n";

    os << "\t\t" << get_gv_name(node);
    os << " -> ";
    os << get_gv_name(node->get_next().get());
    os << ";\n";

    return STOP;
  }

  Action visitReturnInit(const ReturnInit *node) override {
    auto value = node->get_return_value();

    os << "\t\t" << get_gv_name(node);
    os << " [label=\"";
    os << node->get_id() << ":";

    switch (value) {
    case ReturnInit::ReturnType::SUCCESS: {
      os << "OK";

      auto constraints = node->get_node_constraints();
      if (constraints.size()) {
        for (auto c : constraints) {
          os << "\\l{" << kutil::pretty_print_expr(c) << "}";
        }
      }

      break;
    }
    case ReturnInit::ReturnType::FAILURE: {
      os << "ABORT";
      break;
    }
    default: {
      assert(false);
    }
    }

    if (opts.annotations_per_node.find(node->get_id()) !=
        opts.annotations_per_node.end()) {
      os << "\\l" << opts.annotations_per_node.at(node->get_id());
    }

    os << "\"";
    os << ", fillcolor=\"" << get_color(node) << "\"";
    os << "];\n";

    return STOP;
  }

  Action visitReturnProcess(const ReturnProcess *node) override {
    auto value = node->get_return_value();
    auto operation = node->get_return_operation();

    os << "\t\t" << get_gv_name(node);
    os << " [label=\"";
    os << node->get_id() << ":";
    switch (operation) {
    case ReturnProcess::Operation::FWD: {
      os << "fwd(" << value << ")";

      auto constraints = node->get_node_constraints();
      if (constraints.size()) {
        for (auto c : constraints) {
          if (c.isNull()) {
            std::cerr << "NO C!\n";
            exit(1);
          }
          os << "\\l{" << kutil::pretty_print_expr(c) << "}";
        }
      }
      break;
    }
    case ReturnProcess::Operation::DROP: {
      os << "drop()";
      break;
    }
    case ReturnProcess::Operation::BCAST: {
      os << "bcast()";
      break;
    }
    default: {
      assert(false);
    }
    }

    if (opts.annotations_per_node.find(node->get_id()) !=
        opts.annotations_per_node.end()) {
      os << "\\l" << opts.annotations_per_node.at(node->get_id());
    }

    os << "\"";
    os << ", fillcolor=\"" << get_color(node) << "\"";
    os << "];\n";
    return STOP;
  }

  void visitInitRoot(const Node *root) override {
    os << "\tsubgraph clusterinit {\n";
    os << "\t\tlabel=\"nf_init\";\n";
    os << "\t\tnode [style=\"rounded,filled,bold\",color=black];\n";

    root->visit(*this);

    os << "\t}\n";
  }

  void visitProcessRoot(const Node *root) override {
    os << "\tsubgraph clusterprocess {\n";
    if (!opts.process_only) {
      os << "\t\tlabel=\"nf_process\"\n";
    }
    os << "\t\tnode [style=\"rounded,filled\",color=black];\n";

    root->visit(*this);

    os << "\t}\n";
  }

private:
  std::string get_gv_name(const Node *node) const {
    assert(node);

    std::stringstream stream;

    if (node->get_type() == Node::NodeType::RETURN_INIT) {
      const ReturnInit *ret = static_cast<const ReturnInit *>(node);

      stream << "\"return ";
      switch (ret->get_return_value()) {
      case ReturnInit::ReturnType::SUCCESS: {
        stream << "1";
        break;
      }
      case ReturnInit::ReturnType::FAILURE: {
        stream << "0";
        break;
      }
      default: {
        assert(false);
      }
      }
      stream << "\"";

      return stream.str();
    }

    stream << node->get_id();
    return stream.str();
  }
};
} // namespace BDD
