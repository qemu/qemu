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

#include "hw/virtio/virtio.h"
#include "hw/pci/pci.h"

#define TYPE_VIRTIO_NET "virtio-net-device"
#define VIRTIO_NET(obj) \
        OBJECT_CHECK(VirtIONet, (obj), TYPE_VIRTIO_NET)

#define ETH_ALEN    6

/* from Linux's virtio_net.h */

/* The ID for virtio_net */
#define VIRTIO_ID_NET   1

/* The feature bitmap for virtio net */
#define VIRTIO_NET_F_CSUM       0       /* Host handles pkts w/ partial csum */
#define VIRTIO_NET_F_GUEST_CSUM 1       /* Guest handles pkts w/ partial csum */
#define VIRTIO_NET_F_CTRL_GUEST_OFFLOADS 2 /* Control channel offload
                                         * configuration support */
#define VIRTIO_NET_F_MAC        5       /* Host has given MAC address. */
#define VIRTIO_NET_F_GSO        6       /* Host handles pkts w/ any GSO type */
#define VIRTIO_NET_F_GUEST_TSO4 7       /* Guest can handle TSOv4 in. */
#define VIRTIO_NET_F_GUEST_TSO6 8       /* Guest can handle TSOv6 in. */
#define VIRTIO_NET_F_GUEST_ECN  9       /* Guest can handle TSO[6] w/ ECN in. */
#define VIRTIO_NET_F_GUEST_UFO  10      /* Guest can handle UFO in. */
#define VIRTIO_NET_F_HOST_TSO4  11      /* Host can handle TSOv4 in. */
#define VIRTIO_NET_F_HOST_TSO6  12      /* Host can handle TSOv6 in. */
#define VIRTIO_NET_F_HOST_ECN   13      /* Host can handle TSO[6] w/ ECN in. */
#define VIRTIO_NET_F_HOST_UFO   14      /* Host can handle UFO in. */
#define VIRTIO_NET_F_MRG_RXBUF  15      /* Host can merge receive buffers. */
#define VIRTIO_NET_F_STATUS     16      /* virtio_net_config.status available */
#define VIRTIO_NET_F_CTRL_VQ    17      /* Control channel available */
#define VIRTIO_NET_F_CTRL_RX    18      /* Control channel RX mode support */
#define VIRTIO_NET_F_CTRL_VLAN  19      /* Control channel VLAN filtering */
#define VIRTIO_NET_F_CTRL_RX_EXTRA 20   /* Extra RX mode control support */
#define VIRTIO_NET_F_GUEST_ANNOUNCE 21  /* Guest can announce itself */
#define VIRTIO_NET_F_MQ         22      /* Device supports Receive Flow
                                         * Steering */

#define VIRTIO_NET_F_CTRL_MAC_ADDR   23 /* Set MAC address */

#define VIRTIO_NET_S_LINK_UP    1       /* Link is up */
#define VIRTIO_NET_S_ANNOUNCE   2       /* Announcement is needed */

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

struct virtio_net_config
{
    /* The config defining mac address ($ETH_ALEN bytes) */
    uint8_t mac[ETH_ALEN];
    /* See VIRTIO_NET_F_STATUS and VIRTIO_NET_S_* above */
    uint16_t status;
    /* Max virtqueue pairs supported by the device */
    uint16_t max_virtqueue_pairs;
} QEMU_PACKED;

/*
 * Control virtqueue data structures
 *
 * The control virtqueue expects a header in the first sg entry
 * and an ack/status response in the last entry.  Data for the
 * command goes in between.
 */
struct virtio_net_ctrl_hdr {
    uint8_t class;
    uint8_t cmd;
};

typedef uint8_t virtio_net_ctrl_ack;

#define VIRTIO_NET_OK     0
#define VIRTIO_NET_ERR    1

/*
 * Control the RX mode, ie. promisucous, allmulti, etc...
 * All commands require an "out" sg entry containing a 1 byte
 * state value, zero = disable, non-zero = enable.  Commands
 * 0 and 1 are supported with the VIRTIO_NET_F_CTRL_RX feature.
 * Commands 2-5 are added with VIRTIO_NET_F_CTRL_RX_EXTRA.
 */
