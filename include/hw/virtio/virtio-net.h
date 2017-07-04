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

#include "standard-headers/linux/virtio_net.h"
#include "hw/virtio/virtio.h"

#define TYPE_VIRTIO_NET "virtio-net-device"
#define VIRTIO_NET(obj) \
        OBJECT_CHECK(VirtIONet, (obj), TYPE_VIRTIO_NET)

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
} virtio_net_conf;

/* Maximum packet size we can receive from tap device: header + 64k */
#define VIRTIO_NET_MAX_BUFSIZE (sizeof(struct virtio_net_hdr) + (64 << 10))

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

typedef struct VirtIONet {
    VirtIODevice parent_obj;
    uint8_t mac[ETH_ALEN];
    uint16_t status;
    VirtIONetQueue *vqs;
    VirtQueue *ctrl_vq;
    NICState *nic;
    uint32_t tx_timeout;
    int32_t tx_burst;
    uint32_t has_vnet_hdr;
    size_t host_hdr_len;
    size_t guest_hdr_len;
    uint32_t host_features;
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
    uint16_t max_queues;
    uint16_t curr_queues;
    size_t config_size;
    char *netclient_name;
    char *netclient_type;
    uint64_t curr_guest_offloads;
    QEMUTimer *announce_timer;
    int announce_counter;
    bool needs_vnet_hdr_swap;
    bool mtu_bypass_backend;
} VirtIONet;

void virtio_net_set_netclient_name(VirtIONet *n, const char *name,
                                   const char *type);

#endif
