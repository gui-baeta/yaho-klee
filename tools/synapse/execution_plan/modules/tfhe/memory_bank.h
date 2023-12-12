#pragma once

#include <klee/Expr.h>

#include <string>
#include <unordered_map>
#include <unordered_set>

#include "data_structures/data_structures.h"

#define HAS_CONFIG(T)                                                          \
  bool has_##T##_config(addr_t addr) const {                                   \
    return T##_configs.find(addr) != T##_configs.end();                        \
  }

#define SAVE_CONFIG(T)                                                         \
  void save_##T##_config(addr_t addr, BDD::symbex::T##_config_t cfg) {              \
    assert(!has_##T##_config(addr));                                           \
    T##_configs.insert({addr, cfg});                                           \
  }

#define GET_CONFIG(T)                                                          \
  const std::unordered_map<addr_t, BDD::symbex::T##_config_t>                       \
      &get_##T##_configs() {                                                   \
    return T##_configs;                                                        \
  }

namespace synapse {
namespace targets {
namespace tfhe {

class tfheMemoryBank : public TargetMemoryBank {
public:
private:
  std::unordered_map<addr_t, BDD::symbex::map_config_t> map_configs;
  std::unordered_map<addr_t, BDD::symbex::vector_config_t> vector_configs;
  std::unordered_map<addr_t, BDD::symbex::dchain_config_t> dchain_configs;
  std::unordered_map<addr_t, BDD::symbex::sketch_config_t> sketch_configs;
  std::unordered_map<addr_t, BDD::symbex::cht_config_t> cht_configs;

public:
  tfheMemoryBank() {}

  tfheMemoryBank(const tfheMemoryBank &mb)
      : map_configs(mb.map_configs), vector_configs(mb.vector_configs),
        dchain_configs(mb.dchain_configs), sketch_configs(mb.sketch_configs),
        cht_configs(mb.cht_configs) {}

  HAS_CONFIG(map)
  HAS_CONFIG(vector)
  HAS_CONFIG(dchain)
  HAS_CONFIG(sketch)
  HAS_CONFIG(cht)

  SAVE_CONFIG(map)
  SAVE_CONFIG(vector)
  SAVE_CONFIG(dchain)
  SAVE_CONFIG(sketch)
  SAVE_CONFIG(cht)

  GET_CONFIG(map)
  GET_CONFIG(vector)
  GET_CONFIG(dchain)
  GET_CONFIG(sketch)
  GET_CONFIG(cht)

  virtual TargetMemoryBank_ptr clone() const override {
    auto clone = new tfheMemoryBank(*this);
    return TargetMemoryBank_ptr(clone);
  }
};

} // namespace tfhe
} // namespace targets
} // namespace synapse