#define VIRTIO_NET_CTRL_RX    0
 #define VIRTIO_NET_CTRL_RX_PROMISC      0
 #define VIRTIO_NET_CTRL_RX_ALLMULTI     1
 #define VIRTIO_NET_CTRL_RX_ALLUNI       2
 #define VIRTIO_NET_CTRL_RX_NOMULTI      3
 #define VIRTIO_NET_CTRL_RX_NOUNI        4
 #define VIRTIO_NET_CTRL_RX_NOBCAST      5

/*
 * Control the MAC
 *
 * The MAC filter table is managed by the hypervisor, the guest should
 * assume the size is infinite.  Filtering should be considered
 * non-perfect, ie. based on hypervisor resources, the guest may
 * received packets from sources not specified in the filter list.
 *
 * In addition to the class/cmd header, the TABLE_SET command requires
 * two out scatterlists.  Each contains a 4 byte count of entries followed
 * by a concatenated byte stream of the ETH_ALEN MAC addresses.  The
 * first sg list contains unicast addresses, the second is for multicast.
 * This functionality is present if the VIRTIO_NET_F_CTRL_RX feature
 * is available.
 *
 * The ADDR_SET command requests one out scatterlist, it contains a
 * 6 bytes MAC address. This functionality is present if the
 * VIRTIO_NET_F_CTRL_MAC_ADDR feature is available.
 */
struct virtio_net_ctrl_mac {
    uint32_t entries;
    uint8_t macs[][ETH_ALEN];
};

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

#define VIRTIO_NET_CTRL_MAC    1
 #define VIRTIO_NET_CTRL_MAC_TABLE_SET        0
 #define VIRTIO_NET_CTRL_MAC_ADDR_SET         1

/*
 * Control VLAN filtering
 *
 * The VLAN filter table is controlled via a simple ADD/DEL interface.
 * VLAN IDs not added may be filterd by the hypervisor.  Del is the
 * opposite of add.  Both commands expect an out entry containing a 2
 * byte VLAN ID.  VLAN filterting is available with the
 * VIRTIO_NET_F_CTRL_VLAN feature bit.
 */
#define VIRTIO_NET_CTRL_VLAN       2
 #define VIRTIO_NET_CTRL_VLAN_ADD             0
 #define VIRTIO_NET_CTRL_VLAN_DEL             1

/*
 * Control link announce acknowledgement
 *
 * VIRTIO_NET_S_ANNOUNCE bit in the status field requests link announcement from
 * guest driver. The driver is notified by config space change interrupt.  The
 * command VIRTIO_NET_CTRL_ANNOUNCE_ACK is used to indicate that the driver has
 * received the notification. It makes the device clear the bit
 * VIRTIO_NET_S_ANNOUNCE in the status field.
 */
#define VIRTIO_NET_CTRL_ANNOUNCE       3
 #define VIRTIO_NET_CTRL_ANNOUNCE_ACK         0

/*
 * Control Multiqueue
 *
 * The command VIRTIO_NET_CTRL_MQ_VQ_PAIRS_SET
 * enables multiqueue, specifying the number of the transmit and
 * receive queues that will be used. After the command is consumed and acked by
 * the device, the device will not steer new packets on receive virtqueues
 * other than specified nor read from transmit virtqueues other than specified.
 * Accordingly, driver should not transmit new packets  on virtqueues other than
 * specified.
 */
struct virtio_net_ctrl_mq {
    uint16_t virtqueue_pairs;
};

#define VIRTIO_NET_CTRL_MQ   4
 #define VIRTIO_NET_CTRL_MQ_VQ_PAIRS_SET        0
 #define VIRTIO_NET_CTRL_MQ_VQ_PAIRS_MIN        1
 #define VIRTIO_NET_CTRL_MQ_VQ_PAIRS_MAX        0x8000

/*
 * Control network offloads
 *
 * Dynamic offloads are available with the
 * VIRTIO_NET_F_CTRL_GUEST_OFFLOADS feature bit.
 */
#define VIRTIO_NET_CTRL_GUEST_OFFLOADS   5
 #define VIRTIO_NET_CTRL_GUEST_OFFLOADS_SET        0

#define DEFINE_VIRTIO_NET_FEATURES(_state, _field) \
        DEFINE_VIRTIO_COMMON_FEATURES(_state, _field), \
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
