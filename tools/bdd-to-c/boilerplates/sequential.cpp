#ifdef __cplusplus
extern "C" {
#endif
#include <lib/unverified/sketch.h>
#include <lib/verified/cht.h>
#include <lib/verified/double-chain.h>
#include <lib/verified/map.h>
#include <lib/verified/vector.h>

#include <lib/verified/expirator.h>
#include <lib/verified/packet-io.h>
#include <lib/verified/tcpudp_hdr.h>
#include <lib/verified/vigor-time.h>
#ifdef __cplusplus
}
#endif

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

#include <stdbool.h>

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
