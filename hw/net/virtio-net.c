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

#include "qemu/osdep.h"
#include "qemu/iov.h"
#include "qemu/module.h"
#include "hw/virtio/virtio.h"
#include "net/net.h"
#include "net/checksum.h"
#include "net/tap.h"
#include "qemu/error-report.h"
#include "qemu/timer.h"
#include "hw/virtio/virtio-net.h"
#include "net/vhost_net.h"
#include "net/announce.h"
#include "hw/virtio/virtio-bus.h"
#include "qapi/error.h"
#include "qapi/qapi-events-net.h"
#include "hw/virtio/virtio-access.h"
#include "migration/misc.h"
#include "standard-headers/linux/ethtool.h"
#include "trace.h"

#define VIRTIO_NET_VM_VERSION    11

#define MAC_TABLE_ENTRIES    64
#define MAX_VLAN    (1 << 12)   /* Per 802.1Q definition */

/* previously fixed value */
#define VIRTIO_NET_RX_QUEUE_DEFAULT_SIZE 256
#define VIRTIO_NET_TX_QUEUE_DEFAULT_SIZE 256

/* for now, only allow larger queues; with virtio-1, guest can downsize */
#define VIRTIO_NET_RX_QUEUE_MIN_SIZE VIRTIO_NET_RX_QUEUE_DEFAULT_SIZE
#define VIRTIO_NET_TX_QUEUE_MIN_SIZE VIRTIO_NET_TX_QUEUE_DEFAULT_SIZE

#define VIRTIO_NET_IP4_ADDR_SIZE   8        /* ipv4 saddr + daddr */

#define VIRTIO_NET_TCP_FLAG         0x3F
#define VIRTIO_NET_TCP_HDR_LENGTH   0xF000

/* IPv4 max payload, 16 bits in the header */
#define VIRTIO_NET_MAX_IP4_PAYLOAD (65535 - sizeof(struct ip_header))
#define VIRTIO_NET_MAX_TCP_PAYLOAD 65535

/* header length value in ip header without option */
#define VIRTIO_NET_IP4_HEADER_LENGTH 5

#define VIRTIO_NET_IP6_ADDR_SIZE   32      /* ipv6 saddr + daddr */
#define VIRTIO_NET_MAX_IP6_PAYLOAD VIRTIO_NET_MAX_TCP_PAYLOAD

/* Purge coalesced packets timer interval, This value affects the performance
   a lot, and should be tuned carefully, '300000'(300us) is the recommended
   value to pass the WHQL test, '50000' can gain 2x netperf throughput with
   tso/gso/gro 'off'. */
#define VIRTIO_NET_RSC_DEFAULT_INTERVAL 300000

/* temporary until standard header include it */
#if !defined(VIRTIO_NET_HDR_F_RSC_INFO)

#define VIRTIO_NET_HDR_F_RSC_INFO  4 /* rsc_ext data in csum_ fields */
#define VIRTIO_NET_F_RSC_EXT       61

static inline __virtio16 *virtio_net_rsc_ext_num_packets(
    struct virtio_net_hdr *hdr)
{
    return &hdr->csum_start;
}

static inline __virtio16 *virtio_net_rsc_ext_num_dupacks(
    struct virtio_net_hdr *hdr)
{
    return &hdr->csum_offset;
}

#endif

static VirtIOFeature feature_sizes[] = {
    {.flags = 1ULL << VIRTIO_NET_F_MAC,
     .end = virtio_endof(struct virtio_net_config, mac)},
    {.flags = 1ULL << VIRTIO_NET_F_STATUS,
     .end = virtio_endof(struct virtio_net_config, status)},
    {.flags = 1ULL << VIRTIO_NET_F_MQ,
     .end = virtio_endof(struct virtio_net_config, max_virtqueue_pairs)},
    {.flags = 1ULL << VIRTIO_NET_F_MTU,
     .end = virtio_endof(struct virtio_net_config, mtu)},
    {.flags = 1ULL << VIRTIO_NET_F_SPEED_DUPLEX,
     .end = virtio_endof(struct virtio_net_config, duplex)},
    {}
};

static VirtIONetQueue *virtio_net_get_subqueue(NetClientState *nc)
{
    VirtIONet *n = qemu_get_nic_opaque(nc);

    return &n->vqs[nc->queue_index];
}

static int vq2q(int queue_index)
{
    return queue_index / 2;
}

/* TODO
 * - we could suppress RX interrupt if we were so inclined.
 */

static void virtio_net_get_config(VirtIODevice *vdev, uint8_t *config)
{
    VirtIONet *n = VIRTIO_NET(vdev);
    struct virtio_net_config netcfg;

    virtio_stw_p(vdev, &netcfg.status, n->status);
    virtio_stw_p(vdev, &netcfg.max_virtqueue_pairs, n->max_queues);
    virtio_stw_p(vdev, &netcfg.mtu, n->net_conf.mtu);
    memcpy(netcfg.mac, n->mac, ETH_ALEN);
    virtio_stl_p(vdev, &netcfg.speed, n->net_conf.speed);
    netcfg.duplex = n->net_conf.duplex;
    memcpy(config, &netcfg, n->config_size);
}

static void virtio_net_set_config(VirtIODevice *vdev, const uint8_t *config)
{
    VirtIONet *n = VIRTIO_NET(vdev);
    struct virtio_net_config netcfg = {};

    memcpy(&netcfg, config, n->config_size);

    if (!virtio_vdev_has_feature(vdev, VIRTIO_NET_F_CTRL_MAC_ADDR) &&
        !virtio_vdev_has_feature(vdev, VIRTIO_F_VERSION_1) &&
        memcmp(netcfg.mac, n->mac, ETH_ALEN)) {
        memcpy(n->mac, netcfg.mac, ETH_ALEN);
        qemu_format_nic_info_str(qemu_get_queue(n->nic), n->mac);
    }
}

static bool virtio_net_started(VirtIONet *n, uint8_t status)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(n);
    return (status & VIRTIO_CONFIG_S_DRIVER_OK) &&
        (n->status & VIRTIO_NET_S_LINK_UP) && vdev->vm_running;
}

static void virtio_net_announce_notify(VirtIONet *net)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(net);
    trace_virtio_net_announce_notify();

    net->status |= VIRTIO_NET_S_ANNOUNCE;
    virtio_notify_config(vdev);
}

static void virtio_net_announce_timer(void *opaque)
{
    VirtIONet *n = opaque;
    trace_virtio_net_announce_timer(n->announce_timer.round);

    n->announce_timer.round--;
    virtio_net_announce_notify(n);
}

static void virtio_net_announce(NetClientState *nc)
{
    VirtIONet *n = qemu_get_nic_opaque(nc);
    VirtIODevice *vdev = VIRTIO_DEVICE(n);

    /*
     * Make sure the virtio migration announcement timer isn't running
     * If it is, let it trigger announcement so that we do not cause
     * confusion.
     */
    if (n->announce_timer.round) {
        return;
    }

    if (virtio_vdev_has_feature(vdev, VIRTIO_NET_F_GUEST_ANNOUNCE) &&
        virtio_vdev_has_feature(vdev, VIRTIO_NET_F_CTRL_VQ)) {
            virtio_net_announce_notify(n);
    }
}

static void virtio_net_vhost_status(VirtIONet *n, uint8_t status)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(n);
    NetClientState *nc = qemu_get_queue(n->nic);
    int queues = n->multiqueue ? n->max_queues : 1;

    if (!get_vhost_net(nc->peer)) {
        return;
    }

    if ((virtio_net_started(n, status) && !nc->peer->link_down) ==
        !!n->vhost_started) {
        return;
    }
    if (!n->vhost_started) {
        int r, i;

        if (n->needs_vnet_hdr_swap) {
            error_report("backend does not support %s vnet headers; "
                         "falling back on userspace virtio",
                         virtio_is_big_endian(vdev) ? "BE" : "LE");
            return;
        }

        /* Any packets outstanding? Purge them to avoid touching rings
         * when vhost is running.
         */
        for (i = 0;  i < queues; i++) {
            NetClientState *qnc = qemu_get_subqueue(n->nic, i);

            /* Purge both directions: TX and RX. */
            qemu_net_queue_purge(qnc->peer->incoming_queue, qnc);
            qemu_net_queue_purge(qnc->incoming_queue, qnc->peer);
        }

        if (virtio_has_feature(vdev->guest_features, VIRTIO_NET_F_MTU)) {
            r = vhost_net_set_mtu(get_vhost_net(nc->peer), n->net_conf.mtu);
            if (r < 0) {
                error_report("%uBytes MTU not supported by the backend",
                             n->net_conf.mtu);

                return;
            }
        }

        n->vhost_started = 1;
        r = vhost_net_start(vdev, n->nic->ncs, queues);
        if (r < 0) {
            error_report("unable to start vhost net: %d: "
                         "falling back on userspace virtio", -r);
            n->vhost_started = 0;
        }
    } else {
        vhost_net_stop(vdev, n->nic->ncs, queues);
        n->vhost_started = 0;
    }
}

static int virtio_net_set_vnet_endian_one(VirtIODevice *vdev,
                                          NetClientState *peer,
                                          bool enable)
{
    if (virtio_is_big_endian(vdev)) {
        return qemu_set_vnet_be(peer, enable);
    } else {
        return qemu_set_vnet_le(peer, enable);
    }
}

static bool virtio_net_set_vnet_endian(VirtIODevice *vdev, NetClientState *ncs,
                                       int queues, bool enable)
{
    int i;

    for (i = 0; i < queues; i++) {
        if (virtio_net_set_vnet_endian_one(vdev, ncs[i].peer, enable) < 0 &&
            enable) {
            while (--i >= 0) {
                virtio_net_set_vnet_endian_one(vdev, ncs[i].peer, false);
            }

            return true;
        }
    }

    return false;
}

static void virtio_net_vnet_endian_status(VirtIONet *n, uint8_t status)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(n);
    int queues = n->multiqueue ? n->max_queues : 1;

    if (virtio_net_started(n, status)) {
        /* Before using the device, we tell the network backend about the
         * endianness to use when parsing vnet headers. If the backend
         * can't do it, we fallback onto fixing the headers in the core
         * virtio-net code.
         */
        n->needs_vnet_hdr_swap = virtio_net_set_vnet_endian(vdev, n->nic->ncs,
                                                            queues, true);
    } else if (virtio_net_started(n, vdev->status)) {
        /* After using the device, we need to reset the network backend to
         * the default (guest native endianness), otherwise the guest may
         * lose network connectivity if it is rebooted into a different
         * endianness.
         */
        virtio_net_set_vnet_endian(vdev, n->nic->ncs, queues, false);
    }
}

static void virtio_net_drop_tx_queue_data(VirtIODevice *vdev, VirtQueue *vq)
{
    unsigned int dropped = virtqueue_drop_all(vq);
    if (dropped) {
        virtio_notify(vdev, vq);
    }
}

static void virtio_net_set_status(struct VirtIODevice *vdev, uint8_t status)
{
    VirtIONet *n = VIRTIO_NET(vdev);
    VirtIONetQueue *q;
    int i;
    uint8_t queue_status;

    virtio_net_vnet_endian_status(n, status);
    virtio_net_vhost_status(n, status);

    for (i = 0; i < n->max_queues; i++) {
        NetClientState *ncs = qemu_get_subqueue(n->nic, i);
        bool queue_started;
        q = &n->vqs[i];

        if ((!n->multiqueue && i != 0) || i >= n->curr_queues) {
            queue_status = 0;
        } else {
            queue_status = status;
        }
        queue_started =
            virtio_net_started(n, queue_status) && !n->vhost_started;

        if (queue_started) {
            qemu_flush_queued_packets(ncs);
        }

        if (!q->tx_waiting) {
            continue;
        }

        if (queue_started) {
            if (q->tx_timer) {
                timer_mod(q->tx_timer,
                               qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + n->tx_timeout);
            } else {
                qemu_bh_schedule(q->tx_bh);
            }
        } else {
            if (q->tx_timer) {
                timer_del(q->tx_timer);
            } else {
                qemu_bh_cancel(q->tx_bh);
            }
            if ((n->status & VIRTIO_NET_S_LINK_UP) == 0 &&
                (queue_status & VIRTIO_CONFIG_S_DRIVER_OK) &&
                vdev->vm_running) {
                /* if tx is waiting we are likely have some packets in tx queue
                 * and disabled notification */
                q->tx_waiting = 0;
                virtio_queue_set_notification(q->tx_vq, 1);
                virtio_net_drop_tx_queue_data(vdev, q->tx_vq);
            }
        }
    }
}

static void virtio_net_set_link_status(NetClientState *nc)
{
    VirtIONet *n = qemu_get_nic_opaque(nc);
    VirtIODevice *vdev = VIRTIO_DEVICE(n);
    uint16_t old_status = n->status;

    if (nc->link_down)
        n->status &= ~VIRTIO_NET_S_LINK_UP;
    else
        n->status |= VIRTIO_NET_S_LINK_UP;

    if (n->status != old_status)
        virtio_notify_config(vdev);

    virtio_net_set_status(vdev, vdev->status);
}

static void rxfilter_notify(NetClientState *nc)
{
    VirtIONet *n = qemu_get_nic_opaque(nc);

    if (nc->rxfilter_notify_enabled) {
        gchar *path = object_get_canonical_path(OBJECT(n->qdev));
        qapi_event_send_nic_rx_filter_changed(!!n->netclient_name,
                                              n->netclient_name, path);
        g_free(path);

        /* disable event notification to avoid events flooding */
        nc->rxfilter_notify_enabled = 0;
    }
}

