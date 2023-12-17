#pragma once

#include <assert.h>
#include <fstream>
#include <iostream>
#include <stdio.h>
#include <streambuf>
#include <string>

#include "../../../log.h"
#include "../../execution_plan.h"
#include "../visitor.h"

#define CODE_BUILDER_MARKER_BEGIN "/*@{"
#define CODE_BUILDER_MARKER_END "}@*/"

#define GET_BOILERPLATE_PATH(fname)                                            \
  (std::string(__FILE__).substr(0, std::string(__FILE__).rfind("/")) + "/" +   \
   (fname))

namespace synapse {
namespace synthesizer {

class Synthesizer : public ExecutionPlanVisitor {
private:
  std::unique_ptr<std::ostream> os;
  std::string fpath;

protected:
  std::string code;
  const ExecutionPlan *original_ep;

public:
  Synthesizer(const std::string &boilerplate_fpath) : original_ep(nullptr) {
    os = std::unique_ptr<std::ostream>(new std::ostream(std::cerr.rdbuf()));

    auto boilerplate_stream = std::ifstream(boilerplate_fpath);

    if (boilerplate_stream.fail()) {
      Log::err() << "Unable to open boilerplate file " << boilerplate_fpath
                 << "\n";
      exit(1);
    }

    boilerplate_stream.seekg(0, std::ios::end);
    code.reserve(boilerplate_stream.tellg());
    boilerplate_stream.seekg(0, std::ios::beg);

    code.assign((std::istreambuf_iterator<char>(boilerplate_stream)),
                std::istreambuf_iterator<char>());
  }

  void output_to_file(const std::string &_fpath) {
    fpath = _fpath;
    os = std::unique_ptr<std::ostream>(new std::ofstream(fpath));
  }

  virtual void generate(ExecutionPlan &target_ep) = 0;

  void generate(ExecutionPlan &target_ep, const ExecutionPlan &_original_ep) {
    original_ep = &_original_ep;

    const auto &target_meta = target_ep.get_meta();

    if (target_meta.nodes == 0) {
      if (!fpath.size()) {
        return;
      }

      remove(fpath.c_str());
      return;
    }

    generate(target_ep);
    *os << code;
  }
  // FIXME Maybe error comes from here??
  virtual void init_state(ExecutionPlan ep);

protected:
  void fill_mark(std::string marker_label, const std::string &content) {
    auto marker =
        CODE_BUILDER_MARKER_BEGIN + marker_label + CODE_BUILDER_MARKER_END;

    auto delim = code.find(marker);
    assert(delim != std::string::npos);

    auto trimmed_content = content;

    while (trimmed_content.size() &&
           (trimmed_content[0] == ' ' || trimmed_content[0] == '\n')) {
      trimmed_content = trimmed_content.substr(1, trimmed_content.size() - 1);
    }

    while (trimmed_content.size() &&
           (trimmed_content[trimmed_content.size() - 1] == ' ' ||
            trimmed_content[trimmed_content.size() - 1] == '\n')) {
      trimmed_content = trimmed_content.substr(0, trimmed_content.size() - 1);
    }

    code.replace(delim, marker.size(), trimmed_content);
  }

  int get_indentation_level(std::string marker_label) {
    auto marker =
        CODE_BUILDER_MARKER_BEGIN + marker_label + CODE_BUILDER_MARKER_END;

    auto delim = code.find(marker);
    assert(delim != std::string::npos);

    int spaces = 0;
    while (delim > 0 && code[delim - 1] == ' ') {
      spaces++;
      delim--;
    }

    return spaces / 2;
  }
};

} // namespace synthesizer
} // namespace synapse
