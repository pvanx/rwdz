#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_mbuf.h>
#include <rte_ip.h>
#include <rte_udp.h>
#include <rte_lcore.h>
#include <rte_cycles.h>
// setting port dan lain lain
#define NB_MBUF 8192
#define BURST_SIZE 64
#define PAYLOAD_SIZE 1450
#define SRC_PORT 12345
#define DST_PORT 54321
#define SRC_IP 0xc0a80164 // 192.168.1.100
#define DST_IP 0xc0a80165 // 192.168.1.101

static void fill_udp_packet(struct rte_mbuf *mbuf) {
    struct rte_ether_hdr *eth = rte_pktmbuf_mtod(mbuf, struct rte_ether_hdr *);
    struct rte_ipv4_hdr *ip = (struct rte_ipv4_hdr *)(eth + 1);
    struct rte_udp_hdr *udp = (struct rte_udp_hdr *)(ip + 1);
    uint8_t *payload = (uint8_t *)(udp + 1);
  
    memset(eth->d_addr.addr_bytes, 0xff, 6);
    memset(eth->s_addr.addr_bytes, 0xaa, 6);
    eth->ether_type = htons(RTE_ETHER_TYPE_IPV4);

    ip->version_ihl = 0x45;
    ip->type_of_service = 0;
    ip->total_length = htons(sizeof(struct rte_ipv4_hdr) + sizeof(struct rte_udp_hdr) + PAYLOAD_SIZE);
    ip->packet_id = htons(rand());
    ip->fragment_offset = 0;
    ip->time_to_live = 64;
    ip->next_proto_id = IPPROTO_UDP;
    ip->src_addr = htonl(SRC_IP);
    ip->dst_addr = htonl(DST_IP);
    ip->hdr_checksum = 0;

    udp->src_port = htons(SRC_PORT);
    udp->dst_port = htons(DST_PORT);
    udp->dgram_len = htons(sizeof(struct rte_udp_hdr) + PAYLOAD_SIZE);
    udp->dgram_cksum = 0;

    for (int i = 0; i < PAYLOAD_SIZE; i++)
        payload[i] = 'A' + (rand() % 26);

    mbuf->data_len = sizeof(struct rte_ether_hdr) + sizeof(struct rte_ipv4_hdr) + sizeof(struct rte_udp_hdr) + PAYLOAD_SIZE;
    mbuf->pkt_len = mbuf->data_len;

    ip->hdr_checksum = rte_ipv4_cksum(ip);
}

static int lcore_sender(__rte_unused void *arg) {
    uint16_t port_id = (uint16_t)(uintptr_t)arg;
    struct rte_mempool *mbuf_pool = rte_pktmbuf_pool_lookup("MBUF_POOL");
    struct rte_mbuf *pkts[BURST_SIZE];

    while (1) {
        for (int i = 0; i < BURST_SIZE; i++) {
            pkts[i] = rte_pktmbuf_alloc(mbuf_pool);
            if (!pkts[i]) continue;
            fill_udp_packet(pkts[i]);
        }
        uint16_t nb_tx = rte_eth_tx_burst(port_id, 0, pkts, BURST_SIZE);
        for (int i = nb_tx; i < BURST_SIZE; i++)
            rte_pktmbuf_free(pkts[i]);
    }
    return 0;
}

int main(int argc, char **argv) {
    int ret = rte_eal_init(argc, argv);
    if (ret < 0) rte_exit(EXIT_FAILURE, "EAL init failed\n");

    uint16_t nb_ports = rte_eth_dev_count_avail();
    if (nb_ports < 1) rte_exit(EXIT_FAILURE, "No ports available\n");

    struct rte_mempool *mbuf_pool = rte_pktmbuf_pool_create("MBUF_POOL", NB_MBUF * nb_ports,
        0, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
    if (!mbuf_pool) rte_exit(EXIT_FAILURE, "Cannot create mbuf pool\n");
  
    for (uint16_t port = 0; port < nb_ports; port++) {
        struct rte_eth_conf port_conf = {0};
        port_conf.rxmode.max_rx_pkt_len = RTE_ETHER_MAX_LEN;
        ret = rte_eth_dev_configure(port, 1, 1, &port_conf);
        if (ret < 0) rte_exit(EXIT_FAILURE, "Cannot configure port %u\n", port);

        ret = rte_eth_rx_queue_setup(port, 0, 1024, rte_eth_dev_socket_id(port), NULL, mbuf_pool);
        if (ret < 0) rte_exit(EXIT_FAILURE, "Cannot setup RX queue for port %u\n", port);

        ret = rte_eth_tx_queue_setup(port, 0, 1024, rte_eth_dev_socket_id(port), NULL);
        if (ret < 0) rte_exit(EXIT_FAILURE, "Cannot setup TX queue for port %u\n", port);

        ret = rte_eth_dev_start(port);
        if (ret < 0) rte_exit(EXIT_FAILURE, "Cannot start port %u\n", port);

        rte_eth_promiscuous_enable(port);
    }

    uint16_t port_id = 0;
    RTE_LCORE_FOREACH_WORKER(lcore_id) {
        if (port_id >= nb_ports) break;
        rte_eal_remote_launch(lcore_sender, (void *)(uintptr_t)port_id, lcore_id);
        port_id++;
    }

    if (port_id < nb_ports) {
        lcore_sender((void *)(uintptr_t)port_id);
    }

    rte_eal_mp_wait_lcore();
    return 0;
}
