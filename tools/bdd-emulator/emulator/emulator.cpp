#include "emulator.h"
#include "pcap.h"

namespace BDD {
namespace emulation {

void Emulator::list_operations() const {
  std::cerr << "Known operations:\n";
  for (auto it = operations.begin(); it != operations.end(); it++) {
    std::cerr << "  " << it->first << "\n";
  }
}

void Emulator::dump_context(const context_t &ctx) const {
  std::cerr << "========================================\n";
  std::cerr << "Context:\n";
  for (auto c : ctx) {
    std::cerr << "  [*] " << kutil::expr_to_string(c, true) << "\n";
  }
  std::cerr << "========================================\n";
}

void remember_device(const BDD &bdd, uint16_t device, context_t &ctx) {
  auto device_symbol = bdd.get_symbol(symbex::PORT);
  concretize(ctx, device_symbol, device);
}

void remember_pkt_len(const BDD &bdd, uint32_t len, context_t &ctx) {
  auto length_symbol = bdd.get_symbol(symbex::PACKET_LENGTH);
  concretize(ctx, length_symbol, len);
}

bool Emulator::evaluate_condition(klee::ref<klee::Expr> condition,
                                  context_t &ctx) const {
  auto always_true = kutil::solver_toolbox.is_expr_always_true(ctx, condition);

  assert(((kutil::solver_toolbox.is_expr_always_true(ctx, condition)) ^
          (kutil::solver_toolbox.is_expr_always_false(ctx, condition))) &&
         "Can't be sure...");

  return always_true;
}

void Emulator::run(pkt_t pkt, time_ns_t time, uint16_t device) {
  context_t ctx;
  auto next_node = bdd.get_process();

  remember_device(bdd, device, ctx);
  remember_pkt_len(bdd, pkt.size, ctx);

  while (next_node) {
    auto node = next_node;
    next_node = nullptr;

    auto id = node->get_id();
    meta.hit_counter[id]++;

    switch (node->get_type()) {
    case Node::CALL: {
      auto call_node = cast_node<Call>(node);
      assert(call_node);

      auto call = call_node->get_call();
      auto operation = get_operation(call.function_name);

      operation(bdd, call_node, pkt, time, state, meta, ctx, cfg);

      next_node = node->get_next();
    } break;
    case Node::BRANCH: {
      auto branch_node = cast_node<Branch>(node);
      assert(branch_node);

      auto condition = branch_node->get_condition();
      auto result = evaluate_condition(condition, ctx);

      if (result) {
        next_node = branch_node->get_on_true();
      } else {
        next_node = branch_node->get_on_false();
      }
    } break;
    case Node::RETURN_PROCESS: {
      auto ret_node = cast_node<ReturnProcess>(node);
      assert(ret_node);

      auto op = ret_node->get_return_operation();

      if (op == ReturnProcess::Operation::DROP) {
        meta.rejected++;
      } else {
        meta.accepted++;
      }
    } break;
    default: {
      assert(false && "Should not be here.");
      std::cerr << "Error: run in debug mode.\n";
      exit(1);
    }
    }
  }
}

uint64_t get_number_of_packets_from_pcap(pcap_t *pcap) {
  auto fpcap = pcap_file(pcap);
  assert(fpcap);
  auto pcap_start_pos = ftell(fpcap);

  uint64_t counter = 0;
  int status = 1;
  pcap_pkthdr *header;
  const u_char *data;

  while ((status = pcap_next_ex(pcap, &header, &data)) >= 0) {
    counter++;
  }

  // rewind pcap
  fseek(fpcap, pcap_start_pos, SEEK_SET);

  return counter;
}

void Emulator::run(const std::string &pcap_filename, uint16_t device) {
  char errbuff[PCAP_ERRBUF_SIZE];
  auto pcap = pcap_open_offline(pcap_filename.c_str(), errbuff);

  if (pcap == nullptr) {
    std::cerr << "Error opening pcap file " << pcap_filename << ": " << errbuff
              << "\n";
    exit(1);
  }

  auto num_packets = get_number_of_packets_from_pcap(pcap);

  auto fpcap = pcap_file(pcap);
  assert(fpcap);
  auto pcap_start_pos = ftell(fpcap);

  pcap_pkthdr *header;
  const u_char *data;
  int status = 1;
  time_ns_t time = 0;

  auto loops = cfg.loops;
  auto warmup_mode = cfg.warmup;

  if (cfg.report) {
    auto total_num_packets = loops * num_packets;

    if (warmup_mode)
      total_num_packets += num_packets;

    reporter.set_num_packets(total_num_packets);
  }

  while (true) {
    if (!warmup_mode && cfg.loops > 0) {
      loops--;

      if (loops < 0) {
        break;
      }
    }

    fseek(fpcap, pcap_start_pos, SEEK_SET);

    while ((status = pcap_next_ex(pcap, &header, &data)) >= 0) {
      auto _data = static_cast<const uint8_t *>(data);
      auto _size = static_cast<uint32_t>(header->len);

      pkt_t pkt(_data, _size);

      if (cfg.rate.first) {
        // To obtain the time in seconds:
        // (pkt.size * 8) / (gbps * 1e9)
        // We want in ns, not seconds, so we multiply by 1e9.
        // This cancels with the 1e9 on the bottom side of the operation.
        // So actually, result in ns = (pkt.size * 8) / gbps
        // Also, don't forget to take the inter packet gap, preamble, and CRC
        // into consideration.
        auto CRC = 4;
        auto IPG = 20;
        auto dt = (time_ns_t)(((pkt.size + IPG + CRC) * 8) / cfg.rate.second);
        time += dt;
        meta.elapsed += dt;

        if (meta.packet_counter == 0) {
          reporter.set_virtual_time_start(time);
        }
      } else {
        auto last_time = time;
        time = header->ts.tv_sec * 1e9 + header->ts.tv_usec * 1e6;

        if (meta.packet_counter == 0 || last_time > time) {
          reporter.set_virtual_time_start(time);
        }

        auto dt = time - last_time;
        meta.elapsed += dt;
      }

      run(pkt, time, device);

      meta.packet_counter++;

      if (cfg.report) {
        reporter.inc_packet_counter();
        reporter.set_time(time);
        reporter.show();
      }
    }

    // The first iteration is the warmup iteration.
    if (warmup_mode) {
      warmup_mode = false;
      meta.reset();

      if (cfg.report)
        reporter.stop_warmup();
    }
  }

  if (cfg.report) {
    reporter.set_time(time);
    reporter.show(true);
  }
}

operation_ptr Emulator::get_operation(const std::string &name) const {
  auto operation_it = operations.find(name);

  if (operation_it == operations.end()) {
    std::cerr << "Unknown operation \"" << name << "\"\n";
    assert(false && "Unknown operation");
    exit(1);
  }

  return *operation_it->second;
}

void Emulator::setup() {
  auto node = bdd.get_init();

  while (node) {
    if (node->get_type() == Node::CALL) {
      auto call_node = cast_node<Call>(node);
      assert(call_node);

      auto call = call_node->get_call();
      auto operation = get_operation(call.function_name);

      pkt_t mock_pkt;
      context_t empty_ctx;

      operation(bdd, call_node, mock_pkt, 0, state, meta, empty_ctx, cfg);

      node = node->get_next();
    } else if (node->get_type() == Node::BRANCH) {
      auto branch_node = cast_node<Branch>(node);
      assert(branch_node);
      node = branch_node->get_on_true();
    } else {
      break;
    }
  }
}

} // namespace emulation
} // namespace BDD