static intList *get_vlan_table(VirtIONet *n)
{
    intList *list, *entry;
    int i, j;

    list = NULL;
    for (i = 0; i < MAX_VLAN >> 5; i++) {
        for (j = 0; n->vlans[i] && j <= 0x1f; j++) {
            if (n->vlans[i] & (1U << j)) {
                entry = g_malloc0(sizeof(*entry));
                entry->value = (i << 5) + j;
                entry->next = list;
                list = entry;
            }
        }
    }

    return list;
}

static RxFilterInfo *virtio_net_query_rxfilter(NetClientState *nc)
{
    VirtIONet *n = qemu_get_nic_opaque(nc);
    VirtIODevice *vdev = VIRTIO_DEVICE(n);
    RxFilterInfo *info;
    strList *str_list, *entry;
    int i;

    info = g_malloc0(sizeof(*info));
    info->name = g_strdup(nc->name);
    info->promiscuous = n->promisc;

    if (n->nouni) {
        info->unicast = RX_STATE_NONE;
    } else if (n->alluni) {
        info->unicast = RX_STATE_ALL;
    } else {
        info->unicast = RX_STATE_NORMAL;
    }

    if (n->nomulti) {
        info->multicast = RX_STATE_NONE;
    } else if (n->allmulti) {
        info->multicast = RX_STATE_ALL;
    } else {
        info->multicast = RX_STATE_NORMAL;
    }

    info->broadcast_allowed = n->nobcast;
    info->multicast_overflow = n->mac_table.multi_overflow;
    info->unicast_overflow = n->mac_table.uni_overflow;

    info->main_mac = qemu_mac_strdup_printf(n->mac);

    str_list = NULL;
    for (i = 0; i < n->mac_table.first_multi; i++) {
        entry = g_malloc0(sizeof(*entry));
        entry->value = qemu_mac_strdup_printf(n->mac_table.macs + i * ETH_ALEN);
        entry->next = str_list;
        str_list = entry;
    }
    info->unicast_table = str_list;

    str_list = NULL;
    for (i = n->mac_table.first_multi; i < n->mac_table.in_use; i++) {
        entry = g_malloc0(sizeof(*entry));
        entry->value = qemu_mac_strdup_printf(n->mac_table.macs + i * ETH_ALEN);
        entry->next = str_list;
        str_list = entry;
    }
    info->multicast_table = str_list;
    info->vlan_table = get_vlan_table(n);

    if (!virtio_vdev_has_feature(vdev, VIRTIO_NET_F_CTRL_VLAN)) {
        info->vlan = RX_STATE_ALL;
    } else if (!info->vlan_table) {
        info->vlan = RX_STATE_NONE;
    } else {
        info->vlan = RX_STATE_NORMAL;
    }

    /* enable event notification after query */
    nc->rxfilter_notify_enabled = 1;

    return info;
}

static void virtio_net_reset(VirtIODevice *vdev)
{
    VirtIONet *n = VIRTIO_NET(vdev);
    int i;

    /* Reset back to compatibility mode */
    n->promisc = 1;
    n->allmulti = 0;
    n->alluni = 0;
    n->nomulti = 0;
    n->nouni = 0;
    n->nobcast = 0;
    /* multiqueue is disabled by default */
    n->curr_queues = 1;
    timer_del(n->announce_timer.tm);
    n->announce_timer.round = 0;
    n->status &= ~VIRTIO_NET_S_ANNOUNCE;

    /* Flush any MAC and VLAN filter table state */
    n->mac_table.in_use = 0;
    n->mac_table.first_multi = 0;
    n->mac_table.multi_overflow = 0;
    n->mac_table.uni_overflow = 0;
    memset(n->mac_table.macs, 0, MAC_TABLE_ENTRIES * ETH_ALEN);
    memcpy(&n->mac[0], &n->nic->conf->macaddr, sizeof(n->mac));
    qemu_format_nic_info_str(qemu_get_queue(n->nic), n->mac);
    memset(n->vlans, 0, MAX_VLAN >> 3);

    /* Flush any async TX */
    for (i = 0;  i < n->max_queues; i++) {
        NetClientState *nc = qemu_get_subqueue(n->nic, i);

        if (nc->peer) {
            qemu_flush_or_purge_queued_packets(nc->peer, true);
            assert(!virtio_net_get_subqueue(nc)->async_tx.elem);
        }
    }
}

static void peer_test_vnet_hdr(VirtIONet *n)
{
    NetClientState *nc = qemu_get_queue(n->nic);
    if (!nc->peer) {
        return;
    }

    n->has_vnet_hdr = qemu_has_vnet_hdr(nc->peer);
}

static int peer_has_vnet_hdr(VirtIONet *n)
{
    return n->has_vnet_hdr;
}

static int peer_has_ufo(VirtIONet *n)
{
    if (!peer_has_vnet_hdr(n))
        return 0;

    n->has_ufo = qemu_has_ufo(qemu_get_queue(n->nic)->peer);

    return n->has_ufo;
}

static void virtio_net_set_mrg_rx_bufs(VirtIONet *n, int mergeable_rx_bufs,
                                       int version_1)
{
    int i;
    NetClientState *nc;

    n->mergeable_rx_bufs = mergeable_rx_bufs;

    if (version_1) {
        n->guest_hdr_len = sizeof(struct virtio_net_hdr_mrg_rxbuf);
    } else {
        n->guest_hdr_len = n->mergeable_rx_bufs ?
            sizeof(struct virtio_net_hdr_mrg_rxbuf) :
            sizeof(struct virtio_net_hdr);
    }

    for (i = 0; i < n->max_queues; i++) {
        nc = qemu_get_subqueue(n->nic, i);

        if (peer_has_vnet_hdr(n) &&
            qemu_has_vnet_hdr_len(nc->peer, n->guest_hdr_len)) {
            qemu_set_vnet_hdr_len(nc->peer, n->guest_hdr_len);
            n->host_hdr_len = n->guest_hdr_len;
        }
    }
}

static int virtio_net_max_tx_queue_size(VirtIONet *n)
{
    NetClientState *peer = n->nic_conf.peers.ncs[0];

    /*
     * Backends other than vhost-user don't support max queue size.
     */
    if (!peer) {
        return VIRTIO_NET_TX_QUEUE_DEFAULT_SIZE;
    }

    if (peer->info->type != NET_CLIENT_DRIVER_VHOST_USER) {
        return VIRTIO_NET_TX_QUEUE_DEFAULT_SIZE;
    }

    return VIRTQUEUE_MAX_SIZE;
}

static int peer_attach(VirtIONet *n, int index)
{
    NetClientState *nc = qemu_get_subqueue(n->nic, index);

    if (!nc->peer) {
        return 0;
    }

    if (nc->peer->info->type == NET_CLIENT_DRIVER_VHOST_USER) {
        vhost_set_vring_enable(nc->peer, 1);
    }

    if (nc->peer->info->type != NET_CLIENT_DRIVER_TAP) {
        return 0;
    }

    if (n->max_queues == 1) {
        return 0;
    }

    return tap_enable(nc->peer);
}

static int peer_detach(VirtIONet *n, int index)
{
    NetClientState *nc = qemu_get_subqueue(n->nic, index);

    if (!nc->peer) {
        return 0;
    }

    if (nc->peer->info->type == NET_CLIENT_DRIVER_VHOST_USER) {
        vhost_set_vring_enable(nc->peer, 0);
    }

    if (nc->peer->info->type !=  NET_CLIENT_DRIVER_TAP) {
        return 0;
    }

    return tap_disable(nc->peer);
}

static void virtio_net_set_queues(VirtIONet *n)
{
    int i;
    int r;

    if (n->nic->peer_deleted) {
        return;
    }

    for (i = 0; i < n->max_queues; i++) {
        if (i < n->curr_queues) {
            r = peer_attach(n, i);
            assert(!r);
        } else {
            r = peer_detach(n, i);
            assert(!r);
        }
    }
}

static void virtio_net_set_multiqueue(VirtIONet *n, int multiqueue);

static uint64_t virtio_net_get_features(VirtIODevice *vdev, uint64_t features,
                                        Error **errp)
{
    VirtIONet *n = VIRTIO_NET(vdev);
    NetClientState *nc = qemu_get_queue(n->nic);

    /* Firstly sync all virtio-net possible supported features */
    features |= n->host_features;

    virtio_add_feature(&features, VIRTIO_NET_F_MAC);

    if (!peer_has_vnet_hdr(n)) {
        virtio_clear_feature(&features, VIRTIO_NET_F_CSUM);
        virtio_clear_feature(&features, VIRTIO_NET_F_HOST_TSO4);
        virtio_clear_feature(&features, VIRTIO_NET_F_HOST_TSO6);
        virtio_clear_feature(&features, VIRTIO_NET_F_HOST_ECN);

        virtio_clear_feature(&features, VIRTIO_NET_F_GUEST_CSUM);
        virtio_clear_feature(&features, VIRTIO_NET_F_GUEST_TSO4);
        virtio_clear_feature(&features, VIRTIO_NET_F_GUEST_TSO6);
        virtio_clear_feature(&features, VIRTIO_NET_F_GUEST_ECN);
    }

    if (!peer_has_vnet_hdr(n) || !peer_has_ufo(n)) {
        virtio_clear_feature(&features, VIRTIO_NET_F_GUEST_UFO);
        virtio_clear_feature(&features, VIRTIO_NET_F_HOST_UFO);
    }

    if (!get_vhost_net(nc->peer)) {
        return features;
    }

    features = vhost_net_get_features(get_vhost_net(nc->peer), features);
    vdev->backend_features = features;

    if (n->mtu_bypass_backend &&
            (n->host_features & 1ULL << VIRTIO_NET_F_MTU)) {
        features |= (1ULL << VIRTIO_NET_F_MTU);
    }

    return features;
}

static uint64_t virtio_net_bad_features(VirtIODevice *vdev)
{
    uint64_t features = 0;

    /* Linux kernel 2.6.25.  It understood MAC (as everyone must),
     * but also these: */
    virtio_add_feature(&features, VIRTIO_NET_F_MAC);
    virtio_add_feature(&features, VIRTIO_NET_F_CSUM);
    virtio_add_feature(&features, VIRTIO_NET_F_HOST_TSO4);
    virtio_add_feature(&features, VIRTIO_NET_F_HOST_TSO6);
    virtio_add_feature(&features, VIRTIO_NET_F_HOST_ECN);

    return features;
}

static void virtio_net_apply_guest_offloads(VirtIONet *n)
{
    qemu_set_offload(qemu_get_queue(n->nic)->peer,
            !!(n->curr_guest_offloads & (1ULL << VIRTIO_NET_F_GUEST_CSUM)),
            !!(n->curr_guest_offloads & (1ULL << VIRTIO_NET_F_GUEST_TSO4)),
            !!(n->curr_guest_offloads & (1ULL << VIRTIO_NET_F_GUEST_TSO6)),
            !!(n->curr_guest_offloads & (1ULL << VIRTIO_NET_F_GUEST_ECN)),
            !!(n->curr_guest_offloads & (1ULL << VIRTIO_NET_F_GUEST_UFO)));
}

static uint64_t virtio_net_guest_offloads_by_features(uint32_t features)
{
    static const uint64_t guest_offloads_mask =
        (1ULL << VIRTIO_NET_F_GUEST_CSUM) |
        (1ULL << VIRTIO_NET_F_GUEST_TSO4) |
        (1ULL << VIRTIO_NET_F_GUEST_TSO6) |
        (1ULL << VIRTIO_NET_F_GUEST_ECN)  |
        (1ULL << VIRTIO_NET_F_GUEST_UFO);

    return guest_offloads_mask & features;
}

static inline uint64_t virtio_net_supported_guest_offloads(VirtIONet *n)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(n);
    return virtio_net_guest_offloads_by_features(vdev->guest_features);
}

static void virtio_net_set_features(VirtIODevice *vdev, uint64_t features)
{
    VirtIONet *n = VIRTIO_NET(vdev);
    int i;

    if (n->mtu_bypass_backend &&
            !virtio_has_feature(vdev->backend_features, VIRTIO_NET_F_MTU)) {
        features &= ~(1ULL << VIRTIO_NET_F_MTU);
    }

    virtio_net_set_multiqueue(n,
                              virtio_has_feature(features, VIRTIO_NET_F_MQ));

    virtio_net_set_mrg_rx_bufs(n,
                               virtio_has_feature(features,
                                                  VIRTIO_NET_F_MRG_RXBUF),
                               virtio_has_feature(features,
                                                  VIRTIO_F_VERSION_1));

    n->rsc4_enabled = virtio_has_feature(features, VIRTIO_NET_F_RSC_EXT) &&
        virtio_has_feature(features, VIRTIO_NET_F_GUEST_TSO4);
    n->rsc6_enabled = virtio_has_feature(features, VIRTIO_NET_F_RSC_EXT) &&
        virtio_has_feature(features, VIRTIO_NET_F_GUEST_TSO6);

    if (n->has_vnet_hdr) {
        n->curr_guest_offloads =
            virtio_net_guest_offloads_by_features(features);
        virtio_net_apply_guest_offloads(n);
    }

    for (i = 0;  i < n->max_queues; i++) {
        NetClientState *nc = qemu_get_subqueue(n->nic, i);

        if (!get_vhost_net(nc->peer)) {
            continue;
        }
        vhost_net_ack_features(get_vhost_net(nc->peer), features);
    }

    if (virtio_has_feature(features, VIRTIO_NET_F_CTRL_VLAN)) {
        memset(n->vlans, 0, MAX_VLAN >> 3);
    } else {
        memset(n->vlans, 0xff, MAX_VLAN >> 3);
    }
}

static int virtio_net_handle_rx_mode(VirtIONet *n, uint8_t cmd,
                                     struct iovec *iov, unsigned int iov_cnt)
{
    uint8_t on;
    size_t s;
    NetClientState *nc = qemu_get_queue(n->nic);

