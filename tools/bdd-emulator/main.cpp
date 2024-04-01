#include "llvm/Support/CommandLine.h"

#include <fstream>

#include "bdd-emulator.h"

namespace {
llvm::cl::OptionCategory BDDEmulator("BDDEmulator specific options");

llvm::cl::opt<std::string> InputBDDFile("in", llvm::cl::desc("BDD."),
                                        llvm::cl::Required,
                                        llvm::cl::cat(BDDEmulator));

llvm::cl::opt<std::string> InputPcap("pcap", llvm::cl::desc("Pcap file."),
                                     llvm::cl::Required,
                                     llvm::cl::cat(BDDEmulator));

llvm::cl::opt<int> InputDevice("device",
                               llvm::cl::desc("Device that receives packets."),
                               llvm::cl::Required, llvm::cl::cat(BDDEmulator));

llvm::cl::opt<float> Rate("rate", llvm::cl::desc("Rate (Gbps)"),
                          llvm::cl::Optional, llvm::cl::init(0),
                          llvm::cl::cat(BDDEmulator));

llvm::cl::opt<int>
    Loops("loops", llvm::cl::desc("Number of loops (0 to never stop looping)"),
          llvm::cl::Optional, llvm::cl::init(1), llvm::cl::cat(BDDEmulator));

llvm::cl::opt<int> Expiration("expiration",
                              llvm::cl::desc("Expiration time (us)"),
                              llvm::cl::init(0), llvm::cl::Optional,
                              llvm::cl::cat(BDDEmulator));

llvm::cl::opt<bool>
    Warmup("warmup",
           llvm::cl::desc("Loop the pcap first to warmup state, and then do "
                          "another pass to retrieve metadata."),
           llvm::cl::ValueDisallowed, llvm::cl::init(false),
           llvm::cl::cat(BDDEmulator));
} // namespace

int main(int argc, char **argv) {
  llvm::cl::ParseCommandLineOptions(argc, argv);

  auto bdd = BDD::BDD(InputBDDFile);
  auto cfg = BDD::emulation::cfg_t();

  if (Rate > 0) {
    cfg.rate.first = true;
    cfg.rate.second = Rate;
  }

  if (Expiration > 0) {
    cfg.timeout.first = true;
    cfg.timeout.second = Expiration;
  }

  cfg.loops = Loops;
  cfg.warmup = Warmup;
  cfg.report = true;

  BDD::emulation::Emulator emulator(bdd, cfg);
  emulator.run(InputPcap, InputDevice);

  return 0;
}
