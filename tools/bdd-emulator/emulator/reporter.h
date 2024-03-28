#include "internals/internals.h"

#include <assert.h>
#include <chrono>
#include <iomanip>
#include <locale>
#include <ostream>
#include <termios.h>

#define KEY_ESC 0x1b
#define KEY_DEL 0x7f
#define KEY_BELL 0x07

#define VT100_ERASE "\33[2K\r"
#define VT100_UP_1_LINE "\33[1A"
#define VT100_UP_2_LINES "\33[2A"
#define VT100_UP_3_LINES "\33[3A"

#define REPORT_PACKET_PERIOD 1000

using std::chrono::_V2::steady_clock;

namespace BDD {
namespace emulation {

class Reporter {
private:
  const BDD &bdd;
  const meta_t &meta;

  uint64_t num_packets;
  bool warmup_mode;
  uint64_t packet_counter;
  time_ns_t time;
  uint64_t next_report;

  steady_clock::time_point real_time_start;
  time_ns_t virtual_time_start;

public:
  Reporter(const BDD &_bdd, const meta_t &_meta, bool _warmup_mode)
      : bdd(_bdd), meta(_meta), num_packets(0), warmup_mode(_warmup_mode),
        packet_counter(0), time(0), next_report(0),
        real_time_start(steady_clock::now()), virtual_time_start(0) {
    struct termios term;
    tcgetattr(fileno(stdin), &term);

    term.c_lflag &= ~ECHO;
    tcsetattr(fileno(stdin), 0, &term);

    set_thousands_separator();
  }

  void dump_hit_rate_csv(std::ostream &os) const;
  void dump_hit_rate_dot(std::ostream &os) const;
  void render_hit_rate_dot(bool interrupt) const;

private:
  void set_num_packets(uint64_t _num_packets) { num_packets = _num_packets; }

  void stop_warmup() { warmup_mode = false; }
  void inc_packet_counter() { packet_counter++; }

  void set_time(time_ns_t _time) { time = _time; }
  void set_virtual_time_start(time_ns_t _time) { virtual_time_start = _time; }

  void show(bool force_update = false);

  void set_thousands_separator();
  std::string get_elapsed();
  std::string get_elapsed(time_ns_t _time);

  friend class Emulator;
};

} // namespace emulation
} // namespace BDD