    s = iov_to_buf(iov, iov_cnt, 0, &on, sizeof(on));
    if (s != sizeof(on)) {
        return VIRTIO_NET_ERR;
    }

    if (cmd == VIRTIO_NET_CTRL_RX_PROMISC) {
        n->promisc = on;
    } else if (cmd == VIRTIO_NET_CTRL_RX_ALLMULTI) {
        n->allmulti = on;
    } else if (cmd == VIRTIO_NET_CTRL_RX_ALLUNI) {
        n->alluni = on;
    } else if (cmd == VIRTIO_NET_CTRL_RX_NOMULTI) {
        n->nomulti = on;
    } else if (cmd == VIRTIO_NET_CTRL_RX_NOUNI) {
        n->nouni = on;
    } else if (cmd == VIRTIO_NET_CTRL_RX_NOBCAST) {
        n->nobcast = on;
    } else {
        return VIRTIO_NET_ERR;
    }

    rxfilter_notify(nc);

    return VIRTIO_NET_OK;
}

static int virtio_net_handle_offloads(VirtIONet *n, uint8_t cmd,
                                     struct iovec *iov, unsigned int iov_cnt)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(n);
    uint64_t offloads;
    size_t s;

    if (!virtio_vdev_has_feature(vdev, VIRTIO_NET_F_CTRL_GUEST_OFFLOADS)) {
        return VIRTIO_NET_ERR;
    }

    s = iov_to_buf(iov, iov_cnt, 0, &offloads, sizeof(offloads));
    if (s != sizeof(offloads)) {
        return VIRTIO_NET_ERR;
    }

    if (cmd == VIRTIO_NET_CTRL_GUEST_OFFLOADS_SET) {
        uint64_t supported_offloads;

        offloads = virtio_ldq_p(vdev, &offloads);

        if (!n->has_vnet_hdr) {
            return VIRTIO_NET_ERR;
        }

        n->rsc4_enabled = virtio_has_feature(offloads, VIRTIO_NET_F_RSC_EXT) &&
            virtio_has_feature(offloads, VIRTIO_NET_F_GUEST_TSO4);
        n->rsc6_enabled = virtio_has_feature(offloads, VIRTIO_NET_F_RSC_EXT) &&
            virtio_has_feature(offloads, VIRTIO_NET_F_GUEST_TSO6);
        virtio_clear_feature(&offloads, VIRTIO_NET_F_RSC_EXT);

        supported_offloads = virtio_net_supported_guest_offloads(n);
        if (offloads & ~supported_offloads) {
            return VIRTIO_NET_ERR;
        }

        n->curr_guest_offloads = offloads;
        virtio_net_apply_guest_offloads(n);

        return VIRTIO_NET_OK;
    } else {
        return VIRTIO_NET_ERR;
    }
}

static int virtio_net_handle_mac(VirtIONet *n, uint8_t cmd,
                                 struct iovec *iov, unsigned int iov_cnt)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(n);
    struct virtio_net_ctrl_mac mac_data;
    size_t s;
    NetClientState *nc = qemu_get_queue(n->nic);

    if (cmd == VIRTIO_NET_CTRL_MAC_ADDR_SET) {
        if (iov_size(iov, iov_cnt) != sizeof(n->mac)) {
            return VIRTIO_NET_ERR;
        }
        s = iov_to_buf(iov, iov_cnt, 0, &n->mac, sizeof(n->mac));
        assert(s == sizeof(n->mac));
        qemu_format_nic_info_str(qemu_get_queue(n->nic), n->mac);
        rxfilter_notify(nc);

        return VIRTIO_NET_OK;
    }

    if (cmd != VIRTIO_NET_CTRL_MAC_TABLE_SET) {
        return VIRTIO_NET_ERR;
    }

    int in_use = 0;
    int first_multi = 0;
    uint8_t uni_overflow = 0;
    uint8_t multi_overflow = 0;
    uint8_t *macs = g_malloc0(MAC_TABLE_ENTRIES * ETH_ALEN);

    s = iov_to_buf(iov, iov_cnt, 0, &mac_data.entries,
                   sizeof(mac_data.entries));
    mac_data.entries = virtio_ldl_p(vdev, &mac_data.entries);
    if (s != sizeof(mac_data.entries)) {
        goto error;
    }
    iov_discard_front(&iov, &iov_cnt, s);

    if (mac_data.entries * ETH_ALEN > iov_size(iov, iov_cnt)) {
        goto error;
    }

    if (mac_data.entries <= MAC_TABLE_ENTRIES) {
        s = iov_to_buf(iov, iov_cnt, 0, macs,
                       mac_data.entries * ETH_ALEN);
        if (s != mac_data.entries * ETH_ALEN) {
            goto error;
        }
        in_use += mac_data.entries;
    } else {
        uni_overflow = 1;
    }

    iov_discard_front(&iov, &iov_cnt, mac_data.entries * ETH_ALEN);

    first_multi = in_use;

    s = iov_to_buf(iov, iov_cnt, 0, &mac_data.entries,
                   sizeof(mac_data.entries));
    mac_data.entries = virtio_ldl_p(vdev, &mac_data.entries);
    if (s != sizeof(mac_data.entries)) {
        goto error;
    }

    iov_discard_front(&iov, &iov_cnt, s);

    if (mac_data.entries * ETH_ALEN != iov_size(iov, iov_cnt)) {
        goto error;
    }

    if (mac_data.entries <= MAC_TABLE_ENTRIES - in_use) {
        s = iov_to_buf(iov, iov_cnt, 0, &macs[in_use * ETH_ALEN],
                       mac_data.entries * ETH_ALEN);
        if (s != mac_data.entries * ETH_ALEN) {
            goto error;
        }
        in_use += mac_data.entries;
    } else {
        multi_overflow = 1;
    }

    n->mac_table.in_use = in_use;
    n->mac_table.first_multi = first_multi;
    n->mac_table.uni_overflow = uni_overflow;
    n->mac_table.multi_overflow = multi_overflow;
    memcpy(n->mac_table.macs, macs, MAC_TABLE_ENTRIES * ETH_ALEN);
    g_free(macs);
    rxfilter_notify(nc);

    return VIRTIO_NET_OK;

error:
    g_free(macs);
    return VIRTIO_NET_ERR;
}

static int virtio_net_handle_vlan_table(VirtIONet *n, uint8_t cmd,
                                        struct iovec *iov, unsigned int iov_cnt)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(n);
    uint16_t vid;
    size_t s;
    NetClientState *nc = qemu_get_queue(n->nic);

    s = iov_to_buf(iov, iov_cnt, 0, &vid, sizeof(vid));
    vid = virtio_lduw_p(vdev, &vid);
    if (s != sizeof(vid)) {
        return VIRTIO_NET_ERR;
    }

    if (vid >= MAX_VLAN)
        return VIRTIO_NET_ERR;

    if (cmd == VIRTIO_NET_CTRL_VLAN_ADD)
        n->vlans[vid >> 5] |= (1U << (vid & 0x1f));
    else if (cmd == VIRTIO_NET_CTRL_VLAN_DEL)
        n->vlans[vid >> 5] &= ~(1U << (vid & 0x1f));
    else
        return VIRTIO_NET_ERR;

    rxfilter_notify(nc);

    return VIRTIO_NET_OK;
}

static int virtio_net_handle_announce(VirtIONet *n, uint8_t cmd,
                                      struct iovec *iov, unsigned int iov_cnt)
{
    trace_virtio_net_handle_announce(n->announce_timer.round);
    if (cmd == VIRTIO_NET_CTRL_ANNOUNCE_ACK &&
        n->status & VIRTIO_NET_S_ANNOUNCE) {
        n->status &= ~VIRTIO_NET_S_ANNOUNCE;
        if (n->announce_timer.round) {
            qemu_announce_timer_step(&n->announce_timer);
        }
        return VIRTIO_NET_OK;
    } else {
        return VIRTIO_NET_ERR;
    }
}

static int virtio_net_handle_mq(VirtIONet *n, uint8_t cmd,
                                struct iovec *iov, unsigned int iov_cnt)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(n);
    struct virtio_net_ctrl_mq mq;
    size_t s;
    uint16_t queues;

    s = iov_to_buf(iov, iov_cnt, 0, &mq, sizeof(mq));
    if (s != sizeof(mq)) {
        return VIRTIO_NET_ERR;
    }

    if (cmd != VIRTIO_NET_CTRL_MQ_VQ_PAIRS_SET) {
        return VIRTIO_NET_ERR;
    }

    queues = virtio_lduw_p(vdev, &mq.virtqueue_pairs);

    if (queues < VIRTIO_NET_CTRL_MQ_VQ_PAIRS_MIN ||
        queues > VIRTIO_NET_CTRL_MQ_VQ_PAIRS_MAX ||
        queues > n->max_queues ||
        !n->multiqueue) {
        return VIRTIO_NET_ERR;
    }

    n->curr_queues = queues;
    /* stop the backend before changing the number of queues to avoid handling a
     * disabled queue */
    virtio_net_set_status(vdev, vdev->status);
    virtio_net_set_queues(n);

    return VIRTIO_NET_OK;
}

static void virtio_net_handle_ctrl(VirtIODevice *vdev, VirtQueue *vq)
{
    VirtIONet *n = VIRTIO_NET(vdev);
    struct virtio_net_ctrl_hdr ctrl;
    virtio_net_ctrl_ack status = VIRTIO_NET_ERR;
    VirtQueueElement *elem;
    size_t s;
    struct iovec *iov, *iov2;
    unsigned int iov_cnt;

    for (;;) {
        elem = virtqueue_pop(vq, sizeof(VirtQueueElement));
        if (!elem) {
            break;
        }
        if (iov_size(elem->in_sg, elem->in_num) < sizeof(status) ||
            iov_size(elem->out_sg, elem->out_num) < sizeof(ctrl)) {
            virtio_error(vdev, "virtio-net ctrl missing headers");
            virtqueue_detach_element(vq, elem, 0);
            g_free(elem);
            break;
        }

        iov_cnt = elem->out_num;
        iov2 = iov = g_memdup(elem->out_sg, sizeof(struct iovec) * elem->out_num);
        s = iov_to_buf(iov, iov_cnt, 0, &ctrl, sizeof(ctrl));
        iov_discard_front(&iov, &iov_cnt, sizeof(ctrl));
        if (s != sizeof(ctrl)) {
            status = VIRTIO_NET_ERR;
        } else if (ctrl.class == VIRTIO_NET_CTRL_RX) {
            status = virtio_net_handle_rx_mode(n, ctrl.cmd, iov, iov_cnt);
        } else if (ctrl.class == VIRTIO_NET_CTRL_MAC) {
            status = virtio_net_handle_mac(n, ctrl.cmd, iov, iov_cnt);
        } else if (ctrl.class == VIRTIO_NET_CTRL_VLAN) {
            status = virtio_net_handle_vlan_table(n, ctrl.cmd, iov, iov_cnt);
        } else if (ctrl.class == VIRTIO_NET_CTRL_ANNOUNCE) {
            status = virtio_net_handle_announce(n, ctrl.cmd, iov, iov_cnt);
        } else if (ctrl.class == VIRTIO_NET_CTRL_MQ) {
            status = virtio_net_handle_mq(n, ctrl.cmd, iov, iov_cnt);
        } else if (ctrl.class == VIRTIO_NET_CTRL_GUEST_OFFLOADS) {
            status = virtio_net_handle_offloads(n, ctrl.cmd, iov, iov_cnt);
        }

        s = iov_from_buf(elem->in_sg, elem->in_num, 0, &status, sizeof(status));
        assert(s == sizeof(status));

        virtqueue_push(vq, elem, sizeof(status));
        virtio_notify(vdev, vq);
        g_free(iov2);
        g_free(elem);
    }
}

/* RX */

static void virtio_net_handle_rx(VirtIODevice *vdev, VirtQueue *vq)
{
    VirtIONet *n = VIRTIO_NET(vdev);
    int queue_index = vq2q(virtio_get_queue_index(vq));

    qemu_flush_queued_packets(qemu_get_subqueue(n->nic, queue_index));
}

static int virtio_net_can_receive(NetClientState *nc)
{
    VirtIONet *n = qemu_get_nic_opaque(nc);
    VirtIODevice *vdev = VIRTIO_DEVICE(n);
    VirtIONetQueue *q = virtio_net_get_subqueue(nc);

    if (!vdev->vm_running) {
        return 0;
    }

    if (nc->queue_index >= n->curr_queues) {
        return 0;
    }

    if (!virtio_queue_ready(q->rx_vq) ||
        !(vdev->status & VIRTIO_CONFIG_S_DRIVER_OK)) {
        return 0;
    }

    return 1;
}

static int virtio_net_has_buffers(VirtIONetQueue *q, int bufsize)
{
    VirtIONet *n = q->n;
    if (virtio_queue_empty(q->rx_vq) ||
        (n->mergeable_rx_bufs &&
         !virtqueue_avail_bytes(q->rx_vq, bufsize, 0))) {
        virtio_queue_set_notification(q->rx_vq, 1);

        /* To avoid a race condition where the guest has made some buffers
         * available after the above check but before notification was
         * enabled, check for available buffers again.
         */
        if (virtio_queue_empty(q->rx_vq) ||
            (n->mergeable_rx_bufs &&
             !virtqueue_avail_bytes(q->rx_vq, bufsize, 0))) {
            return 0;
        }
    }

    virtio_queue_set_notification(q->rx_vq, 0);
    return 1;
}

static void virtio_net_hdr_swap(VirtIODevice *vdev, struct virtio_net_hdr *hdr)
{
    virtio_tswap16s(vdev, &hdr->hdr_len);
    virtio_tswap16s(vdev, &hdr->gso_size);
    virtio_tswap16s(vdev, &hdr->csum_start);
    virtio_tswap16s(vdev, &hdr->csum_offset);
}

