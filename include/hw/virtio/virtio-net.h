/*
 * Virtio Network Device
 *
 * Copyright IBM, Corp. 2007
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#ifndef QEMU_VIRTIO_NET_H
#define QEMU_VIRTIO_NET_H

#include "qemu/units.h"
#include "standard-headers/linux/virtio_net.h"
#include "hw/virtio/virtio.h"
#include "net/announce.h"
#include "qemu/option_int.h"
#include "qom/object.h"

#include "ebpf/ebpf_rss.h"

#define TYPE_VIRTIO_NET "virtio-net-device"
OBJECT_DECLARE_SIMPLE_TYPE(VirtIONet, VIRTIO_NET)

#define TX_TIMER_INTERVAL 150000 /* 150 us */

/* Limit the number of packets that can be sent via a single flush
 * of the TX queue.  This gives us a guaranteed exit condition and
 * ensures fairness in the io path.  256 conveniently matches the
 * length of the TX queue and shows a good balance of performance
 * and latency. */
#define TX_BURST 256

typedef struct virtio_net_conf
{
    uint32_t txtimer;
    int32_t txburst;
    char *tx;
    uint16_t rx_queue_size;
    uint16_t tx_queue_size;
    uint16_t mtu;
    int32_t speed;
    char *duplex_str;
    uint8_t duplex;
    char *primary_id_str;
} virtio_net_conf;

/* Coalesced packets type & status */
typedef enum {
    RSC_COALESCE,           /* Data been coalesced */
    RSC_FINAL,              /* Will terminate current connection */
    RSC_NO_MATCH,           /* No matched in the buffer pool */
    RSC_BYPASS,             /* Packet to be bypass, not tcp, tcp ctrl, etc */
    RSC_CANDIDATE                /* Data want to be coalesced */
} CoalesceStatus;

typedef struct VirtioNetRscStat {
    uint32_t received;
    uint32_t coalesced;
    uint32_t over_size;
    uint32_t cache;
    uint32_t empty_cache;
    uint32_t no_match_cache;
    uint32_t win_update;
    uint32_t no_match;
    uint32_t tcp_syn;
    uint32_t tcp_ctrl_drain;
    uint32_t dup_ack;
    uint32_t dup_ack1;
    uint32_t dup_ack2;
    uint32_t pure_ack;
    uint32_t ack_out_of_win;
    uint32_t data_out_of_win;
    uint32_t data_out_of_order;
    uint32_t data_after_pure_ack;
    uint32_t bypass_not_tcp;
    uint32_t tcp_option;
    uint32_t tcp_all_opt;
    uint32_t ip_frag;
    uint32_t ip_ecn;
    uint32_t ip_hacked;
    uint32_t ip_option;
    uint32_t purge_failed;
    uint32_t drain_failed;
    uint32_t final_failed;
    int64_t  timer;
} VirtioNetRscStat;

/* Rsc unit general info used to checking if can coalescing */
typedef struct VirtioNetRscUnit {
    void *ip;   /* ip header */
    uint16_t *ip_plen;      /* data len pointer in ip header field */
    struct tcp_header *tcp; /* tcp header */
    uint16_t tcp_hdrlen;    /* tcp header len */
    uint16_t payload;       /* pure payload without virtio/eth/ip/tcp */
} VirtioNetRscUnit;

/* Coalesced segment */
typedef struct VirtioNetRscSeg {
    QTAILQ_ENTRY(VirtioNetRscSeg) next;
    void *buf;
    size_t size;
    uint16_t packets;
    uint16_t dup_ack;
    bool is_coalesced;      /* need recal ipv4 header checksum, mark here */
    VirtioNetRscUnit unit;
    NetClientState *nc;
} VirtioNetRscSeg;


/* Chain is divided by protocol(ipv4/v6) and NetClientInfo */
typedef struct VirtioNetRscChain {
    QTAILQ_ENTRY(VirtioNetRscChain) next;
    VirtIONet *n;                            /* VirtIONet */
    uint16_t proto;
    uint8_t  gso_type;
    uint16_t max_payload;
    QEMUTimer *drain_timer;
    QTAILQ_HEAD(, VirtioNetRscSeg) buffers;
    VirtioNetRscStat stat;
} VirtioNetRscChain;

/* Maximum packet size we can receive from tap device: header + 64k */
#define VIRTIO_NET_MAX_BUFSIZE (sizeof(struct virtio_net_hdr) + (64 * KiB))

#define VIRTIO_NET_RSS_MAX_KEY_SIZE     40
#define VIRTIO_NET_RSS_MAX_TABLE_LEN    128

typedef struct VirtioNetRssData {
    bool    enabled;
    bool    enabled_software_rss;
    bool    redirect;
    bool    populate_hash;
    uint32_t hash_types;
    uint8_t key[VIRTIO_NET_RSS_MAX_KEY_SIZE];
    uint16_t indirections_len;
    uint16_t *indirections_table;
    uint16_t default_queue;
} VirtioNetRssData;

typedef struct VirtIONetQueue {
    VirtQueue *rx_vq;
    VirtQueue *tx_vq;
    QEMUTimer *tx_timer;
    QEMUBH *tx_bh;
    uint32_t tx_waiting;
    struct {
        VirtQueueElement *elem;
    } async_tx;
    struct VirtIONet *n;
} VirtIONetQueue;

struct VirtIONet {
    VirtIODevice parent_obj;
    uint8_t mac[ETH_ALEN];
    uint16_t status;
    VirtIONetQueue *vqs;
    VirtQueue *ctrl_vq;
    NICState *nic;
    /* RSC Chains - temporary storage of coalesced data,
       all these data are lost in case of migration */
    QTAILQ_HEAD(, VirtioNetRscChain) rsc_chains;
    uint32_t tx_timeout;
    int32_t tx_burst;
    uint32_t has_vnet_hdr;
    size_t host_hdr_len;
    size_t guest_hdr_len;
    uint64_t host_features;
    uint32_t rsc_timeout;
    uint8_t rsc4_enabled;
    uint8_t rsc6_enabled;
    uint8_t has_ufo;
    uint32_t mergeable_rx_bufs;
    uint8_t promisc;
    uint8_t allmulti;
    uint8_t alluni;
    uint8_t nomulti;
    uint8_t nouni;
    uint8_t nobcast;
    uint8_t vhost_started;
    struct {
        uint32_t in_use;
        uint32_t first_multi;
        uint8_t multi_overflow;
        uint8_t uni_overflow;
        uint8_t *macs;
    } mac_table;
    uint32_t *vlans;
    virtio_net_conf net_conf;
    NICConf nic_conf;
    DeviceState *qdev;
    int multiqueue;
    uint16_t max_queue_pairs;
    uint16_t curr_queue_pairs;
    uint16_t max_ncs;
    size_t config_size;
    char *netclient_name;
    char *netclient_type;
    uint64_t curr_guest_offloads;
    /* used on saved state restore phase to preserve the curr_guest_offloads */
    uint64_t saved_guest_offloads;
    AnnounceTimer announce_timer;
    bool needs_vnet_hdr_swap;
    bool mtu_bypass_backend;
    /* primary failover device is hidden*/
    bool failover_primary_hidden;
    bool failover;
    DeviceListener primary_listener;
    QDict *primary_opts;
    bool primary_opts_from_json;
    Notifier migration_state;
    VirtioNetRssData rss_data;
    struct NetRxPkt *rx_pkt;
    struct EBPFRSSContext ebpf_rss;
};

void virtio_net_set_netclient_name(VirtIONet *n, const char *name,
                                   const char *type);

#endif
