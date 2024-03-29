#define _GNU_SOURCE

#include <lib/unverified/sketch.h>
#include <lib/verified/cht.h>
#include <lib/verified/double-chain.h>
#include <lib/verified/map.h>
#include <lib/verified/vector.h>

#include <lib/verified/expirator.h>
#include <lib/verified/packet-io.h>
#include <lib/verified/tcpudp_hdr.h>
#include <lib/verified/vigor-time.h>

#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_udp.h>

#include <rte_cycles.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_lcore.h>
#include <rte_malloc.h>
#include <rte_mbuf.h>
#include <rte_random.h>

#include <getopt.h>
#include <pcap.h>
#include <stdbool.h>
#include <unistd.h>

#define NF_INFO(text, ...)                                                     \
  printf(text "\n", ##__VA_ARGS__);                                            \
  fflush(stdout);

#ifdef ENABLE_LOG
#define NF_DEBUG(text, ...)                                                    \
  fprintf(stderr, "DEBUG: " text "\n", ##__VA_ARGS__);                         \
  fflush(stderr);
#else // ENABLE_LOG
#define NF_DEBUG(...)
#endif // ENABLE_LOG

#define BATCH_SIZE 32

#define MBUF_CACHE_SIZE 256
#define MAX_NUM_DEVICES 32 // this is quite arbitrary...

#define IP_MIN_SIZE_WORDS 5
#define WORD_SIZE 4

#define FLOOD_FRAME ((uint16_t)-1)

static const uint16_t RX_QUEUE_SIZE = 1024;
static const uint16_t TX_QUEUE_SIZE = 1024;

static const unsigned MEMPOOL_BUFFER_COUNT = 2048;

uintmax_t nf_util_parse_int(const char *str, const char *name, int base,
                            char next) {
  char *temp;
  intmax_t result = strtoimax(str, &temp, base);

  // There's also a weird failure case with overflows, but let's not care
  if (temp == str || *temp != next) {
    rte_exit(EXIT_FAILURE, "Error while parsing '%s': %s\n", name, str);
  }

  return result;
}

bool nf_init(void);
int nf_process(uint16_t device, uint8_t *buffer, uint16_t packet_length,
               time_ns_t now);

// Send the given packet to all devices except the packet's own
void flood(struct rte_mbuf *packet, uint16_t nb_devices, uint16_t queue_id) {
  rte_mbuf_refcnt_set(packet, nb_devices - 1);
  int total_sent = 0;
  uint16_t skip_device = packet->port;
  for (uint16_t device = 0; device < nb_devices; device++) {
    if (device != skip_device) {
      total_sent += rte_eth_tx_burst(device, queue_id, &packet, 1);
    }
  }
  // should not happen, but in case we couldn't transmit, ensure the packet is
  // freed
  if (total_sent != nb_devices - 1) {
    rte_mbuf_refcnt_set(packet, 1);
    rte_pktmbuf_free(packet);
  }
}

// Initializes the given device using the given memory pool
static int nf_init_device(uint16_t device, struct rte_mempool *mbuf_pool) {
  int retval;

  // device_conf passed to rte_eth_dev_configure cannot be NULL
  struct rte_eth_conf device_conf = {0};
  // device_conf.rxmode.hw_strip_crc = 1;

  // Configure the device (1, 1 == number of RX/TX queues)
  retval = rte_eth_dev_configure(device, 1, 1, &device_conf);
  if (retval != 0) {
    return retval;
  }

  // Allocate and set up a TX queue (NULL == default config)
  retval = rte_eth_tx_queue_setup(device, 0, TX_QUEUE_SIZE,
                                  rte_eth_dev_socket_id(device), NULL);
  if (retval != 0) {
    return retval;
  }

  // Allocate and set up RX queues (NULL == default config)
  retval = rte_eth_rx_queue_setup(
      device, 0, RX_QUEUE_SIZE, rte_eth_dev_socket_id(device), NULL, mbuf_pool);
  if (retval != 0) {
    return retval;
  }

  // Start the device
  retval = rte_eth_dev_start(device);
  if (retval != 0) {
    return retval;
  }

  // Enable RX in promiscuous mode, just in case
  rte_eth_promiscuous_enable(device);
  if (rte_eth_promiscuous_get(device) != 1) {
    return retval;
  }

  return 0;
}

struct pkt {
  uint64_t ts;
  uint32_t pktlen;
  uint8_t *pkt;
  unsigned device;
};

int packet_timestamp_comparator(const void *a, const void *b) {
  struct pkt *p1 = (struct pkt *)a;
  struct pkt *p2 = (struct pkt *)b;

  if (p1->ts > p2->ts) {
    return 1;
  }

  if (p1->ts < p2->ts) {
    return -1;
  }

  return 0;
}

struct pkts {
  struct pkt *pkts;
  unsigned n_pkts;
  unsigned reserved;
};

struct pkts pkts;

uint64_t *call_path_hit_counter_ptr;
unsigned call_path_hit_counter_sz;

void packetHandler(uint8_t *userData, const struct pcap_pkthdr *pkthdr,
                   const uint8_t *packet) {
  if (pkts.reserved <= pkts.n_pkts) {
    pkts.reserved = pkts.n_pkts + 1000;
    pkts.pkts =
        (struct pkt *)realloc(pkts.pkts, sizeof(struct pkt) * pkts.reserved);
  }

  unsigned device = *((unsigned *)userData);

  pkts.pkts[pkts.n_pkts].ts =
      ((uint64_t)pkthdr->ts.tv_sec) * 1e9 + (uint64_t)pkthdr->ts.tv_usec * 1e3;
  pkts.pkts[pkts.n_pkts].pktlen = pkthdr->len;
  pkts.pkts[pkts.n_pkts].pkt = (uint8_t *)malloc(sizeof(uint8_t) * pkthdr->len);
  memcpy(pkts.pkts[pkts.n_pkts].pkt, packet, pkthdr->len);
  pkts.pkts[pkts.n_pkts].device = device;
  pkts.n_pkts++;
}

void load_pkts(const char *pcap, unsigned device) {
  pcap_t *descr;
  char errbuf[PCAP_ERRBUF_SIZE];

  printf("Loading packets (device=%u, pcap=%s)\n", device, pcap);

  descr = pcap_open_offline(pcap, errbuf);
  if (descr == NULL) {
    printf("pcap %s\n", pcap);
    rte_exit(EXIT_FAILURE, "pcap_open_offline() failed: %s\n", errbuf);
  }

  if (pcap_loop(descr, -1, packetHandler, (uint8_t *)&device) < 0) {
    rte_exit(EXIT_FAILURE, "pcap_loop() failed\n");
  }

  if (pkts.reserved > pkts.n_pkts) {
    pkts.pkts =
        (struct pkt *)realloc(pkts.pkts, sizeof(struct pkt) * pkts.n_pkts);
  }

  pcap_close(descr);
}

struct device_conf_t {
  uint16_t device_id;
  const char *pcap;
};

struct config_t {
  struct device_conf_t *devices_conf;
  uint16_t devices;
  uint32_t loops;
};

struct config_t config;

// Main worker method (for now used on a single thread...)
static void worker_main(void) {
  if (!nf_init()) {
    rte_exit(EXIT_FAILURE, "Error initializing NF");
  }

  NF_INFO("Core %u forwarding packets.", rte_lcore_id());

  if (rte_eth_dev_count_avail() != 2) {
    rte_exit(EXIT_FAILURE, "We assume there will be exactly 2 devices for our "
                           "simple batching implementation.");
  }

  while (1) {
    unsigned DEVICES_COUNT = rte_eth_dev_count_avail();
    for (uint16_t dev = 0; dev < DEVICES_COUNT; dev++) {
      struct rte_mbuf *mbufs[BATCH_SIZE];
      uint16_t rx_count = rte_eth_rx_burst(dev, 0, mbufs, BATCH_SIZE);

      struct rte_mbuf *mbufs_to_send[BATCH_SIZE];
      uint16_t tx_count = 0;
      for (uint16_t n = 0; n < rx_count; n++) {
        uint8_t *data = rte_pktmbuf_mtod(mbufs[n], uint8_t *);
        packet_state_total_length(data, &(mbufs[n]->pkt_len));
        time_ns_t now = current_time();
        uint16_t dst_device =
            nf_process(mbufs[n]->port, data, mbufs[n]->pkt_len, now);

        if (dst_device == dev) {
          rte_pktmbuf_free(mbufs[n]);
        } else { // includes flood when 2 devices, which is equivalent
                 // to just
                 // a
                 // send
          mbufs_to_send[tx_count] = mbufs[n];
          tx_count++;
        }
      }

      uint16_t sent_count =
          rte_eth_tx_burst(1 - dev, 0, mbufs_to_send, tx_count);
      for (uint16_t n = sent_count; n < tx_count; n++) {
        rte_pktmbuf_free(mbufs[n]); // should not happen, but we're in
                                    // the unverified case anyway
      }
    }
  }
}

void nf_config_usage(void) {
  NF_INFO("Usage:\n"
          "[DPDK EAL options] -- [<device:pcap> ...] --loops <loops>\n"
          "\n"
          "\t device: networking device to feed the pcap\n"
          "\t pcap: traffic trace to analyze\n"
          "\t loops: number of times to loop the pcap\n");
}

void nf_config_print(void) {
  NF_INFO("\n--- Config ---\n");

  for (uint16_t device = 0; device < config.devices; device++) {
    NF_INFO("device: %" PRIu16 " PCAP:%s", device,
            config.devices_conf[device].pcap);
  }
  NF_INFO("loops: %" PRIu32, config.loops);

  NF_INFO("\n--- --- ------ ---\n");
}

#define PARSE_ERROR(format, ...)                                               \
  nf_config_usage();                                                           \
  fprintf(stderr, format, ##__VA_ARGS__);                                      \
  exit(EXIT_FAILURE);

void nf_config_init_device(uint16_t device_id) {
  for (int i = 0; i < config.devices; i++) {
    if (config.devices_conf[i].device_id == device_id) {
      PARSE_ERROR("Duplicated device: %" PRIu16 ".", device_id);
    }
  }

  config.devices++;
  config.devices_conf = (struct device_conf_t *)realloc(
      config.devices_conf, sizeof(struct device_conf_t) * config.devices);

  config.devices_conf[config.devices - 1].pcap = NULL;
  config.devices_conf[config.devices - 1].device_id = device_id;
}

void nf_config_init(int argc, char **argv) {
  config.devices = 0;
  config.loops = 1;

  struct option long_options[] = {{"loops", required_argument, NULL, 'l'},
                                  {NULL, 0, NULL, 0}};

  int opt;
  opterr = 0;
  while ((opt = getopt_long(argc, argv, "l:", long_options, NULL)) != EOF) {
    switch (opt) {
    case 'l': {
      config.loops = nf_util_parse_int(optarg, "loops", 10, '\0');
      break;
    }

    default:
      PARSE_ERROR("Unknown option.\n");
    }
  }

  for (int iarg = optind; iarg < argc; iarg++) {
    const char *delim = ":";
    char *token;

    token = strtok(argv[iarg], delim);
    if (token == NULL) {
      PARSE_ERROR("Missing \"device\" argument.\n");
    }

    uint16_t device_id = nf_util_parse_int(token, "device", 10, '\0');
    nf_config_init_device(device_id);

    token = strtok(NULL, delim);
    if (token == NULL) {
      PARSE_ERROR("Missing \"pcap\" argument.\n");
    }

    if (access(token, F_OK) != 0) {
      PARSE_ERROR("No such file \"%s\".\n", token);
    }

    config.devices_conf[config.devices - 1].pcap = token;
  }

  pkts.pkts = NULL;
  pkts.n_pkts = 0;
  pkts.reserved = 0;

  for (int i = 0; i < config.devices; i++) {
    load_pkts(config.devices_conf[i].pcap, config.devices_conf[i].device_id);
  }

  printf("Sorting %u packets...\n", pkts.n_pkts);
  qsort(pkts.pkts, pkts.n_pkts, sizeof(struct pkt),
        packet_timestamp_comparator);

  uint64_t last_ts = 0;
  for (unsigned i = 0; i < pkts.n_pkts; i++) {
    struct pkt pkt = pkts.pkts[i];
    assert(pkt.ts >= last_ts);
    last_ts = pkt.ts;
  }
}

// Entry point
int main(int argc, char **argv) {
  // Initialize the DPDK Environment Abstraction Layer (EAL)
  int ret = rte_eal_init(argc, argv);
  if (ret < 0) {
    rte_exit(EXIT_FAILURE, "Error with EAL initialization, ret=%d\n", ret);
  }
  argc -= ret;
  argv += ret;

  // Create a memory pool
  unsigned nb_devices = rte_eth_dev_count_avail();
  struct rte_mempool *mbuf_pool = rte_pktmbuf_pool_create(
      "MEMPOOL",                         // name
      MEMPOOL_BUFFER_COUNT * nb_devices, // #elements
      0, // cache size (per-core, not useful in a single-threaded app)
      0, // application private area size
      RTE_MBUF_DEFAULT_BUF_SIZE, // data buffer size
      rte_socket_id()            // socket ID
  );
  if (mbuf_pool == NULL) {
    rte_exit(EXIT_FAILURE, "Cannot create pool: %s\n", rte_strerror(rte_errno));
  }

  // Initialize all devices
  for (uint16_t device = 0; device < nb_devices; device++) {
    ret = nf_init_device(device, mbuf_pool);
    if (ret == 0) {
      NF_INFO("Initialized device %" PRIu16 ".", device);
    } else {
      rte_exit(EXIT_FAILURE, "Cannot init device %" PRIu16 ": %d", device, ret);
    }
  }

  // Run!
  worker_main();

  return 0;
}