/* dhclient uses AF_PACKET but doesn't pass auxdata to the kernel so
 * it never finds out that the packets don't have valid checksums.  This
 * causes dhclient to get upset.  Fedora's carried a patch for ages to
 * fix this with Xen but it hasn't appeared in an upstream release of
 * dhclient yet.
 *
 * To avoid breaking existing guests, we catch udp packets and add
 * checksums.  This is terrible but it's better than hacking the guest
 * kernels.
 *
 * N.B. if we introduce a zero-copy API, this operation is no longer free so
 * we should provide a mechanism to disable it to avoid polluting the host
 * cache.
 */
static void work_around_broken_dhclient(struct virtio_net_hdr *hdr,
                                        uint8_t *buf, size_t size)
{
    if ((hdr->flags & VIRTIO_NET_HDR_F_NEEDS_CSUM) && /* missing csum */
        (size > 27 && size < 1500) && /* normal sized MTU */
        (buf[12] == 0x08 && buf[13] == 0x00) && /* ethertype == IPv4 */
        (buf[23] == 17) && /* ip.protocol == UDP */
        (buf[34] == 0 && buf[35] == 67)) { /* udp.srcport == bootps */
        net_checksum_calculate(buf, size);
        hdr->flags &= ~VIRTIO_NET_HDR_F_NEEDS_CSUM;
    }
}

static void receive_header(VirtIONet *n, const struct iovec *iov, int iov_cnt,
                           const void *buf, size_t size)
{
    if (n->has_vnet_hdr) {
        /* FIXME this cast is evil */
        void *wbuf = (void *)buf;
        work_around_broken_dhclient(wbuf, wbuf + n->host_hdr_len,
                                    size - n->host_hdr_len);

        if (n->needs_vnet_hdr_swap) {
            virtio_net_hdr_swap(VIRTIO_DEVICE(n), wbuf);
        }
        iov_from_buf(iov, iov_cnt, 0, buf, sizeof(struct virtio_net_hdr));
    } else {
        struct virtio_net_hdr hdr = {
            .flags = 0,
            .gso_type = VIRTIO_NET_HDR_GSO_NONE
        };
        iov_from_buf(iov, iov_cnt, 0, &hdr, sizeof hdr);
    }
}

