#include "bdd-analyzer-report.h"

#include <fstream>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

void from_json(const json &j, config_t &config) {
  j.at("device").get_to(config.device);

  if (j.find("pcap") != j.end()) {
    j.at("pcap").get_to(config.pcap);
  } else {
    j.at("total_packets").get_to(config.total_packets);
    j.at("total_flows").get_to(config.total_flows);
    j.at("churn_fpm").get_to(config.churn_fpm);
    j.at("rate_pps").get_to(config.rate_pps);
    j.at("packet_sizes").get_to(config.packet_sizes);
    j.at("traffic_uniform").get_to(config.traffic_uniform);
    j.at("traffic_zipf").get_to(config.traffic_zipf);
    j.at("traffic_zipf_param").get_to(config.traffic_zipf_param);
  }
}

void from_json(const json &j, bdd_node_counters &counters) {
  for (const auto &kv : j.items()) {
    BDD::node_id_t node_id = std::stoull(kv.key());
    uint64_t count = kv.value();
    counters[node_id] = count;
  }
}

void from_json(const json &j, bdd_analyzer_report_t &report) {
  j.at("config").get_to(report.config);

  // Use our parser instead of the default one provided by the library. Their
  // one is not working for some reason.
  from_json(j["counters"], report.counters);

  j.at("elapsed").get_to(report.elapsed);
}

bdd_analyzer_report_t parse_bdd_analyzer_report_t(const std::string &filename) {
  std::ifstream file(filename);
  if (!file.is_open()) {
    assert(false && "failed to open file");
  }

  json j = json::parse(file);
  bdd_analyzer_report_t report = j.get<bdd_analyzer_report_t>();

  return report;
}