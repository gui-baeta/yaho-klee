#pragma once

#include <memory>
#include <vector>

namespace synapse {

class Module;
typedef std::shared_ptr<Module> Module_ptr;

class TargetMemoryBank;
typedef std::shared_ptr<TargetMemoryBank> TargetMemoryBank_ptr;

enum TargetType {
  x86,
  x86_BMv2,
  x86_Tofino,
  Tofino,
  BMv2,
};

std::ostream &operator<<(std::ostream &os, TargetType type);

struct Target {
  const TargetType type;
  const std::vector<Module_ptr> modules;
  const TargetMemoryBank_ptr memory_bank;

  Target(TargetType _type, const std::vector<Module_ptr> &_modules,
         const TargetMemoryBank_ptr &_memory_bank);
};

typedef std::shared_ptr<Target> Target_ptr;

} // namespace synapse