static int receive_filter(VirtIONet *n, const uint8_t *buf, int size)
{
    static const uint8_t bcast[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    static const uint8_t vlan[] = {0x81, 0x00};
    uint8_t *ptr = (uint8_t *)buf;
    int i;

    if (n->promisc)
        return 1;

    ptr += n->host_hdr_len;

    if (!memcmp(&ptr[12], vlan, sizeof(vlan))) {
        int vid = lduw_be_p(ptr + 14) & 0xfff;
        if (!(n->vlans[vid >> 5] & (1U << (vid & 0x1f))))
            return 0;
    }

    if (ptr[0] & 1) { // multicast
        if (!memcmp(ptr, bcast, sizeof(bcast))) {
            return !n->nobcast;
        } else if (n->nomulti) {
            return 0;
        } else if (n->allmulti || n->mac_table.multi_overflow) {
            return 1;
        }

        for (i = n->mac_table.first_multi; i < n->mac_table.in_use; i++) {
            if (!memcmp(ptr, &n->mac_table.macs[i * ETH_ALEN], ETH_ALEN)) {
                return 1;
            }
        }
    } else { // unicast
        if (n->nouni) {
            return 0;
        } else if (n->alluni || n->mac_table.uni_overflow) {
            return 1;
        } else if (!memcmp(ptr, n->mac, ETH_ALEN)) {
            return 1;
        }

        for (i = 0; i < n->mac_table.first_multi; i++) {
            if (!memcmp(ptr, &n->mac_table.macs[i * ETH_ALEN], ETH_ALEN)) {
                return 1;
            }
        }
    }

    return 0;
}

static ssize_t virtio_net_receive_rcu(NetClientState *nc, const uint8_t *buf,
                                      size_t size)
{
    VirtIONet *n = qemu_get_nic_opaque(nc);
    VirtIONetQueue *q = virtio_net_get_subqueue(nc);
    VirtIODevice *vdev = VIRTIO_DEVICE(n);
    struct iovec mhdr_sg[VIRTQUEUE_MAX_SIZE];
    struct virtio_net_hdr_mrg_rxbuf mhdr;
    unsigned mhdr_cnt = 0;
    size_t offset, i, guest_offset;

    if (!virtio_net_can_receive(nc)) {
        return -1;
    }

    /* hdr_len refers to the header we supply to the guest */
    if (!virtio_net_has_buffers(q, size + n->guest_hdr_len - n->host_hdr_len)) {
        return 0;
    }

    if (!receive_filter(n, buf, size))
        return size;

    offset = i = 0;

    while (offset < size) {
        VirtQueueElement *elem;
        int len, total;
        const struct iovec *sg;

        total = 0;

        elem = virtqueue_pop(q->rx_vq, sizeof(VirtQueueElement));
        if (!elem) {
            if (i) {
                virtio_error(vdev, "virtio-net unexpected empty queue: "
                             "i %zd mergeable %d offset %zd, size %zd, "
                             "guest hdr len %zd, host hdr len %zd "
                             "guest features 0x%" PRIx64,
                             i, n->mergeable_rx_bufs, offset, size,
                             n->guest_hdr_len, n->host_hdr_len,
                             vdev->guest_features);
            }
            return -1;
        }

        if (elem->in_num < 1) {
            virtio_error(vdev,
                         "virtio-net receive queue contains no in buffers");
            virtqueue_detach_element(q->rx_vq, elem, 0);
            g_free(elem);
            return -1;
        }

        sg = elem->in_sg;
        if (i == 0) {
            assert(offset == 0);
            if (n->mergeable_rx_bufs) {
                mhdr_cnt = iov_copy(mhdr_sg, ARRAY_SIZE(mhdr_sg),
                                    sg, elem->in_num,
                                    offsetof(typeof(mhdr), num_buffers),
                                    sizeof(mhdr.num_buffers));
            }

            receive_header(n, sg, elem->in_num, buf, size);
            offset = n->host_hdr_len;
            total += n->guest_hdr_len;
            guest_offset = n->guest_hdr_len;
        } else {
            guest_offset = 0;
        }

        /* copy in packet.  ugh */
        len = iov_from_buf(sg, elem->in_num, guest_offset,
                           buf + offset, size - offset);
        total += len;
        offset += len;
        /* If buffers can't be merged, at this point we
         * must have consumed the complete packet.
         * Otherwise, drop it. */
        if (!n->mergeable_rx_bufs && offset < size) {
            virtqueue_unpop(q->rx_vq, elem, total);
            g_free(elem);
            return size;
        }

        /* signal other side */
        virtqueue_fill(q->rx_vq, elem, total, i++);
        g_free(elem);
    }

    if (mhdr_cnt) {
        virtio_stw_p(vdev, &mhdr.num_buffers, i);
        iov_from_buf(mhdr_sg, mhdr_cnt,
                     0,
                     &mhdr.num_buffers, sizeof mhdr.num_buffers);
    }

    virtqueue_flush(q->rx_vq, i);
    virtio_notify(vdev, q->rx_vq);

    return size;
}

static ssize_t virtio_net_do_receive(NetClientState *nc, const uint8_t *buf,
                                  size_t size)
{
    ssize_t r;

    rcu_read_lock();
    r = virtio_net_receive_rcu(nc, buf, size);
    rcu_read_unlock();
    return r;
}

static void virtio_net_rsc_extract_unit4(VirtioNetRscChain *chain,
                                         const uint8_t *buf,
                                         VirtioNetRscUnit *unit)
{
    uint16_t ip_hdrlen;
    struct ip_header *ip;

    ip = (struct ip_header *)(buf + chain->n->guest_hdr_len
                              + sizeof(struct eth_header));
    unit->ip = (void *)ip;
    ip_hdrlen = (ip->ip_ver_len & 0xF) << 2;
    unit->ip_plen = &ip->ip_len;
    unit->tcp = (struct tcp_header *)(((uint8_t *)unit->ip) + ip_hdrlen);
    unit->tcp_hdrlen = (htons(unit->tcp->th_offset_flags) & 0xF000) >> 10;
    unit->payload = htons(*unit->ip_plen) - ip_hdrlen - unit->tcp_hdrlen;
}

static void virtio_net_rsc_extract_unit6(VirtioNetRscChain *chain,
                                         const uint8_t *buf,
                                         VirtioNetRscUnit *unit)
{
    struct ip6_header *ip6;

    ip6 = (struct ip6_header *)(buf + chain->n->guest_hdr_len
                                 + sizeof(struct eth_header));
    unit->ip = ip6;
    unit->ip_plen = &(ip6->ip6_ctlun.ip6_un1.ip6_un1_plen);
    unit->tcp = (struct tcp_header *)(((uint8_t *)unit->ip)\
                                        + sizeof(struct ip6_header));
    unit->tcp_hdrlen = (htons(unit->tcp->th_offset_flags) & 0xF000) >> 10;

    /* There is a difference between payload lenght in ipv4 and v6,
       ip header is excluded in ipv6 */
    unit->payload = htons(*unit->ip_plen) - unit->tcp_hdrlen;
}

static size_t virtio_net_rsc_drain_seg(VirtioNetRscChain *chain,
                                       VirtioNetRscSeg *seg)
{
    int ret;
    struct virtio_net_hdr *h;

    h = (struct virtio_net_hdr *)seg->buf;
    h->flags = 0;
    h->gso_type = VIRTIO_NET_HDR_GSO_NONE;

    if (seg->is_coalesced) {
        *virtio_net_rsc_ext_num_packets(h) = seg->packets;
        *virtio_net_rsc_ext_num_dupacks(h) = seg->dup_ack;
        h->flags = VIRTIO_NET_HDR_F_RSC_INFO;
        if (chain->proto == ETH_P_IP) {
            h->gso_type = VIRTIO_NET_HDR_GSO_TCPV4;
        } else {
            h->gso_type = VIRTIO_NET_HDR_GSO_TCPV6;
        }
    }

    ret = virtio_net_do_receive(seg->nc, seg->buf, seg->size);
    QTAILQ_REMOVE(&chain->buffers, seg, next);
    g_free(seg->buf);
    g_free(seg);

    return ret;
}

static void virtio_net_rsc_purge(void *opq)
{
    VirtioNetRscSeg *seg, *rn;
    VirtioNetRscChain *chain = (VirtioNetRscChain *)opq;

    QTAILQ_FOREACH_SAFE(seg, &chain->buffers, next, rn) {
        if (virtio_net_rsc_drain_seg(chain, seg) == 0) {
            chain->stat.purge_failed++;
            continue;
        }
    }

    chain->stat.timer++;
    if (!QTAILQ_EMPTY(&chain->buffers)) {
        timer_mod(chain->drain_timer,
              qemu_clock_get_ns(QEMU_CLOCK_HOST) + chain->n->rsc_timeout);
    }
}

static void virtio_net_rsc_cleanup(VirtIONet *n)
{
    VirtioNetRscChain *chain, *rn_chain;
    VirtioNetRscSeg *seg, *rn_seg;

    QTAILQ_FOREACH_SAFE(chain, &n->rsc_chains, next, rn_chain) {
        QTAILQ_FOREACH_SAFE(seg, &chain->buffers, next, rn_seg) {
            QTAILQ_REMOVE(&chain->buffers, seg, next);
            g_free(seg->buf);
            g_free(seg);
        }

        timer_del(chain->drain_timer);
        timer_free(chain->drain_timer);
        QTAILQ_REMOVE(&n->rsc_chains, chain, next);
        g_free(chain);
    }
}

static void virtio_net_rsc_cache_buf(VirtioNetRscChain *chain,
                                     NetClientState *nc,
                                     const uint8_t *buf, size_t size)
{
    uint16_t hdr_len;
    VirtioNetRscSeg *seg;

    hdr_len = chain->n->guest_hdr_len;
    seg = g_malloc(sizeof(VirtioNetRscSeg));
    seg->buf = g_malloc(hdr_len + sizeof(struct eth_header)
        + sizeof(struct ip6_header) + VIRTIO_NET_MAX_TCP_PAYLOAD);
    memcpy(seg->buf, buf, size);
    seg->size = size;
    seg->packets = 1;
    seg->dup_ack = 0;
    seg->is_coalesced = 0;
    seg->nc = nc;

    QTAILQ_INSERT_TAIL(&chain->buffers, seg, next);
    chain->stat.cache++;

    switch (chain->proto) {
    case ETH_P_IP:
        virtio_net_rsc_extract_unit4(chain, seg->buf, &seg->unit);
        break;
    case ETH_P_IPV6:
        virtio_net_rsc_extract_unit6(chain, seg->buf, &seg->unit);
        break;
    default:
        g_assert_not_reached();
    }
}

static int32_t virtio_net_rsc_handle_ack(VirtioNetRscChain *chain,
                                         VirtioNetRscSeg *seg,
                                         const uint8_t *buf,
                                         struct tcp_header *n_tcp,
                                         struct tcp_header *o_tcp)
{
    uint32_t nack, oack;
    uint16_t nwin, owin;

    nack = htonl(n_tcp->th_ack);
    nwin = htons(n_tcp->th_win);
    oack = htonl(o_tcp->th_ack);
    owin = htons(o_tcp->th_win);

    if ((nack - oack) >= VIRTIO_NET_MAX_TCP_PAYLOAD) {
        chain->stat.ack_out_of_win++;
        return RSC_FINAL;
    } else if (nack == oack) {
        /* duplicated ack or window probe */
        if (nwin == owin) {
            /* duplicated ack, add dup ack count due to whql test up to 1 */
            chain->stat.dup_ack++;
            return RSC_FINAL;
        } else {
            /* Coalesce window update */
            o_tcp->th_win = n_tcp->th_win;
            chain->stat.win_update++;
            return RSC_COALESCE;
        }
    } else {
        /* pure ack, go to 'C', finalize*/
        chain->stat.pure_ack++;
        return RSC_FINAL;
    }
}

static int32_t virtio_net_rsc_coalesce_data(VirtioNetRscChain *chain,
                                            VirtioNetRscSeg *seg,
                                            const uint8_t *buf,
                                            VirtioNetRscUnit *n_unit)
{
    void *data;
    uint16_t o_ip_len;
    uint32_t nseq, oseq;
    VirtioNetRscUnit *o_unit;

    o_unit = &seg->unit;
    o_ip_len = htons(*o_unit->ip_plen);
    nseq = htonl(n_unit->tcp->th_seq);
    oseq = htonl(o_unit->tcp->th_seq);

    /* out of order or retransmitted. */
    if ((nseq - oseq) > VIRTIO_NET_MAX_TCP_PAYLOAD) {
        chain->stat.data_out_of_win++;
        return RSC_FINAL;
    }

    data = ((uint8_t *)n_unit->tcp) + n_unit->tcp_hdrlen;
    if (nseq == oseq) {
        if ((o_unit->payload == 0) && n_unit->payload) {
            /* From no payload to payload, normal case, not a dup ack or etc */
            chain->stat.data_after_pure_ack++;
            goto coalesce;
        } else {
            return virtio_net_rsc_handle_ack(chain, seg, buf,
                                             n_unit->tcp, o_unit->tcp);
        }
    } else if ((nseq - oseq) != o_unit->payload) {
        /* Not a consistent packet, out of order */
        chain->stat.data_out_of_order++;
        return RSC_FINAL;
    } else {
coalesce:
        if ((o_ip_len + n_unit->payload) > chain->max_payload) {
            chain->stat.over_size++;
            return RSC_FINAL;
        }

        /* Here comes the right data, the payload length in v4/v6 is different,
           so use the field value to update and record the new data len */
        o_unit->payload += n_unit->payload; /* update new data len */

        /* update field in ip header */
        *o_unit->ip_plen = htons(o_ip_len + n_unit->payload);

        /* Bring 'PUSH' big, the whql test guide says 'PUSH' can be coalesced
           for windows guest, while this may change the behavior for linux
           guest (only if it uses RSC feature). */
        o_unit->tcp->th_offset_flags = n_unit->tcp->th_offset_flags;

        o_unit->tcp->th_ack = n_unit->tcp->th_ack;
        o_unit->tcp->th_win = n_unit->tcp->th_win;

        memmove(seg->buf + seg->size, data, n_unit->payload);
        seg->size += n_unit->payload;
        seg->packets++;
        chain->stat.coalesced++;
        return RSC_COALESCE;
    }
}

static int32_t virtio_net_rsc_coalesce4(VirtioNetRscChain *chain,
                                        VirtioNetRscSeg *seg,
                                        const uint8_t *buf, size_t size,
                                        VirtioNetRscUnit *unit)
{
    struct ip_header *ip1, *ip2;

    ip1 = (struct ip_header *)(unit->ip);
    ip2 = (struct ip_header *)(seg->unit.ip);
    if ((ip1->ip_src ^ ip2->ip_src) || (ip1->ip_dst ^ ip2->ip_dst)
        || (unit->tcp->th_sport ^ seg->unit.tcp->th_sport)
        || (unit->tcp->th_dport ^ seg->unit.tcp->th_dport)) {
        chain->stat.no_match++;
        return RSC_NO_MATCH;
    }

    return virtio_net_rsc_coalesce_data(chain, seg, buf, unit);
}

static int32_t virtio_net_rsc_coalesce6(VirtioNetRscChain *chain,
                                        VirtioNetRscSeg *seg,
                                        const uint8_t *buf, size_t size,
                                        VirtioNetRscUnit *unit)
{
    struct ip6_header *ip1, *ip2;

    ip1 = (struct ip6_header *)(unit->ip);
    ip2 = (struct ip6_header *)(seg->unit.ip);
    if (memcmp(&ip1->ip6_src, &ip2->ip6_src, sizeof(struct in6_address))
        || memcmp(&ip1->ip6_dst, &ip2->ip6_dst, sizeof(struct in6_address))
        || (unit->tcp->th_sport ^ seg->unit.tcp->th_sport)
        || (unit->tcp->th_dport ^ seg->unit.tcp->th_dport)) {
            chain->stat.no_match++;
            return RSC_NO_MATCH;
    }

    return virtio_net_rsc_coalesce_data(chain, seg, buf, unit);
}

/* Packets with 'SYN' should bypass, other flag should be sent after drain
 * to prevent out of order */
static int virtio_net_rsc_tcp_ctrl_check(VirtioNetRscChain *chain,
                                         struct tcp_header *tcp)
{
    uint16_t tcp_hdr;
    uint16_t tcp_flag;

    tcp_flag = htons(tcp->th_offset_flags);
    tcp_hdr = (tcp_flag & VIRTIO_NET_TCP_HDR_LENGTH) >> 10;
    tcp_flag &= VIRTIO_NET_TCP_FLAG;
    tcp_flag = htons(tcp->th_offset_flags) & 0x3F;
    if (tcp_flag & TH_SYN) {
        chain->stat.tcp_syn++;
        return RSC_BYPASS;
    }

    if (tcp_flag & (TH_FIN | TH_URG | TH_RST | TH_ECE | TH_CWR)) {
        chain->stat.tcp_ctrl_drain++;
        return RSC_FINAL;
    }

    if (tcp_hdr > sizeof(struct tcp_header)) {
        chain->stat.tcp_all_opt++;
        return RSC_FINAL;
    }

    return RSC_CANDIDATE;
}

static size_t virtio_net_rsc_do_coalesce(VirtioNetRscChain *chain,
                                         NetClientState *nc,
                                         const uint8_t *buf, size_t size,
                                         VirtioNetRscUnit *unit)
{
    int ret;
    VirtioNetRscSeg *seg, *nseg;

    if (QTAILQ_EMPTY(&chain->buffers)) {
        chain->stat.empty_cache++;
        virtio_net_rsc_cache_buf(chain, nc, buf, size);
        timer_mod(chain->drain_timer,
              qemu_clock_get_ns(QEMU_CLOCK_HOST) + chain->n->rsc_timeout);
        return size;
    }

    QTAILQ_FOREACH_SAFE(seg, &chain->buffers, next, nseg) {
        if (chain->proto == ETH_P_IP) {
            ret = virtio_net_rsc_coalesce4(chain, seg, buf, size, unit);
        } else {
            ret = virtio_net_rsc_coalesce6(chain, seg, buf, size, unit);
        }

        if (ret == RSC_FINAL) {
            if (virtio_net_rsc_drain_seg(chain, seg) == 0) {
                /* Send failed */
                chain->stat.final_failed++;
                return 0;
            }

            /* Send current packet */
            return virtio_net_do_receive(nc, buf, size);
        } else if (ret == RSC_NO_MATCH) {
            continue;
        } else {
            /* Coalesced, mark coalesced flag to tell calc cksum for ipv4 */
            seg->is_coalesced = 1;
            return size;
        }
    }

    chain->stat.no_match_cache++;
    virtio_net_rsc_cache_buf(chain, nc, buf, size);
    return size;
}

/* Drain a connection data, this is to avoid out of order segments */
static size_t virtio_net_rsc_drain_flow(VirtioNetRscChain *chain,
                                        NetClientState *nc,
                                        const uint8_t *buf, size_t size,
                                        uint16_t ip_start, uint16_t ip_size,
                                        uint16_t tcp_port)
{
    VirtioNetRscSeg *seg, *nseg;
    uint32_t ppair1, ppair2;

    ppair1 = *(uint32_t *)(buf + tcp_port);
    QTAILQ_FOREACH_SAFE(seg, &chain->buffers, next, nseg) {
        ppair2 = *(uint32_t *)(seg->buf + tcp_port);
        if (memcmp(buf + ip_start, seg->buf + ip_start, ip_size)
            || (ppair1 != ppair2)) {
            continue;
        }
        if (virtio_net_rsc_drain_seg(chain, seg) == 0) {
            chain->stat.drain_failed++;
        }

        break;
    }

    return virtio_net_do_receive(nc, buf, size);
}

static int32_t virtio_net_rsc_sanity_check4(VirtioNetRscChain *chain,
                                            struct ip_header *ip,
                                            const uint8_t *buf, size_t size)
{
    uint16_t ip_len;

    /* Not an ipv4 packet */
    if (((ip->ip_ver_len & 0xF0) >> 4) != IP_HEADER_VERSION_4) {
        chain->stat.ip_option++;
        return RSC_BYPASS;
    }

    /* Don't handle packets with ip option */
    if ((ip->ip_ver_len & 0xF) != VIRTIO_NET_IP4_HEADER_LENGTH) {
        chain->stat.ip_option++;
        return RSC_BYPASS;
    }

    if (ip->ip_p != IPPROTO_TCP) {
        chain->stat.bypass_not_tcp++;
        return RSC_BYPASS;
    }

    /* Don't handle packets with ip fragment */
    if (!(htons(ip->ip_off) & IP_DF)) {
        chain->stat.ip_frag++;
        return RSC_BYPASS;
    }

    /* Don't handle packets with ecn flag */
    if (IPTOS_ECN(ip->ip_tos)) {
        chain->stat.ip_ecn++;
        return RSC_BYPASS;
    }

    ip_len = htons(ip->ip_len);
    if (ip_len < (sizeof(struct ip_header) + sizeof(struct tcp_header))
        || ip_len > (size - chain->n->guest_hdr_len -
                     sizeof(struct eth_header))) {
        chain->stat.ip_hacked++;
        return RSC_BYPASS;
    }

    return RSC_CANDIDATE;
}

static size_t virtio_net_rsc_receive4(VirtioNetRscChain *chain,
                                      NetClientState *nc,
                                      const uint8_t *buf, size_t size)
{
    int32_t ret;
    uint16_t hdr_len;
    VirtioNetRscUnit unit;

    hdr_len = ((VirtIONet *)(chain->n))->guest_hdr_len;

    if (size < (hdr_len + sizeof(struct eth_header) + sizeof(struct ip_header)
        + sizeof(struct tcp_header))) {
        chain->stat.bypass_not_tcp++;
        return virtio_net_do_receive(nc, buf, size);
    }

    virtio_net_rsc_extract_unit4(chain, buf, &unit);
    if (virtio_net_rsc_sanity_check4(chain, unit.ip, buf, size)
        != RSC_CANDIDATE) {
        return virtio_net_do_receive(nc, buf, size);
    }

    ret = virtio_net_rsc_tcp_ctrl_check(chain, unit.tcp);
    if (ret == RSC_BYPASS) {
        return virtio_net_do_receive(nc, buf, size);
    } else if (ret == RSC_FINAL) {
        return virtio_net_rsc_drain_flow(chain, nc, buf, size,
                ((hdr_len + sizeof(struct eth_header)) + 12),
                VIRTIO_NET_IP4_ADDR_SIZE,
                hdr_len + sizeof(struct eth_header) + sizeof(struct ip_header));
    }

    return virtio_net_rsc_do_coalesce(chain, nc, buf, size, &unit);
}

static int32_t virtio_net_rsc_sanity_check6(VirtioNetRscChain *chain,
                                            struct ip6_header *ip6,
                                            const uint8_t *buf, size_t size)
{
    uint16_t ip_len;

    if (((ip6->ip6_ctlun.ip6_un1.ip6_un1_flow & 0xF0) >> 4)
        != IP_HEADER_VERSION_6) {
        return RSC_BYPASS;
    }

    /* Both option and protocol is checked in this */
    if (ip6->ip6_ctlun.ip6_un1.ip6_un1_nxt != IPPROTO_TCP) {
        chain->stat.bypass_not_tcp++;
        return RSC_BYPASS;
    }

    ip_len = htons(ip6->ip6_ctlun.ip6_un1.ip6_un1_plen);
    if (ip_len < sizeof(struct tcp_header) ||
        ip_len > (size - chain->n->guest_hdr_len - sizeof(struct eth_header)
                  - sizeof(struct ip6_header))) {
        chain->stat.ip_hacked++;
        return RSC_BYPASS;
    }

    /* Don't handle packets with ecn flag */
    if (IP6_ECN(ip6->ip6_ctlun.ip6_un3.ip6_un3_ecn)) {
        chain->stat.ip_ecn++;
        return RSC_BYPASS;
    }

    return RSC_CANDIDATE;
}

static size_t virtio_net_rsc_receive6(void *opq, NetClientState *nc,
                                      const uint8_t *buf, size_t size)
{
    int32_t ret;
    uint16_t hdr_len;
    VirtioNetRscChain *chain;
    VirtioNetRscUnit unit;

    chain = (VirtioNetRscChain *)opq;
    hdr_len = ((VirtIONet *)(chain->n))->guest_hdr_len;

    if (size < (hdr_len + sizeof(struct eth_header) + sizeof(struct ip6_header)
        + sizeof(tcp_header))) {
        return virtio_net_do_receive(nc, buf, size);
    }

    virtio_net_rsc_extract_unit6(chain, buf, &unit);
    if (RSC_CANDIDATE != virtio_net_rsc_sanity_check6(chain,
                                                 unit.ip, buf, size)) {
        return virtio_net_do_receive(nc, buf, size);
    }

    ret = virtio_net_rsc_tcp_ctrl_check(chain, unit.tcp);
    if (ret == RSC_BYPASS) {
        return virtio_net_do_receive(nc, buf, size);
    } else if (ret == RSC_FINAL) {
        return virtio_net_rsc_drain_flow(chain, nc, buf, size,
                ((hdr_len + sizeof(struct eth_header)) + 8),
                VIRTIO_NET_IP6_ADDR_SIZE,
                hdr_len + sizeof(struct eth_header)
                + sizeof(struct ip6_header));
    }

    return virtio_net_rsc_do_coalesce(chain, nc, buf, size, &unit);
}

static VirtioNetRscChain *virtio_net_rsc_lookup_chain(VirtIONet *n,
                                                      NetClientState *nc,
                                                      uint16_t proto)
{
    VirtioNetRscChain *chain;

    if ((proto != (uint16_t)ETH_P_IP) && (proto != (uint16_t)ETH_P_IPV6)) {
        return NULL;
    }

    QTAILQ_FOREACH(chain, &n->rsc_chains, next) {
        if (chain->proto == proto) {
            return chain;
        }
    }

    chain = g_malloc(sizeof(*chain));
    chain->n = n;
    chain->proto = proto;
    if (proto == (uint16_t)ETH_P_IP) {
        chain->max_payload = VIRTIO_NET_MAX_IP4_PAYLOAD;
        chain->gso_type = VIRTIO_NET_HDR_GSO_TCPV4;
    } else {
        chain->max_payload = VIRTIO_NET_MAX_IP6_PAYLOAD;
        chain->gso_type = VIRTIO_NET_HDR_GSO_TCPV6;
    }
    chain->drain_timer = timer_new_ns(QEMU_CLOCK_HOST,
                                      virtio_net_rsc_purge, chain);
    memset(&chain->stat, 0, sizeof(chain->stat));

    QTAILQ_INIT(&chain->buffers);
    QTAILQ_INSERT_TAIL(&n->rsc_chains, chain, next);

    return chain;
}

static ssize_t virtio_net_rsc_receive(NetClientState *nc,
                                      const uint8_t *buf,
                                      size_t size)
{
    uint16_t proto;
    VirtioNetRscChain *chain;
    struct eth_header *eth;
    VirtIONet *n;

    n = qemu_get_nic_opaque(nc);
    if (size < (n->host_hdr_len + sizeof(struct eth_header))) {
        return virtio_net_do_receive(nc, buf, size);
    }

    eth = (struct eth_header *)(buf + n->guest_hdr_len);
    proto = htons(eth->h_proto);

    chain = virtio_net_rsc_lookup_chain(n, nc, proto);
    if (chain) {
        chain->stat.received++;
        if (proto == (uint16_t)ETH_P_IP && n->rsc4_enabled) {
            return virtio_net_rsc_receive4(chain, nc, buf, size);
        } else if (proto == (uint16_t)ETH_P_IPV6 && n->rsc6_enabled) {
            return virtio_net_rsc_receive6(chain, nc, buf, size);
        }
    }
    return virtio_net_do_receive(nc, buf, size);
}

static ssize_t virtio_net_receive(NetClientState *nc, const uint8_t *buf,
                                  size_t size)
{
    VirtIONet *n = qemu_get_nic_opaque(nc);
    if ((n->rsc4_enabled || n->rsc6_enabled)) {
        return virtio_net_rsc_receive(nc, buf, size);
    } else {
        return virtio_net_do_receive(nc, buf, size);
    }
}

static int32_t virtio_net_flush_tx(VirtIONetQueue *q);

static void virtio_net_tx_complete(NetClientState *nc, ssize_t len)
{
    VirtIONet *n = qemu_get_nic_opaque(nc);
    VirtIONetQueue *q = virtio_net_get_subqueue(nc);
    VirtIODevice *vdev = VIRTIO_DEVICE(n);

    virtqueue_push(q->tx_vq, q->async_tx.elem, 0);
    virtio_notify(vdev, q->tx_vq);

    g_free(q->async_tx.elem);
    q->async_tx.elem = NULL;

    virtio_queue_set_notification(q->tx_vq, 1);
    virtio_net_flush_tx(q);
}

/* TX */
static int32_t virtio_net_flush_tx(VirtIONetQueue *q)
{
    VirtIONet *n = q->n;
    VirtIODevice *vdev = VIRTIO_DEVICE(n);
    VirtQueueElement *elem;
    int32_t num_packets = 0;
    int queue_index = vq2q(virtio_get_queue_index(q->tx_vq));
    if (!(vdev->status & VIRTIO_CONFIG_S_DRIVER_OK)) {
        return num_packets;
    }

    if (q->async_tx.elem) {
        virtio_queue_set_notification(q->tx_vq, 0);
        return num_packets;
    }

    for (;;) {
        ssize_t ret;
        unsigned int out_num;
        struct iovec sg[VIRTQUEUE_MAX_SIZE], sg2[VIRTQUEUE_MAX_SIZE + 1], *out_sg;
        struct virtio_net_hdr_mrg_rxbuf mhdr;

        elem = virtqueue_pop(q->tx_vq, sizeof(VirtQueueElement));
        if (!elem) {
            break;
        }

        out_num = elem->out_num;
        out_sg = elem->out_sg;
        if (out_num < 1) {
            virtio_error(vdev, "virtio-net header not in first element");
            virtqueue_detach_element(q->tx_vq, elem, 0);
            g_free(elem);
            return -EINVAL;
        }

        if (n->has_vnet_hdr) {
            if (iov_to_buf(out_sg, out_num, 0, &mhdr, n->guest_hdr_len) <
                n->guest_hdr_len) {
                virtio_error(vdev, "virtio-net header incorrect");
                virtqueue_detach_element(q->tx_vq, elem, 0);
                g_free(elem);
                return -EINVAL;
            }
            if (n->needs_vnet_hdr_swap) {
                virtio_net_hdr_swap(vdev, (void *) &mhdr);
                sg2[0].iov_base = &mhdr;
                sg2[0].iov_len = n->guest_hdr_len;
                out_num = iov_copy(&sg2[1], ARRAY_SIZE(sg2) - 1,
                                   out_sg, out_num,
                                   n->guest_hdr_len, -1);
                if (out_num == VIRTQUEUE_MAX_SIZE) {
                    goto drop;
                }
                out_num += 1;
                out_sg = sg2;
            }
        }
        /*
         * If host wants to see the guest header as is, we can
         * pass it on unchanged. Otherwise, copy just the parts
         * that host is interested in.
         */
        assert(n->host_hdr_len <= n->guest_hdr_len);
        if (n->host_hdr_len != n->guest_hdr_len) {
            unsigned sg_num = iov_copy(sg, ARRAY_SIZE(sg),
                                       out_sg, out_num,
                                       0, n->host_hdr_len);
            sg_num += iov_copy(sg + sg_num, ARRAY_SIZE(sg) - sg_num,
                             out_sg, out_num,
                             n->guest_hdr_len, -1);
            out_num = sg_num;
            out_sg = sg;
        }

        ret = qemu_sendv_packet_async(qemu_get_subqueue(n->nic, queue_index),
                                      out_sg, out_num, virtio_net_tx_complete);
        if (ret == 0) {
            virtio_queue_set_notification(q->tx_vq, 0);
            q->async_tx.elem = elem;
            return -EBUSY;
        }

drop:
        virtqueue_push(q->tx_vq, elem, 0);
        virtio_notify(vdev, q->tx_vq);
        g_free(elem);

        if (++num_packets >= n->tx_burst) {
            break;
        }
    }
    return num_packets;
}

static void virtio_net_handle_tx_timer(VirtIODevice *vdev, VirtQueue *vq)
{
    VirtIONet *n = VIRTIO_NET(vdev);
    VirtIONetQueue *q = &n->vqs[vq2q(virtio_get_queue_index(vq))];

    if (unlikely((n->status & VIRTIO_NET_S_LINK_UP) == 0)) {
        virtio_net_drop_tx_queue_data(vdev, vq);
        return;
    }

    /* This happens when device was stopped but VCPU wasn't. */
    if (!vdev->vm_running) {
        q->tx_waiting = 1;
        return;
    }

    if (q->tx_waiting) {
        virtio_queue_set_notification(vq, 1);
        timer_del(q->tx_timer);
        q->tx_waiting = 0;
        if (virtio_net_flush_tx(q) == -EINVAL) {
            return;
        }
    } else {
        timer_mod(q->tx_timer,
                       qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + n->tx_timeout);
        q->tx_waiting = 1;
        virtio_queue_set_notification(vq, 0);
    }
}

static void virtio_net_handle_tx_bh(VirtIODevice *vdev, VirtQueue *vq)
{
    VirtIONet *n = VIRTIO_NET(vdev);
    VirtIONetQueue *q = &n->vqs[vq2q(virtio_get_queue_index(vq))];

    if (unlikely((n->status & VIRTIO_NET_S_LINK_UP) == 0)) {
        virtio_net_drop_tx_queue_data(vdev, vq);
        return;
    }

    if (unlikely(q->tx_waiting)) {
        return;
    }
    q->tx_waiting = 1;
    /* This happens when device was stopped but VCPU wasn't. */
    if (!vdev->vm_running) {
        return;
    }
    virtio_queue_set_notification(vq, 0);
    qemu_bh_schedule(q->tx_bh);
}

static void virtio_net_tx_timer(void *opaque)
{
    VirtIONetQueue *q = opaque;
    VirtIONet *n = q->n;
    VirtIODevice *vdev = VIRTIO_DEVICE(n);
    /* This happens when device was stopped but BH wasn't. */
    if (!vdev->vm_running) {
        /* Make sure tx waiting is set, so we'll run when restarted. */
        assert(q->tx_waiting);
        return;
    }

    q->tx_waiting = 0;

    /* Just in case the driver is not ready on more */
    if (!(vdev->status & VIRTIO_CONFIG_S_DRIVER_OK)) {
        return;
    }

    virtio_queue_set_notification(q->tx_vq, 1);
    virtio_net_flush_tx(q);
}

static void virtio_net_tx_bh(void *opaque)
{
    VirtIONetQueue *q = opaque;
    VirtIONet *n = q->n;
    VirtIODevice *vdev = VIRTIO_DEVICE(n);
    int32_t ret;

    /* This happens when device was stopped but BH wasn't. */
    if (!vdev->vm_running) {
        /* Make sure tx waiting is set, so we'll run when restarted. */
        assert(q->tx_waiting);
        return;
    }

    q->tx_waiting = 0;

    /* Just in case the driver is not ready on more */
    if (unlikely(!(vdev->status & VIRTIO_CONFIG_S_DRIVER_OK))) {
        return;
    }

    ret = virtio_net_flush_tx(q);
    if (ret == -EBUSY || ret == -EINVAL) {
        return; /* Notification re-enable handled by tx_complete or device
                 * broken */
    }

    /* If we flush a full burst of packets, assume there are
     * more coming and immediately reschedule */
    if (ret >= n->tx_burst) {
        qemu_bh_schedule(q->tx_bh);
        q->tx_waiting = 1;
        return;
    }

    /* If less than a full burst, re-enable notification and flush
     * anything that may have come in while we weren't looking.  If
     * we find something, assume the guest is still active and reschedule */
    virtio_queue_set_notification(q->tx_vq, 1);
    ret = virtio_net_flush_tx(q);
    if (ret == -EINVAL) {
        return;
    } else if (ret > 0) {
        virtio_queue_set_notification(q->tx_vq, 0);
        qemu_bh_schedule(q->tx_bh);
        q->tx_waiting = 1;
    }
}

static void virtio_net_add_queue(VirtIONet *n, int index)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(n);

    n->vqs[index].rx_vq = virtio_add_queue(vdev, n->net_conf.rx_queue_size,
                                           virtio_net_handle_rx);

    if (n->net_conf.tx && !strcmp(n->net_conf.tx, "timer")) {
        n->vqs[index].tx_vq =
            virtio_add_queue(vdev, n->net_conf.tx_queue_size,
                             virtio_net_handle_tx_timer);
        n->vqs[index].tx_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                              virtio_net_tx_timer,
                                              &n->vqs[index]);
    } else {
        n->vqs[index].tx_vq =
            virtio_add_queue(vdev, n->net_conf.tx_queue_size,
                             virtio_net_handle_tx_bh);
        n->vqs[index].tx_bh = qemu_bh_new(virtio_net_tx_bh, &n->vqs[index]);
    }

    n->vqs[index].tx_waiting = 0;
    n->vqs[index].n = n;
}

