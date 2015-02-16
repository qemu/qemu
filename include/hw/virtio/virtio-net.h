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

#ifndef _QEMU_VIRTIO_NET_H
#define _QEMU_VIRTIO_NET_H

#include "standard-headers/linux/virtio_net.h"
#include "hw/virtio/virtio.h"

#define TYPE_VIRTIO_NET "virtio-net-device"
#define VIRTIO_NET(obj) \
        OBJECT_CHECK(VirtIONet, (obj), TYPE_VIRTIO_NET)

#define VIRTIO_NET_F_CTRL_GUEST_OFFLOADS 2 /* Control channel offload
                                         * configuration support */

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
} virtio_net_conf;

/* Maximum packet size we can receive from tap device: header + 64k */
#define VIRTIO_NET_MAX_BUFSIZE (sizeof(struct virtio_net_hdr) + (64 << 10))

typedef struct VirtIONetQueue {
    VirtQueue *rx_vq;
    VirtQueue *tx_vq;
    QEMUTimer *tx_timer;
    QEMUBH *tx_bh;
    int tx_waiting;
    struct {
        VirtQueueElement elem;
        ssize_t len;
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
    uint8_t has_ufo;
    int mergeable_rx_bufs;
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
} VirtIONet;

/*
 * Control network offloads
 *
 * Dynamic offloads are available with the
 * VIRTIO_NET_F_CTRL_GUEST_OFFLOADS feature bit.
 */
#define VIRTIO_NET_CTRL_GUEST_OFFLOADS   5
 #define VIRTIO_NET_CTRL_GUEST_OFFLOADS_SET        0

#define DEFINE_VIRTIO_NET_FEATURES(_state, _field) \
        DEFINE_PROP_BIT("any_layout", _state, _field, VIRTIO_F_ANY_LAYOUT, true), \
        DEFINE_PROP_BIT("csum", _state, _field, VIRTIO_NET_F_CSUM, true), \
        DEFINE_PROP_BIT("guest_csum", _state, _field, VIRTIO_NET_F_GUEST_CSUM, true), \
        DEFINE_PROP_BIT("gso", _state, _field, VIRTIO_NET_F_GSO, true), \
        DEFINE_PROP_BIT("guest_tso4", _state, _field, VIRTIO_NET_F_GUEST_TSO4, true), \
        DEFINE_PROP_BIT("guest_tso6", _state, _field, VIRTIO_NET_F_GUEST_TSO6, true), \
        DEFINE_PROP_BIT("guest_ecn", _state, _field, VIRTIO_NET_F_GUEST_ECN, true), \
        DEFINE_PROP_BIT("guest_ufo", _state, _field, VIRTIO_NET_F_GUEST_UFO, true), \
        DEFINE_PROP_BIT("guest_announce", _state, _field, VIRTIO_NET_F_GUEST_ANNOUNCE, true), \
        DEFINE_PROP_BIT("host_tso4", _state, _field, VIRTIO_NET_F_HOST_TSO4, true), \
        DEFINE_PROP_BIT("host_tso6", _state, _field, VIRTIO_NET_F_HOST_TSO6, true), \
        DEFINE_PROP_BIT("host_ecn", _state, _field, VIRTIO_NET_F_HOST_ECN, true), \
        DEFINE_PROP_BIT("host_ufo", _state, _field, VIRTIO_NET_F_HOST_UFO, true), \
        DEFINE_PROP_BIT("mrg_rxbuf", _state, _field, VIRTIO_NET_F_MRG_RXBUF, true), \
        DEFINE_PROP_BIT("status", _state, _field, VIRTIO_NET_F_STATUS, true), \
        DEFINE_PROP_BIT("ctrl_vq", _state, _field, VIRTIO_NET_F_CTRL_VQ, true), \
        DEFINE_PROP_BIT("ctrl_rx", _state, _field, VIRTIO_NET_F_CTRL_RX, true), \
        DEFINE_PROP_BIT("ctrl_vlan", _state, _field, VIRTIO_NET_F_CTRL_VLAN, true), \
        DEFINE_PROP_BIT("ctrl_rx_extra", _state, _field, VIRTIO_NET_F_CTRL_RX_EXTRA, true), \
        DEFINE_PROP_BIT("ctrl_mac_addr", _state, _field, VIRTIO_NET_F_CTRL_MAC_ADDR, true), \
        DEFINE_PROP_BIT("ctrl_guest_offloads", _state, _field, VIRTIO_NET_F_CTRL_GUEST_OFFLOADS, true), \
        DEFINE_PROP_BIT("mq", _state, _field, VIRTIO_NET_F_MQ, false)

#define DEFINE_VIRTIO_NET_PROPERTIES(_state, _field)                           \
    DEFINE_PROP_UINT32("x-txtimer", _state, _field.txtimer, TX_TIMER_INTERVAL),\
    DEFINE_PROP_INT32("x-txburst", _state, _field.txburst, TX_BURST),          \
    DEFINE_PROP_STRING("tx", _state, _field.tx)

void virtio_net_set_config_size(VirtIONet *n, uint32_t host_features);
void virtio_net_set_netclient_name(VirtIONet *n, const char *name,
                                   const char *type);

#endif
