#pragma once

#include "execution_plan/execution_plan.h"
#include "execution_plan/visitors/synthesizers/synthesizers.h"

#include <sys/stat.h>
#include <vector>

using synapse::synthesizer::Synthesizer;
using synapse::synthesizer::bmv2::BMv2Generator;
using synapse::synthesizer::tofino::TofinoGenerator;
using synapse::synthesizer::x86::x86Generator;
using synapse::synthesizer::tfhe::tfheGenerator;
using synapse::synthesizer::x86_bmv2::x86BMv2Generator;
using synapse::synthesizer::x86_tofino::x86TofinoGenerator;

namespace synapse {

class CodeGenerator {
private:
  typedef ExecutionPlan (CodeGenerator::*ExecutionPlanTargetExtractor)(
      const ExecutionPlan &) const;
  typedef std::shared_ptr<Synthesizer> Synthesizer_ptr;

  struct target_helper_t {
    ExecutionPlanTargetExtractor extractor;
    Synthesizer_ptr generator;

    target_helper_t(ExecutionPlanTargetExtractor _extractor)
        : extractor(_extractor) {}

    target_helper_t(ExecutionPlanTargetExtractor _extractor,
                    Synthesizer_ptr _generator)
        : extractor(_extractor), generator(_generator) {}
  };

  public:
    std::vector<target_helper_t> target_helpers_loaded;
    std::map<TargetType, target_helper_t> target_helpers_bank;

private:
  ExecutionPlan x86_extractor(const ExecutionPlan &execution_plan) const;
  ExecutionPlan tfhe_extractor(const ExecutionPlan &execution_plan) const;
  ExecutionPlan x86_bmv2_extractor(const ExecutionPlan &execution_plan) const;
  ExecutionPlan x86_tofino_extractor(const ExecutionPlan &execution_plan) const;
  ExecutionPlan bmv2_extractor(const ExecutionPlan &execution_plan) const;
  ExecutionPlan fpga_extractor(const ExecutionPlan &execution_plan) const;
  ExecutionPlan tofino_extractor(const ExecutionPlan &execution_plan) const;
  ExecutionPlan netronome_extractor(const ExecutionPlan &execution_plan) const;

  std::string directory;

public:
  CodeGenerator(const std::string &_directory) : directory(_directory) {
    target_helpers_bank = {
        {TargetType::x86_BMv2,
         target_helper_t(&CodeGenerator::x86_bmv2_extractor,
                         std::make_shared<x86BMv2Generator>())},

        {TargetType::BMv2, target_helper_t(&CodeGenerator::bmv2_extractor,
                                           std::make_shared<BMv2Generator>())},

        {TargetType::FPGA, target_helper_t(&CodeGenerator::fpga_extractor)},

        {TargetType::x86_Tofino,
         target_helper_t(&CodeGenerator::x86_tofino_extractor,
                         std::make_shared<x86TofinoGenerator>())},

        {TargetType::Tofino,
         target_helper_t(&CodeGenerator::tofino_extractor,
                         std::make_shared<TofinoGenerator>())},

        {TargetType::Netronome,
         target_helper_t(&CodeGenerator::netronome_extractor)},

        {TargetType::x86, target_helper_t(&CodeGenerator::x86_extractor,
                                          std::make_shared<x86Generator>())},
        {TargetType::tfhe, target_helper_t(&CodeGenerator::tfhe_extractor,
                                          std::make_shared<tfheGenerator>())},
    };
  }

  void add_target(TargetType target) {
    auto found_it = target_helpers_bank.find(target);
    assert(found_it != target_helpers_bank.end() &&
           "TargetType not found in target_extractors_bank of CodeGenerator");
    assert(found_it->second.generator);

    if (!directory.size()) {
      target_helpers_loaded.push_back(found_it->second);
      return;
    }

    auto output_file = directory + "/";

    switch (target) {
    case TargetType::x86_BMv2:
      output_file += "bmv2-x86.c";
      break;
    case TargetType::BMv2:
      output_file += "bmv2.p4";
      break;
    case TargetType::FPGA:
      output_file += "fpga.v";
      break;
    case TargetType::x86_Tofino:
      output_file += "tofino-x86.cpp";
      break;
    case TargetType::Tofino:
      output_file += "tofino.p4";
      break;
    case TargetType::Netronome:
      output_file += "netronome.c";
      break;
    case TargetType::x86:
      output_file += "x86.c";
      break;
    case TargetType::tfhe:
      output_file += "tfhe.rs";
      break;
    }

    found_it->second.generator->output_to_file(output_file);
    target_helpers_loaded.push_back(found_it->second);
  }

  ExecutionPlan extract_at(const ExecutionPlan &execution_plan,
                        size_t target_ix) {
    auto target_helper = this->target_helpers_loaded[target_ix];
    auto &extractor = target_helper.extractor;

    return (this->*extractor)(execution_plan);
  }

  void init_generator_state(const ExecutionPlan &execution_plan) {
    for (auto helper : target_helpers_loaded) {
      auto &generator = helper.generator;

      // Cast Synthesizer to its child tfheGenerator
//      auto tfhe_generator = std::static_pointer_cast<synapse::synthesizer::tfhe::tfheGenerator>(generator);
//      tfhe_generator->init_state(extracted_ep);
      generator->init_state(execution_plan);
    }
  }

  void generate(const ExecutionPlan &execution_plan) {
    for (auto helper : target_helpers_loaded) {
      auto &extractor = helper.extractor;
      auto &generator = helper.generator;

      auto extracted_ep = (this->*extractor)(execution_plan);
      generator->generate(extracted_ep, execution_plan);
    }
  }
};

} // namespace synapse