static void virtio_net_del_queue(VirtIONet *n, int index)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(n);
    VirtIONetQueue *q = &n->vqs[index];
    NetClientState *nc = qemu_get_subqueue(n->nic, index);

    qemu_purge_queued_packets(nc);

    virtio_del_queue(vdev, index * 2);
    if (q->tx_timer) {
        timer_del(q->tx_timer);
        timer_free(q->tx_timer);
        q->tx_timer = NULL;
    } else {
        qemu_bh_delete(q->tx_bh);
        q->tx_bh = NULL;
    }
    q->tx_waiting = 0;
    virtio_del_queue(vdev, index * 2 + 1);
}

static void virtio_net_change_num_queues(VirtIONet *n, int new_max_queues)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(n);
    int old_num_queues = virtio_get_num_queues(vdev);
    int new_num_queues = new_max_queues * 2 + 1;
    int i;

    assert(old_num_queues >= 3);
    assert(old_num_queues % 2 == 1);

    if (old_num_queues == new_num_queues) {
        return;
    }

    /*
     * We always need to remove and add ctrl vq if
     * old_num_queues != new_num_queues. Remove ctrl_vq first,
     * and then we only enter one of the following two loops.
     */
    virtio_del_queue(vdev, old_num_queues - 1);

    for (i = new_num_queues - 1; i < old_num_queues - 1; i += 2) {
        /* new_num_queues < old_num_queues */
        virtio_net_del_queue(n, i / 2);
    }

    for (i = old_num_queues - 1; i < new_num_queues - 1; i += 2) {
        /* new_num_queues > old_num_queues */
        virtio_net_add_queue(n, i / 2);
    }

    /* add ctrl_vq last */
    n->ctrl_vq = virtio_add_queue(vdev, 64, virtio_net_handle_ctrl);
}

