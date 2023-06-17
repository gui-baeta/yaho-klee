#include "internals/internals.h"

#include <assert.h>
#include <chrono>
#include <iomanip>
#include <locale>
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

public:
  Reporter(const BDD &_bdd, const meta_t &_meta, bool _warmup_mode)
      : bdd(_bdd), meta(_meta), num_packets(0), warmup_mode(_warmup_mode),
        packet_counter(0), time(0), next_report(0),
        real_time_start(steady_clock::now()) {
    struct termios term;
    tcgetattr(fileno(stdin), &term);

    term.c_lflag &= ~ECHO;
    tcsetattr(fileno(stdin), 0, &term);

    set_thousands_separator();
  }

  void set_num_packets(uint64_t _num_packets) {
    if (warmup_mode) {
      num_packets = 2 * _num_packets;
    } else {
      num_packets = _num_packets;
    }
  }

  void stop_warmup() { warmup_mode = false; }
  void inc_packet_counter() { packet_counter++; }
  void set_time(time_ns_t _time) { time = _time; }

  void render_hit_rate() const;
  void show(bool force_update = false);

private:
  void set_thousands_separator();
  std::string get_elapsed();
  std::string get_elapsed(time_ns_t _time);
};

} // namespace emulation
} // namespace BDD