#include "reporter.h"

#include "../visualizers/hit-rate.h"

namespace BDD {
namespace emulation {

void Reporter::render_hit_rate_dot(bool interrupt) const {
  HitRateGraphvizGenerator::visualize(bdd, meta.get_hit_rate(), interrupt);
}

void Reporter::dump_hit_rate_dot(std::ostream &os) const {
  auto hit_rate = meta.get_hit_rate();
  HitRateGraphvizGenerator dot_generator(os, hit_rate);
  dot_generator.visit(bdd);
}

std::string get_human_readable_time_duration(std::chrono::nanoseconds elapsed) {
  std::stringstream ss;

  auto fill = ss.fill();
  ss.fill('0');

  auto h = std::chrono::duration_cast<std::chrono::hours>(elapsed);
  elapsed -= h;

  auto m = std::chrono::duration_cast<std::chrono::minutes>(elapsed);
  elapsed -= m;

  auto s = std::chrono::duration_cast<std::chrono::seconds>(elapsed);
  elapsed -= s;

  auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed);
  elapsed -= ns;

  if (h.count() > 0)
    ss << std::setw(2) << h.count() << "h";

  if (m.count() > 0)
    ss << std::setw(2) << m.count() << "m";

  ss << std::setw(2) << s.count();
  ss << "." << std::setw(9) << ns.count();
  ss << "s";

  ss.fill(fill);
  return ss.str();
}

std::string Reporter::get_elapsed() {
  auto now = steady_clock::now();
  auto elapsed = now - real_time_start;
  return get_human_readable_time_duration(elapsed);
}

std::string Reporter::get_elapsed(time_ns_t _time) {
  assert(_time >= virtual_time_start && "Time can't be negative");
  auto elapsed = std::chrono::nanoseconds(_time - virtual_time_start);
  return get_human_readable_time_duration(elapsed);
}

void Reporter::set_thousands_separator() {
  struct my_numpunct : std::numpunct<char> {
  protected:
    virtual char do_thousands_sep() const override { return ','; }
    virtual std::string do_grouping() const override { return "\03"; }
  };

  auto my_locale = std::locale(std::cout.getloc(), new my_numpunct);
  std::cout.imbue(my_locale);
}

void Reporter::show(bool force_update) {
  if (packet_counter < next_report && !force_update) {
    return;
  }

  auto progress = (int)((100.0 * packet_counter) / num_packets);
  auto churn_fpm = (60.0 * meta.flows_expired) / (double)(meta.elapsed * 1e-9);
  auto percent_accepted = 100.0 * meta.accepted / meta.packet_counter;
  auto percent_rejected = 100.0 * meta.rejected / meta.packet_counter;

  if (next_report > 1) {
    for (auto i = 0; i < 14; i++)
      std::cout << VT100_UP_1_LINE;
  }

  std::cout << VT100_ERASE;
  std::cout << "Emulator data\n";

  std::cout << VT100_ERASE;
  std::cout << "  Real time     " << get_elapsed() << "\n";
  std::cout << VT100_ERASE;
  std::cout << "  Virtual Time  " << get_elapsed(time) << "\n";
  std::cout << VT100_ERASE;
  std::cout << "  Progress      " << progress << " %\n";
  std::cout << VT100_ERASE;
  std::cout << "  Packets       " << packet_counter << "\n";
  std::cout << VT100_ERASE;
  std::cout << "  Total packets " << num_packets << "\n";
  std::cout << VT100_ERASE;
  std::cout << "  Warmup        " << warmup_mode << "\n";

  std::cout << VT100_ERASE;
  std::cout << "Metadata\n";
  std::cout << VT100_ERASE;
  std::cout << "  Packets       " << meta.packet_counter << "\n";
  std::cout << VT100_ERASE;
  std::cout << "  Accepted      " << meta.accepted;
  std::cout << " (" << percent_accepted << " %)\n";
  std::cout << VT100_ERASE;
  std::cout << "  Rejected      " << meta.rejected;
  std::cout << " (" << percent_rejected << "%)\n";
  std::cout << VT100_ERASE;
  std::cout << "  Elapsed       " << get_elapsed(meta.elapsed) << "\n";
  std::cout << VT100_ERASE;
  std::cout << "  Expired       " << meta.flows_expired;
  std::cout << " (" << churn_fpm << " fpm)\n";
  std::cout << VT100_ERASE;
  std::cout << "  Dchain allocs " << meta.dchain_allocations << "\n";

  fflush(stdout);

  if (!force_update) {
    next_report += REPORT_PACKET_PERIOD;
  }
}

void Reporter::dump_hit_rate_csv(std::ostream &os) const {
  auto hit_rate = meta.get_hit_rate();

  os << "# node,hit rate";
  for (auto it = hit_rate.begin(); it != hit_rate.end(); it++) {
    os << "\n" << it->first << "," << it->second;
  }
}

} // namespace emulation
} // namespace BDD