static void virtio_net_set_multiqueue(VirtIONet *n, int multiqueue)
{
    int max = multiqueue ? n->max_queues : 1;

    n->multiqueue = multiqueue;
    virtio_net_change_num_queues(n, max);

    virtio_net_set_queues(n);
}

static int virtio_net_post_load_device(void *opaque, int version_id)
{
    VirtIONet *n = opaque;
    VirtIODevice *vdev = VIRTIO_DEVICE(n);
    int i, link_down;

    trace_virtio_net_post_load_device();
    virtio_net_set_mrg_rx_bufs(n, n->mergeable_rx_bufs,
                               virtio_vdev_has_feature(vdev,
                                                       VIRTIO_F_VERSION_1));

    /* MAC_TABLE_ENTRIES may be different from the saved image */
    if (n->mac_table.in_use > MAC_TABLE_ENTRIES) {
        n->mac_table.in_use = 0;
    }

    if (!virtio_vdev_has_feature(vdev, VIRTIO_NET_F_CTRL_GUEST_OFFLOADS)) {
        n->curr_guest_offloads = virtio_net_supported_guest_offloads(n);
    }

    /*
     * curr_guest_offloads will be later overwritten by the
     * virtio_set_features_nocheck call done from the virtio_load.
     * Here we make sure it is preserved and restored accordingly
     * in the virtio_net_post_load_virtio callback.
     */
    n->saved_guest_offloads = n->curr_guest_offloads;

    virtio_net_set_queues(n);

    /* Find the first multicast entry in the saved MAC filter */
    for (i = 0; i < n->mac_table.in_use; i++) {
        if (n->mac_table.macs[i * ETH_ALEN] & 1) {
            break;
        }
    }
    n->mac_table.first_multi = i;

    /* nc.link_down can't be migrated, so infer link_down according
     * to link status bit in n->status */
    link_down = (n->status & VIRTIO_NET_S_LINK_UP) == 0;
    for (i = 0; i < n->max_queues; i++) {
        qemu_get_subqueue(n->nic, i)->link_down = link_down;
    }

    if (virtio_vdev_has_feature(vdev, VIRTIO_NET_F_GUEST_ANNOUNCE) &&
        virtio_vdev_has_feature(vdev, VIRTIO_NET_F_CTRL_VQ)) {
        qemu_announce_timer_reset(&n->announce_timer, migrate_announce_params(),
                                  QEMU_CLOCK_VIRTUAL,
                                  virtio_net_announce_timer, n);
        if (n->announce_timer.round) {
            timer_mod(n->announce_timer.tm,
                      qemu_clock_get_ms(n->announce_timer.type));
        } else {
            qemu_announce_timer_del(&n->announce_timer, false);
        }
    }

    return 0;
}

static int virtio_net_post_load_virtio(VirtIODevice *vdev)
{
    VirtIONet *n = VIRTIO_NET(vdev);
    /*
     * The actual needed state is now in saved_guest_offloads,
     * see virtio_net_post_load_device for detail.
     * Restore it back and apply the desired offloads.
     */
    n->curr_guest_offloads = n->saved_guest_offloads;
    if (peer_has_vnet_hdr(n)) {
        virtio_net_apply_guest_offloads(n);
    }

    return 0;
}

/* tx_waiting field of a VirtIONetQueue */
static const VMStateDescription vmstate_virtio_net_queue_tx_waiting = {
    .name = "virtio-net-queue-tx_waiting",
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(tx_waiting, VirtIONetQueue),
        VMSTATE_END_OF_LIST()
   },
};

static bool max_queues_gt_1(void *opaque, int version_id)
{
    return VIRTIO_NET(opaque)->max_queues > 1;
}

static bool has_ctrl_guest_offloads(void *opaque, int version_id)
{
    return virtio_vdev_has_feature(VIRTIO_DEVICE(opaque),
                                   VIRTIO_NET_F_CTRL_GUEST_OFFLOADS);
}

static bool mac_table_fits(void *opaque, int version_id)
{
    return VIRTIO_NET(opaque)->mac_table.in_use <= MAC_TABLE_ENTRIES;
}

static bool mac_table_doesnt_fit(void *opaque, int version_id)
{
    return !mac_table_fits(opaque, version_id);
}

/* This temporary type is shared by all the WITH_TMP methods
 * although only some fields are used by each.
 */
struct VirtIONetMigTmp {
    VirtIONet      *parent;
    VirtIONetQueue *vqs_1;
    uint16_t        curr_queues_1;
    uint8_t         has_ufo;
    uint32_t        has_vnet_hdr;
};

/* The 2nd and subsequent tx_waiting flags are loaded later than
 * the 1st entry in the queues and only if there's more than one
 * entry.  We use the tmp mechanism to calculate a temporary
 * pointer and count and also validate the count.
 */

static int virtio_net_tx_waiting_pre_save(void *opaque)
{
    struct VirtIONetMigTmp *tmp = opaque;

    tmp->vqs_1 = tmp->parent->vqs + 1;
    tmp->curr_queues_1 = tmp->parent->curr_queues - 1;
    if (tmp->parent->curr_queues == 0) {
        tmp->curr_queues_1 = 0;
    }

    return 0;
}

static int virtio_net_tx_waiting_pre_load(void *opaque)
{
    struct VirtIONetMigTmp *tmp = opaque;

    /* Reuse the pointer setup from save */
    virtio_net_tx_waiting_pre_save(opaque);

    if (tmp->parent->curr_queues > tmp->parent->max_queues) {
        error_report("virtio-net: curr_queues %x > max_queues %x",
            tmp->parent->curr_queues, tmp->parent->max_queues);

        return -EINVAL;
    }

    return 0; /* all good */
}

static const VMStateDescription vmstate_virtio_net_tx_waiting = {
    .name      = "virtio-net-tx_waiting",
    .pre_load  = virtio_net_tx_waiting_pre_load,
    .pre_save  = virtio_net_tx_waiting_pre_save,
    .fields    = (VMStateField[]) {
        VMSTATE_STRUCT_VARRAY_POINTER_UINT16(vqs_1, struct VirtIONetMigTmp,
                                     curr_queues_1,
                                     vmstate_virtio_net_queue_tx_waiting,
                                     struct VirtIONetQueue),
        VMSTATE_END_OF_LIST()
    },
};

/* the 'has_ufo' flag is just tested; if the incoming stream has the
 * flag set we need to check that we have it
 */
static int virtio_net_ufo_post_load(void *opaque, int version_id)
{
    struct VirtIONetMigTmp *tmp = opaque;

    if (tmp->has_ufo && !peer_has_ufo(tmp->parent)) {
        error_report("virtio-net: saved image requires TUN_F_UFO support");
        return -EINVAL;
    }

    return 0;
}

static int virtio_net_ufo_pre_save(void *opaque)
{
    struct VirtIONetMigTmp *tmp = opaque;

    tmp->has_ufo = tmp->parent->has_ufo;

    return 0;
}

static const VMStateDescription vmstate_virtio_net_has_ufo = {
    .name      = "virtio-net-ufo",
    .post_load = virtio_net_ufo_post_load,
    .pre_save  = virtio_net_ufo_pre_save,
    .fields    = (VMStateField[]) {
        VMSTATE_UINT8(has_ufo, struct VirtIONetMigTmp),
        VMSTATE_END_OF_LIST()
    },
};

/* the 'has_vnet_hdr' flag is just tested; if the incoming stream has the
 * flag set we need to check that we have it
 */
static int virtio_net_vnet_post_load(void *opaque, int version_id)
{
    struct VirtIONetMigTmp *tmp = opaque;

    if (tmp->has_vnet_hdr && !peer_has_vnet_hdr(tmp->parent)) {
        error_report("virtio-net: saved image requires vnet_hdr=on");
        return -EINVAL;
    }

    return 0;
}

static int virtio_net_vnet_pre_save(void *opaque)
{
    struct VirtIONetMigTmp *tmp = opaque;

    tmp->has_vnet_hdr = tmp->parent->has_vnet_hdr;

    return 0;
}

static const VMStateDescription vmstate_virtio_net_has_vnet = {
    .name      = "virtio-net-vnet",
    .post_load = virtio_net_vnet_post_load,
    .pre_save  = virtio_net_vnet_pre_save,
    .fields    = (VMStateField[]) {
        VMSTATE_UINT32(has_vnet_hdr, struct VirtIONetMigTmp),
        VMSTATE_END_OF_LIST()
    },
};

static const VMStateDescription vmstate_virtio_net_device = {
    .name = "virtio-net-device",
    .version_id = VIRTIO_NET_VM_VERSION,
    .minimum_version_id = VIRTIO_NET_VM_VERSION,
    .post_load = virtio_net_post_load_device,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8_ARRAY(mac, VirtIONet, ETH_ALEN),
        VMSTATE_STRUCT_POINTER(vqs, VirtIONet,
                               vmstate_virtio_net_queue_tx_waiting,
                               VirtIONetQueue),
        VMSTATE_UINT32(mergeable_rx_bufs, VirtIONet),
        VMSTATE_UINT16(status, VirtIONet),
        VMSTATE_UINT8(promisc, VirtIONet),
        VMSTATE_UINT8(allmulti, VirtIONet),
        VMSTATE_UINT32(mac_table.in_use, VirtIONet),

        /* Guarded pair: If it fits we load it, else we throw it away
         * - can happen if source has a larger MAC table.; post-load
         *  sets flags in this case.
         */
        VMSTATE_VBUFFER_MULTIPLY(mac_table.macs, VirtIONet,
                                0, mac_table_fits, mac_table.in_use,
                                 ETH_ALEN),
        VMSTATE_UNUSED_VARRAY_UINT32(VirtIONet, mac_table_doesnt_fit, 0,
                                     mac_table.in_use, ETH_ALEN),

        /* Note: This is an array of uint32's that's always been saved as a
         * buffer; hold onto your endiannesses; it's actually used as a bitmap
         * but based on the uint.
         */
        VMSTATE_BUFFER_POINTER_UNSAFE(vlans, VirtIONet, 0, MAX_VLAN >> 3),
        VMSTATE_WITH_TMP(VirtIONet, struct VirtIONetMigTmp,
                         vmstate_virtio_net_has_vnet),
        VMSTATE_UINT8(mac_table.multi_overflow, VirtIONet),
        VMSTATE_UINT8(mac_table.uni_overflow, VirtIONet),
        VMSTATE_UINT8(alluni, VirtIONet),
        VMSTATE_UINT8(nomulti, VirtIONet),
        VMSTATE_UINT8(nouni, VirtIONet),
        VMSTATE_UINT8(nobcast, VirtIONet),
        VMSTATE_WITH_TMP(VirtIONet, struct VirtIONetMigTmp,
                         vmstate_virtio_net_has_ufo),
        VMSTATE_SINGLE_TEST(max_queues, VirtIONet, max_queues_gt_1, 0,
                            vmstate_info_uint16_equal, uint16_t),
        VMSTATE_UINT16_TEST(curr_queues, VirtIONet, max_queues_gt_1),
        VMSTATE_WITH_TMP(VirtIONet, struct VirtIONetMigTmp,
                         vmstate_virtio_net_tx_waiting),
        VMSTATE_UINT64_TEST(curr_guest_offloads, VirtIONet,
                            has_ctrl_guest_offloads),
        VMSTATE_END_OF_LIST()
   },
};

static NetClientInfo net_virtio_info = {
    .type = NET_CLIENT_DRIVER_NIC,
    .size = sizeof(NICState),
    .can_receive = virtio_net_can_receive,
    .receive = virtio_net_receive,
    .link_status_changed = virtio_net_set_link_status,
    .query_rx_filter = virtio_net_query_rxfilter,
    .announce = virtio_net_announce,
};

static bool virtio_net_guest_notifier_pending(VirtIODevice *vdev, int idx)
{
    VirtIONet *n = VIRTIO_NET(vdev);
    NetClientState *nc = qemu_get_subqueue(n->nic, vq2q(idx));
    assert(n->vhost_started);
    return vhost_net_virtqueue_pending(get_vhost_net(nc->peer), idx);
}

static void virtio_net_guest_notifier_mask(VirtIODevice *vdev, int idx,
                                           bool mask)
{
    VirtIONet *n = VIRTIO_NET(vdev);
    NetClientState *nc = qemu_get_subqueue(n->nic, vq2q(idx));
    assert(n->vhost_started);
    vhost_net_virtqueue_mask(get_vhost_net(nc->peer),
                             vdev, idx, mask);
}

static void virtio_net_set_config_size(VirtIONet *n, uint64_t host_features)
{
    virtio_add_feature(&host_features, VIRTIO_NET_F_MAC);

    n->config_size = virtio_feature_get_config_size(feature_sizes,
                                                    host_features);
}

void virtio_net_set_netclient_name(VirtIONet *n, const char *name,
                                   const char *type)
{
    /*
     * The name can be NULL, the netclient name will be type.x.
     */
    assert(type != NULL);

    g_free(n->netclient_name);
    g_free(n->netclient_type);
    n->netclient_name = g_strdup(name);
    n->netclient_type = g_strdup(type);
}

static void virtio_net_device_realize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VirtIONet *n = VIRTIO_NET(dev);
    NetClientState *nc;
    int i;

    if (n->net_conf.mtu) {
        n->host_features |= (1ULL << VIRTIO_NET_F_MTU);
    }

    if (n->net_conf.duplex_str) {
        if (strncmp(n->net_conf.duplex_str, "half", 5) == 0) {
            n->net_conf.duplex = DUPLEX_HALF;
        } else if (strncmp(n->net_conf.duplex_str, "full", 5) == 0) {
            n->net_conf.duplex = DUPLEX_FULL;
        } else {
            error_setg(errp, "'duplex' must be 'half' or 'full'");
        }
        n->host_features |= (1ULL << VIRTIO_NET_F_SPEED_DUPLEX);
    } else {
        n->net_conf.duplex = DUPLEX_UNKNOWN;
    }

    if (n->net_conf.speed < SPEED_UNKNOWN) {
        error_setg(errp, "'speed' must be between 0 and INT_MAX");
    } else if (n->net_conf.speed >= 0) {
        n->host_features |= (1ULL << VIRTIO_NET_F_SPEED_DUPLEX);
    }

    virtio_net_set_config_size(n, n->host_features);
    virtio_init(vdev, "virtio-net", VIRTIO_ID_NET, n->config_size);

    /*
     * We set a lower limit on RX queue size to what it always was.
     * Guests that want a smaller ring can always resize it without
     * help from us (using virtio 1 and up).
     */
    if (n->net_conf.rx_queue_size < VIRTIO_NET_RX_QUEUE_MIN_SIZE ||
        n->net_conf.rx_queue_size > VIRTQUEUE_MAX_SIZE ||
        !is_power_of_2(n->net_conf.rx_queue_size)) {
        error_setg(errp, "Invalid rx_queue_size (= %" PRIu16 "), "
                   "must be a power of 2 between %d and %d.",
                   n->net_conf.rx_queue_size, VIRTIO_NET_RX_QUEUE_MIN_SIZE,
                   VIRTQUEUE_MAX_SIZE);
        virtio_cleanup(vdev);
        return;
    }

    if (n->net_conf.tx_queue_size < VIRTIO_NET_TX_QUEUE_MIN_SIZE ||
        n->net_conf.tx_queue_size > VIRTQUEUE_MAX_SIZE ||
        !is_power_of_2(n->net_conf.tx_queue_size)) {
        error_setg(errp, "Invalid tx_queue_size (= %" PRIu16 "), "
                   "must be a power of 2 between %d and %d",
                   n->net_conf.tx_queue_size, VIRTIO_NET_TX_QUEUE_MIN_SIZE,
                   VIRTQUEUE_MAX_SIZE);
        virtio_cleanup(vdev);
        return;
    }

    n->max_queues = MAX(n->nic_conf.peers.queues, 1);
    if (n->max_queues * 2 + 1 > VIRTIO_QUEUE_MAX) {
        error_setg(errp, "Invalid number of queues (= %" PRIu32 "), "
                   "must be a positive integer less than %d.",
                   n->max_queues, (VIRTIO_QUEUE_MAX - 1) / 2);
        virtio_cleanup(vdev);
        return;
    }
    n->vqs = g_malloc0(sizeof(VirtIONetQueue) * n->max_queues);
    n->curr_queues = 1;
    n->tx_timeout = n->net_conf.txtimer;

    if (n->net_conf.tx && strcmp(n->net_conf.tx, "timer")
                       && strcmp(n->net_conf.tx, "bh")) {
        warn_report("virtio-net: "
                    "Unknown option tx=%s, valid options: \"timer\" \"bh\"",
                    n->net_conf.tx);
        error_printf("Defaulting to \"bh\"");
    }

    n->net_conf.tx_queue_size = MIN(virtio_net_max_tx_queue_size(n),
                                    n->net_conf.tx_queue_size);

    for (i = 0; i < n->max_queues; i++) {
        virtio_net_add_queue(n, i);
    }

    n->ctrl_vq = virtio_add_queue(vdev, 64, virtio_net_handle_ctrl);
    qemu_macaddr_default_if_unset(&n->nic_conf.macaddr);
    memcpy(&n->mac[0], &n->nic_conf.macaddr, sizeof(n->mac));
    n->status = VIRTIO_NET_S_LINK_UP;
    qemu_announce_timer_reset(&n->announce_timer, migrate_announce_params(),
                              QEMU_CLOCK_VIRTUAL,
                              virtio_net_announce_timer, n);
    n->announce_timer.round = 0;

    if (n->netclient_type) {
        /*
         * Happen when virtio_net_set_netclient_name has been called.
         */
        n->nic = qemu_new_nic(&net_virtio_info, &n->nic_conf,
                              n->netclient_type, n->netclient_name, n);
    } else {
        n->nic = qemu_new_nic(&net_virtio_info, &n->nic_conf,
                              object_get_typename(OBJECT(dev)), dev->id, n);
    }

    peer_test_vnet_hdr(n);
    if (peer_has_vnet_hdr(n)) {
        for (i = 0; i < n->max_queues; i++) {
            qemu_using_vnet_hdr(qemu_get_subqueue(n->nic, i)->peer, true);
        }
        n->host_hdr_len = sizeof(struct virtio_net_hdr);
    } else {
        n->host_hdr_len = 0;
    }

    qemu_format_nic_info_str(qemu_get_queue(n->nic), n->nic_conf.macaddr.a);

    n->vqs[0].tx_waiting = 0;
    n->tx_burst = n->net_conf.txburst;
    virtio_net_set_mrg_rx_bufs(n, 0, 0);
    n->promisc = 1; /* for compatibility */

    n->mac_table.macs = g_malloc0(MAC_TABLE_ENTRIES * ETH_ALEN);

    n->vlans = g_malloc0(MAX_VLAN >> 3);

    nc = qemu_get_queue(n->nic);
    nc->rxfilter_notify_enabled = 1;

    QTAILQ_INIT(&n->rsc_chains);
    n->qdev = dev;
}

static void virtio_net_device_unrealize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VirtIONet *n = VIRTIO_NET(dev);
    int i, max_queues;

    /* This will stop vhost backend if appropriate. */
    virtio_net_set_status(vdev, 0);

    g_free(n->netclient_name);
    n->netclient_name = NULL;
    g_free(n->netclient_type);
    n->netclient_type = NULL;

    g_free(n->mac_table.macs);
    g_free(n->vlans);

    max_queues = n->multiqueue ? n->max_queues : 1;
    for (i = 0; i < max_queues; i++) {
        virtio_net_del_queue(n, i);
    }

    qemu_announce_timer_del(&n->announce_timer, false);
    g_free(n->vqs);
    qemu_del_nic(n->nic);
    virtio_net_rsc_cleanup(n);
    virtio_cleanup(vdev);
}

static void virtio_net_instance_init(Object *obj)
{
    VirtIONet *n = VIRTIO_NET(obj);

    /*
     * The default config_size is sizeof(struct virtio_net_config).
     * Can be overriden with virtio_net_set_config_size.
     */
    n->config_size = sizeof(struct virtio_net_config);
    device_add_bootindex_property(obj, &n->nic_conf.bootindex,
                                  "bootindex", "/ethernet-phy@0",
                                  DEVICE(n), NULL);
}

static int virtio_net_pre_save(void *opaque)
{
    VirtIONet *n = opaque;

    /* At this point, backend must be stopped, otherwise
     * it might keep writing to memory. */
    assert(!n->vhost_started);

    return 0;
}

static const VMStateDescription vmstate_virtio_net = {
    .name = "virtio-net",
    .minimum_version_id = VIRTIO_NET_VM_VERSION,
    .version_id = VIRTIO_NET_VM_VERSION,
    .fields = (VMStateField[]) {
        VMSTATE_VIRTIO_DEVICE,
        VMSTATE_END_OF_LIST()
    },
    .pre_save = virtio_net_pre_save,
};

static Property virtio_net_properties[] = {
    DEFINE_PROP_BIT64("csum", VirtIONet, host_features,
                    VIRTIO_NET_F_CSUM, true),
    DEFINE_PROP_BIT64("guest_csum", VirtIONet, host_features,
                    VIRTIO_NET_F_GUEST_CSUM, true),
    DEFINE_PROP_BIT64("gso", VirtIONet, host_features, VIRTIO_NET_F_GSO, true),
    DEFINE_PROP_BIT64("guest_tso4", VirtIONet, host_features,
                    VIRTIO_NET_F_GUEST_TSO4, true),
    DEFINE_PROP_BIT64("guest_tso6", VirtIONet, host_features,
                    VIRTIO_NET_F_GUEST_TSO6, true),
    DEFINE_PROP_BIT64("guest_ecn", VirtIONet, host_features,
                    VIRTIO_NET_F_GUEST_ECN, true),
    DEFINE_PROP_BIT64("guest_ufo", VirtIONet, host_features,
                    VIRTIO_NET_F_GUEST_UFO, true),
    DEFINE_PROP_BIT64("guest_announce", VirtIONet, host_features,
                    VIRTIO_NET_F_GUEST_ANNOUNCE, true),
    DEFINE_PROP_BIT64("host_tso4", VirtIONet, host_features,
                    VIRTIO_NET_F_HOST_TSO4, true),
    DEFINE_PROP_BIT64("host_tso6", VirtIONet, host_features,
                    VIRTIO_NET_F_HOST_TSO6, true),
    DEFINE_PROP_BIT64("host_ecn", VirtIONet, host_features,
                    VIRTIO_NET_F_HOST_ECN, true),
    DEFINE_PROP_BIT64("host_ufo", VirtIONet, host_features,
                    VIRTIO_NET_F_HOST_UFO, true),
    DEFINE_PROP_BIT64("mrg_rxbuf", VirtIONet, host_features,
                    VIRTIO_NET_F_MRG_RXBUF, true),
    DEFINE_PROP_BIT64("status", VirtIONet, host_features,
                    VIRTIO_NET_F_STATUS, true),
    DEFINE_PROP_BIT64("ctrl_vq", VirtIONet, host_features,
                    VIRTIO_NET_F_CTRL_VQ, true),
    DEFINE_PROP_BIT64("ctrl_rx", VirtIONet, host_features,
                    VIRTIO_NET_F_CTRL_RX, true),
    DEFINE_PROP_BIT64("ctrl_vlan", VirtIONet, host_features,
                    VIRTIO_NET_F_CTRL_VLAN, true),
    DEFINE_PROP_BIT64("ctrl_rx_extra", VirtIONet, host_features,
                    VIRTIO_NET_F_CTRL_RX_EXTRA, true),
    DEFINE_PROP_BIT64("ctrl_mac_addr", VirtIONet, host_features,
                    VIRTIO_NET_F_CTRL_MAC_ADDR, true),
    DEFINE_PROP_BIT64("ctrl_guest_offloads", VirtIONet, host_features,
                    VIRTIO_NET_F_CTRL_GUEST_OFFLOADS, true),
    DEFINE_PROP_BIT64("mq", VirtIONet, host_features, VIRTIO_NET_F_MQ, false),
    DEFINE_PROP_BIT64("guest_rsc_ext", VirtIONet, host_features,
                    VIRTIO_NET_F_RSC_EXT, false),
    DEFINE_PROP_UINT32("rsc_interval", VirtIONet, rsc_timeout,
                       VIRTIO_NET_RSC_DEFAULT_INTERVAL),
    DEFINE_NIC_PROPERTIES(VirtIONet, nic_conf),
    DEFINE_PROP_UINT32("x-txtimer", VirtIONet, net_conf.txtimer,
                       TX_TIMER_INTERVAL),
    DEFINE_PROP_INT32("x-txburst", VirtIONet, net_conf.txburst, TX_BURST),
    DEFINE_PROP_STRING("tx", VirtIONet, net_conf.tx),
    DEFINE_PROP_UINT16("rx_queue_size", VirtIONet, net_conf.rx_queue_size,
                       VIRTIO_NET_RX_QUEUE_DEFAULT_SIZE),
    DEFINE_PROP_UINT16("tx_queue_size", VirtIONet, net_conf.tx_queue_size,
                       VIRTIO_NET_TX_QUEUE_DEFAULT_SIZE),
    DEFINE_PROP_UINT16("host_mtu", VirtIONet, net_conf.mtu, 0),
    DEFINE_PROP_BOOL("x-mtu-bypass-backend", VirtIONet, mtu_bypass_backend,
                     true),
    DEFINE_PROP_INT32("speed", VirtIONet, net_conf.speed, SPEED_UNKNOWN),
    DEFINE_PROP_STRING("duplex", VirtIONet, net_conf.duplex_str),
    DEFINE_PROP_END_OF_LIST(),
};

static void virtio_net_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_CLASS(klass);

    dc->props = virtio_net_properties;
    dc->vmsd = &vmstate_virtio_net;
    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
    vdc->realize = virtio_net_device_realize;
    vdc->unrealize = virtio_net_device_unrealize;
    vdc->get_config = virtio_net_get_config;
    vdc->set_config = virtio_net_set_config;
    vdc->get_features = virtio_net_get_features;
    vdc->set_features = virtio_net_set_features;
    vdc->bad_features = virtio_net_bad_features;
    vdc->reset = virtio_net_reset;
    vdc->set_status = virtio_net_set_status;
    vdc->guest_notifier_mask = virtio_net_guest_notifier_mask;
    vdc->guest_notifier_pending = virtio_net_guest_notifier_pending;
    vdc->legacy_features |= (0x1 << VIRTIO_NET_F_GSO);
    vdc->post_load = virtio_net_post_load_virtio;
    vdc->vmsd = &vmstate_virtio_net_device;
}

static const TypeInfo virtio_net_info = {
    .name = TYPE_VIRTIO_NET,
    .parent = TYPE_VIRTIO_DEVICE,
    .instance_size = sizeof(VirtIONet),
    .instance_init = virtio_net_instance_init,
    .class_init = virtio_net_class_init,
};

static void virtio_register_types(void)
{
    type_register_static(&virtio_net_info);
}

type_init(virtio_register_types)
