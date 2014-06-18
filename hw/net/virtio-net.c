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

#include "qemu/iov.h"
#include "hw/virtio/virtio.h"
#include "net/net.h"
#include "net/checksum.h"
#include "net/tap.h"
#include "qemu/error-report.h"
#include "qemu/timer.h"
#include "hw/virtio/virtio-net.h"
#include "net/vhost_net.h"
#include "hw/virtio/virtio-bus.h"
#include "qapi/qmp/qjson.h"
#include "qapi-event.h"

#define VIRTIO_NET_VM_VERSION    11

#define MAC_TABLE_ENTRIES    64
#define MAX_VLAN    (1 << 12)   /* Per 802.1Q definition */

/*
 * Calculate the number of bytes up to and including the given 'field' of
 * 'container'.
 */
#define endof(container, field) \
    (offsetof(container, field) + sizeof(((container *)0)->field))

typedef struct VirtIOFeature {
    uint32_t flags;
    size_t end;
} VirtIOFeature;

static VirtIOFeature feature_sizes[] = {
    {.flags = 1 << VIRTIO_NET_F_MAC,
     .end = endof(struct virtio_net_config, mac)},
    {.flags = 1 << VIRTIO_NET_F_STATUS,
     .end = endof(struct virtio_net_config, status)},
    {.flags = 1 << VIRTIO_NET_F_MQ,
     .end = endof(struct virtio_net_config, max_virtqueue_pairs)},
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

    stw_p(&netcfg.status, n->status);
    stw_p(&netcfg.max_virtqueue_pairs, n->max_queues);
    memcpy(netcfg.mac, n->mac, ETH_ALEN);
    memcpy(config, &netcfg, n->config_size);
}

static void virtio_net_set_config(VirtIODevice *vdev, const uint8_t *config)
{
    VirtIONet *n = VIRTIO_NET(vdev);
    struct virtio_net_config netcfg = {};

    memcpy(&netcfg, config, n->config_size);

    if (!(vdev->guest_features >> VIRTIO_NET_F_CTRL_MAC_ADDR & 1) &&
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

static void virtio_net_announce_timer(void *opaque)
{
    VirtIONet *n = opaque;
    VirtIODevice *vdev = VIRTIO_DEVICE(n);

    n->announce_counter--;
    n->status |= VIRTIO_NET_S_ANNOUNCE;
    virtio_notify_config(vdev);
}

static void virtio_net_vhost_status(VirtIONet *n, uint8_t status)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(n);
    NetClientState *nc = qemu_get_queue(n->nic);
    int queues = n->multiqueue ? n->max_queues : 1;

    if (!get_vhost_net(nc->peer)) {
        return;
    }

    if (!!n->vhost_started ==
        (virtio_net_started(n, status) && !nc->peer->link_down)) {
        return;
    }
    if (!n->vhost_started) {
        int r;
        if (!vhost_net_query(get_vhost_net(nc->peer), vdev)) {
            return;
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

static void virtio_net_set_status(struct VirtIODevice *vdev, uint8_t status)
{
    VirtIONet *n = VIRTIO_NET(vdev);
    VirtIONetQueue *q;
    int i;
    uint8_t queue_status;

    virtio_net_vhost_status(n, status);

    for (i = 0; i < n->max_queues; i++) {
        q = &n->vqs[i];

        if ((!n->multiqueue && i != 0) || i >= n->curr_queues) {
            queue_status = 0;
        } else {
            queue_status = status;
        }

        if (!q->tx_waiting) {
            continue;
        }

        if (virtio_net_started(n, queue_status) && !n->vhost_started) {
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
                                              n->netclient_name, path, &error_abort);
        g_free(path);

        /* disable event notification to avoid events flooding */
        nc->rxfilter_notify_enabled = 0;
    }
}

static char *mac_strdup_printf(const uint8_t *mac)
{
    return g_strdup_printf("%.2x:%.2x:%.2x:%.2x:%.2x:%.2x", mac[0],
                            mac[1], mac[2], mac[3], mac[4], mac[5]);
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

    info->main_mac = mac_strdup_printf(n->mac);

    str_list = NULL;
    for (i = 0; i < n->mac_table.first_multi; i++) {
        entry = g_malloc0(sizeof(*entry));
        entry->value = mac_strdup_printf(n->mac_table.macs + i * ETH_ALEN);
        entry->next = str_list;
        str_list = entry;
    }
    info->unicast_table = str_list;

    str_list = NULL;
    for (i = n->mac_table.first_multi; i < n->mac_table.in_use; i++) {
        entry = g_malloc0(sizeof(*entry));
        entry->value = mac_strdup_printf(n->mac_table.macs + i * ETH_ALEN);
        entry->next = str_list;
        str_list = entry;
    }
    info->multicast_table = str_list;
    info->vlan_table = get_vlan_table(n);

    if (!((1 << VIRTIO_NET_F_CTRL_VLAN) & vdev->guest_features)) {
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

    /* Reset back to compatibility mode */
    n->promisc = 1;
    n->allmulti = 0;
    n->alluni = 0;
    n->nomulti = 0;
    n->nouni = 0;
    n->nobcast = 0;
    /* multiqueue is disabled by default */
    n->curr_queues = 1;
    timer_del(n->announce_timer);
    n->announce_counter = 0;
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

static void virtio_net_set_mrg_rx_bufs(VirtIONet *n, int mergeable_rx_bufs)
{
    int i;
    NetClientState *nc;

    n->mergeable_rx_bufs = mergeable_rx_bufs;

    n->guest_hdr_len = n->mergeable_rx_bufs ?
        sizeof(struct virtio_net_hdr_mrg_rxbuf) : sizeof(struct virtio_net_hdr);

    for (i = 0; i < n->max_queues; i++) {
        nc = qemu_get_subqueue(n->nic, i);

        if (peer_has_vnet_hdr(n) &&
            qemu_has_vnet_hdr_len(nc->peer, n->guest_hdr_len)) {
            qemu_set_vnet_hdr_len(nc->peer, n->guest_hdr_len);
            n->host_hdr_len = n->guest_hdr_len;
        }
    }
}

static int peer_attach(VirtIONet *n, int index)
{
    NetClientState *nc = qemu_get_subqueue(n->nic, index);

    if (!nc->peer) {
        return 0;
    }

    if (nc->peer->info->type != NET_CLIENT_OPTIONS_KIND_TAP) {
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

    if (nc->peer->info->type !=  NET_CLIENT_OPTIONS_KIND_TAP) {
        return 0;
    }

    return tap_disable(nc->peer);
}

static void virtio_net_set_queues(VirtIONet *n)
{
    int i;
    int r;

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

static uint32_t virtio_net_get_features(VirtIODevice *vdev, uint32_t features)
{
    VirtIONet *n = VIRTIO_NET(vdev);
    NetClientState *nc = qemu_get_queue(n->nic);

    features |= (1 << VIRTIO_NET_F_MAC);

    if (!peer_has_vnet_hdr(n)) {
        features &= ~(0x1 << VIRTIO_NET_F_CSUM);
        features &= ~(0x1 << VIRTIO_NET_F_HOST_TSO4);
        features &= ~(0x1 << VIRTIO_NET_F_HOST_TSO6);
        features &= ~(0x1 << VIRTIO_NET_F_HOST_ECN);

        features &= ~(0x1 << VIRTIO_NET_F_GUEST_CSUM);
        features &= ~(0x1 << VIRTIO_NET_F_GUEST_TSO4);
        features &= ~(0x1 << VIRTIO_NET_F_GUEST_TSO6);
        features &= ~(0x1 << VIRTIO_NET_F_GUEST_ECN);
    }

    if (!peer_has_vnet_hdr(n) || !peer_has_ufo(n)) {
        features &= ~(0x1 << VIRTIO_NET_F_GUEST_UFO);
        features &= ~(0x1 << VIRTIO_NET_F_HOST_UFO);
    }

    if (!get_vhost_net(nc->peer)) {
        return features;
    }
    return vhost_net_get_features(get_vhost_net(nc->peer), features);
}

static uint32_t virtio_net_bad_features(VirtIODevice *vdev)
{
    uint32_t features = 0;

    /* Linux kernel 2.6.25.  It understood MAC (as everyone must),
     * but also these: */
    features |= (1 << VIRTIO_NET_F_MAC);
    features |= (1 << VIRTIO_NET_F_CSUM);
    features |= (1 << VIRTIO_NET_F_HOST_TSO4);
    features |= (1 << VIRTIO_NET_F_HOST_TSO6);
    features |= (1 << VIRTIO_NET_F_HOST_ECN);

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

static void virtio_net_set_features(VirtIODevice *vdev, uint32_t features)
{
    VirtIONet *n = VIRTIO_NET(vdev);
    int i;

    virtio_net_set_multiqueue(n, !!(features & (1 << VIRTIO_NET_F_MQ)));

    virtio_net_set_mrg_rx_bufs(n, !!(features & (1 << VIRTIO_NET_F_MRG_RXBUF)));

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

    if ((1 << VIRTIO_NET_F_CTRL_VLAN) & features) {
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

    if (!((1 << VIRTIO_NET_F_CTRL_GUEST_OFFLOADS) & vdev->guest_features)) {
        return VIRTIO_NET_ERR;
    }

    s = iov_to_buf(iov, iov_cnt, 0, &offloads, sizeof(offloads));
    if (s != sizeof(offloads)) {
        return VIRTIO_NET_ERR;
    }

    if (cmd == VIRTIO_NET_CTRL_GUEST_OFFLOADS_SET) {
        uint64_t supported_offloads;

        if (!n->has_vnet_hdr) {
            return VIRTIO_NET_ERR;
        }

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
    mac_data.entries = ldl_p(&mac_data.entries);
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
    mac_data.entries = ldl_p(&mac_data.entries);
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
    uint16_t vid;
    size_t s;
    NetClientState *nc = qemu_get_queue(n->nic);

    s = iov_to_buf(iov, iov_cnt, 0, &vid, sizeof(vid));
    vid = lduw_p(&vid);
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
    if (cmd == VIRTIO_NET_CTRL_ANNOUNCE_ACK &&
        n->status & VIRTIO_NET_S_ANNOUNCE) {
        n->status &= ~VIRTIO_NET_S_ANNOUNCE;
        if (n->announce_counter) {
            timer_mod(n->announce_timer,
                      qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) +
                      self_announce_delay(n->announce_counter));
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

    queues = lduw_p(&mq.virtqueue_pairs);

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
    VirtQueueElement elem;
    size_t s;
    struct iovec *iov;
    unsigned int iov_cnt;

    while (virtqueue_pop(vq, &elem)) {
        if (iov_size(elem.in_sg, elem.in_num) < sizeof(status) ||
            iov_size(elem.out_sg, elem.out_num) < sizeof(ctrl)) {
            error_report("virtio-net ctrl missing headers");
            exit(1);
        }

        iov = elem.out_sg;
        iov_cnt = elem.out_num;
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

        s = iov_from_buf(elem.in_sg, elem.in_num, 0, &status, sizeof(status));
        assert(s == sizeof(status));

        virtqueue_push(vq, &elem, sizeof(status));
        virtio_notify(vdev, vq);
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
        int vid = be16_to_cpup((uint16_t *)(ptr + 14)) & 0xfff;
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

static ssize_t virtio_net_receive(NetClientState *nc, const uint8_t *buf, size_t size)
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
        VirtQueueElement elem;
        int len, total;
        const struct iovec *sg = elem.in_sg;

        total = 0;

        if (virtqueue_pop(q->rx_vq, &elem) == 0) {
            if (i == 0)
                return -1;
            error_report("virtio-net unexpected empty queue: "
                    "i %zd mergeable %d offset %zd, size %zd, "
                    "guest hdr len %zd, host hdr len %zd guest features 0x%x",
                    i, n->mergeable_rx_bufs, offset, size,
                    n->guest_hdr_len, n->host_hdr_len, vdev->guest_features);
            exit(1);
        }

        if (elem.in_num < 1) {
            error_report("virtio-net receive queue contains no in buffers");
            exit(1);
        }

        if (i == 0) {
            assert(offset == 0);
            if (n->mergeable_rx_bufs) {
                mhdr_cnt = iov_copy(mhdr_sg, ARRAY_SIZE(mhdr_sg),
                                    sg, elem.in_num,
                                    offsetof(typeof(mhdr), num_buffers),
                                    sizeof(mhdr.num_buffers));
            }

            receive_header(n, sg, elem.in_num, buf, size);
            offset = n->host_hdr_len;
            total += n->guest_hdr_len;
            guest_offset = n->guest_hdr_len;
        } else {
            guest_offset = 0;
        }

        /* copy in packet.  ugh */
        len = iov_from_buf(sg, elem.in_num, guest_offset,
                           buf + offset, size - offset);
        total += len;
        offset += len;
        /* If buffers can't be merged, at this point we
         * must have consumed the complete packet.
         * Otherwise, drop it. */
        if (!n->mergeable_rx_bufs && offset < size) {
#if 0
            error_report("virtio-net truncated non-mergeable packet: "
                         "i %zd mergeable %d offset %zd, size %zd, "
                         "guest hdr len %zd, host hdr len %zd",
                         i, n->mergeable_rx_bufs,
                         offset, size, n->guest_hdr_len, n->host_hdr_len);
#endif
            return size;
        }

        /* signal other side */
        virtqueue_fill(q->rx_vq, &elem, total, i++);
    }

    if (mhdr_cnt) {
        stw_p(&mhdr.num_buffers, i);
        iov_from_buf(mhdr_sg, mhdr_cnt,
                     0,
                     &mhdr.num_buffers, sizeof mhdr.num_buffers);
    }

    virtqueue_flush(q->rx_vq, i);
    virtio_notify(vdev, q->rx_vq);

    return size;
}

static int32_t virtio_net_flush_tx(VirtIONetQueue *q);

static void virtio_net_tx_complete(NetClientState *nc, ssize_t len)
{
    VirtIONet *n = qemu_get_nic_opaque(nc);
    VirtIONetQueue *q = virtio_net_get_subqueue(nc);
    VirtIODevice *vdev = VIRTIO_DEVICE(n);

    virtqueue_push(q->tx_vq, &q->async_tx.elem, 0);
    virtio_notify(vdev, q->tx_vq);

    q->async_tx.elem.out_num = q->async_tx.len = 0;

    virtio_queue_set_notification(q->tx_vq, 1);
    virtio_net_flush_tx(q);
}

/* TX */
static int32_t virtio_net_flush_tx(VirtIONetQueue *q)
{
    VirtIONet *n = q->n;
    VirtIODevice *vdev = VIRTIO_DEVICE(n);
    VirtQueueElement elem;
    int32_t num_packets = 0;
    int queue_index = vq2q(virtio_get_queue_index(q->tx_vq));
    if (!(vdev->status & VIRTIO_CONFIG_S_DRIVER_OK)) {
        return num_packets;
    }

    assert(vdev->vm_running);

    if (q->async_tx.elem.out_num) {
        virtio_queue_set_notification(q->tx_vq, 0);
        return num_packets;
    }

    while (virtqueue_pop(q->tx_vq, &elem)) {
        ssize_t ret, len;
        unsigned int out_num = elem.out_num;
        struct iovec *out_sg = &elem.out_sg[0];
        struct iovec sg[VIRTQUEUE_MAX_SIZE];

        if (out_num < 1) {
            error_report("virtio-net header not in first element");
            exit(1);
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

        len = n->guest_hdr_len;

        ret = qemu_sendv_packet_async(qemu_get_subqueue(n->nic, queue_index),
                                      out_sg, out_num, virtio_net_tx_complete);
        if (ret == 0) {
            virtio_queue_set_notification(q->tx_vq, 0);
            q->async_tx.elem = elem;
            q->async_tx.len  = len;
            return -EBUSY;
        }

        len += ret;

        virtqueue_push(q->tx_vq, &elem, 0);
        virtio_notify(vdev, q->tx_vq);

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

    /* This happens when device was stopped but VCPU wasn't. */
    if (!vdev->vm_running) {
        q->tx_waiting = 1;
        return;
    }

    if (q->tx_waiting) {
        virtio_queue_set_notification(vq, 1);
        timer_del(q->tx_timer);
        q->tx_waiting = 0;
        virtio_net_flush_tx(q);
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
    assert(vdev->vm_running);

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

    assert(vdev->vm_running);

    q->tx_waiting = 0;

    /* Just in case the driver is not ready on more */
    if (unlikely(!(vdev->status & VIRTIO_CONFIG_S_DRIVER_OK))) {
        return;
    }

    ret = virtio_net_flush_tx(q);
    if (ret == -EBUSY) {
        return; /* Notification re-enable handled by tx_complete */
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
    if (virtio_net_flush_tx(q) > 0) {
        virtio_queue_set_notification(q->tx_vq, 0);
        qemu_bh_schedule(q->tx_bh);
        q->tx_waiting = 1;
    }
}

static void virtio_net_set_multiqueue(VirtIONet *n, int multiqueue)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(n);
    int i, max = multiqueue ? n->max_queues : 1;

    n->multiqueue = multiqueue;

    for (i = 2; i <= n->max_queues * 2 + 1; i++) {
        virtio_del_queue(vdev, i);
    }

    for (i = 1; i < max; i++) {
        n->vqs[i].rx_vq = virtio_add_queue(vdev, 256, virtio_net_handle_rx);
        if (n->vqs[i].tx_timer) {
            n->vqs[i].tx_vq =
                virtio_add_queue(vdev, 256, virtio_net_handle_tx_timer);
            n->vqs[i].tx_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                                   virtio_net_tx_timer,
                                                   &n->vqs[i]);
        } else {
            n->vqs[i].tx_vq =
                virtio_add_queue(vdev, 256, virtio_net_handle_tx_bh);
            n->vqs[i].tx_bh = qemu_bh_new(virtio_net_tx_bh, &n->vqs[i]);
        }

        n->vqs[i].tx_waiting = 0;
        n->vqs[i].n = n;
    }

    /* Note: Minux Guests (version 3.2.1) use ctrl vq but don't ack
     * VIRTIO_NET_F_CTRL_VQ. Create ctrl vq unconditionally to avoid
     * breaking them.
     */
    n->ctrl_vq = virtio_add_queue(vdev, 64, virtio_net_handle_ctrl);

    virtio_net_set_queues(n);
}

static void virtio_net_save(QEMUFile *f, void *opaque)
{
    int i;
    VirtIONet *n = opaque;
    VirtIODevice *vdev = VIRTIO_DEVICE(n);

    /* At this point, backend must be stopped, otherwise
     * it might keep writing to memory. */
    assert(!n->vhost_started);
    virtio_save(vdev, f);

    qemu_put_buffer(f, n->mac, ETH_ALEN);
    qemu_put_be32(f, n->vqs[0].tx_waiting);
    qemu_put_be32(f, n->mergeable_rx_bufs);
    qemu_put_be16(f, n->status);
    qemu_put_byte(f, n->promisc);
    qemu_put_byte(f, n->allmulti);
    qemu_put_be32(f, n->mac_table.in_use);
    qemu_put_buffer(f, n->mac_table.macs, n->mac_table.in_use * ETH_ALEN);
    qemu_put_buffer(f, (uint8_t *)n->vlans, MAX_VLAN >> 3);
    qemu_put_be32(f, n->has_vnet_hdr);
    qemu_put_byte(f, n->mac_table.multi_overflow);
    qemu_put_byte(f, n->mac_table.uni_overflow);
    qemu_put_byte(f, n->alluni);
    qemu_put_byte(f, n->nomulti);
    qemu_put_byte(f, n->nouni);
    qemu_put_byte(f, n->nobcast);
    qemu_put_byte(f, n->has_ufo);
    if (n->max_queues > 1) {
        qemu_put_be16(f, n->max_queues);
        qemu_put_be16(f, n->curr_queues);
        for (i = 1; i < n->curr_queues; i++) {
            qemu_put_be32(f, n->vqs[i].tx_waiting);
        }
    }

    if ((1 << VIRTIO_NET_F_CTRL_GUEST_OFFLOADS) & vdev->guest_features) {
        qemu_put_be64(f, n->curr_guest_offloads);
    }
}

static int virtio_net_load(QEMUFile *f, void *opaque, int version_id)
{
    VirtIONet *n = opaque;
    VirtIODevice *vdev = VIRTIO_DEVICE(n);
    int ret, i, link_down;

    if (version_id < 2 || version_id > VIRTIO_NET_VM_VERSION)
        return -EINVAL;

    ret = virtio_load(vdev, f);
    if (ret) {
        return ret;
    }

    qemu_get_buffer(f, n->mac, ETH_ALEN);
    n->vqs[0].tx_waiting = qemu_get_be32(f);

    virtio_net_set_mrg_rx_bufs(n, qemu_get_be32(f));

    if (version_id >= 3)
        n->status = qemu_get_be16(f);

    if (version_id >= 4) {
        if (version_id < 8) {
            n->promisc = qemu_get_be32(f);
            n->allmulti = qemu_get_be32(f);
        } else {
            n->promisc = qemu_get_byte(f);
            n->allmulti = qemu_get_byte(f);
        }
    }

    if (version_id >= 5) {
        n->mac_table.in_use = qemu_get_be32(f);
        /* MAC_TABLE_ENTRIES may be different from the saved image */
        if (n->mac_table.in_use <= MAC_TABLE_ENTRIES) {
            qemu_get_buffer(f, n->mac_table.macs,
                            n->mac_table.in_use * ETH_ALEN);
        } else {
            int64_t i;

            /* Overflow detected - can happen if source has a larger MAC table.
             * We simply set overflow flag so there's no need to maintain the
             * table of addresses, discard them all.
             * Note: 64 bit math to avoid integer overflow.
             */
            for (i = 0; i < (int64_t)n->mac_table.in_use * ETH_ALEN; ++i) {
                qemu_get_byte(f);
            }
            n->mac_table.multi_overflow = n->mac_table.uni_overflow = 1;
            n->mac_table.in_use = 0;
        }
    }
 
    if (version_id >= 6)
        qemu_get_buffer(f, (uint8_t *)n->vlans, MAX_VLAN >> 3);

    if (version_id >= 7) {
        if (qemu_get_be32(f) && !peer_has_vnet_hdr(n)) {
            error_report("virtio-net: saved image requires vnet_hdr=on");
            return -1;
        }
    }

    if (version_id >= 9) {
        n->mac_table.multi_overflow = qemu_get_byte(f);
        n->mac_table.uni_overflow = qemu_get_byte(f);
    }

    if (version_id >= 10) {
        n->alluni = qemu_get_byte(f);
        n->nomulti = qemu_get_byte(f);
        n->nouni = qemu_get_byte(f);
        n->nobcast = qemu_get_byte(f);
    }

    if (version_id >= 11) {
        if (qemu_get_byte(f) && !peer_has_ufo(n)) {
            error_report("virtio-net: saved image requires TUN_F_UFO support");
            return -1;
        }
    }

    if (n->max_queues > 1) {
        if (n->max_queues != qemu_get_be16(f)) {
            error_report("virtio-net: different max_queues ");
            return -1;
        }

        n->curr_queues = qemu_get_be16(f);
        if (n->curr_queues > n->max_queues) {
            error_report("virtio-net: curr_queues %x > max_queues %x",
                         n->curr_queues, n->max_queues);
            return -1;
        }
        for (i = 1; i < n->curr_queues; i++) {
            n->vqs[i].tx_waiting = qemu_get_be32(f);
        }
    }

    if ((1 << VIRTIO_NET_F_CTRL_GUEST_OFFLOADS) & vdev->guest_features) {
        n->curr_guest_offloads = qemu_get_be64(f);
    } else {
        n->curr_guest_offloads = virtio_net_supported_guest_offloads(n);
    }

    if (peer_has_vnet_hdr(n)) {
        virtio_net_apply_guest_offloads(n);
    }

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

    if (vdev->guest_features & (0x1 << VIRTIO_NET_F_GUEST_ANNOUNCE) &&
        vdev->guest_features & (0x1 << VIRTIO_NET_F_CTRL_VQ)) {
        n->announce_counter = SELF_ANNOUNCE_ROUNDS;
        timer_mod(n->announce_timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL));
    }

    return 0;
}

static void virtio_net_cleanup(NetClientState *nc)
{
    VirtIONet *n = qemu_get_nic_opaque(nc);

    n->nic = NULL;
}

static NetClientInfo net_virtio_info = {
    .type = NET_CLIENT_OPTIONS_KIND_NIC,
    .size = sizeof(NICState),
    .can_receive = virtio_net_can_receive,
    .receive = virtio_net_receive,
    .cleanup = virtio_net_cleanup,
    .link_status_changed = virtio_net_set_link_status,
    .query_rx_filter = virtio_net_query_rxfilter,
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

void virtio_net_set_config_size(VirtIONet *n, uint32_t host_features)
{
    int i, config_size = 0;
    host_features |= (1 << VIRTIO_NET_F_MAC);
    for (i = 0; feature_sizes[i].flags != 0; i++) {
        if (host_features & feature_sizes[i].flags) {
            config_size = MAX(feature_sizes[i].end, config_size);
        }
    }
    n->config_size = config_size;
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

    virtio_init(vdev, "virtio-net", VIRTIO_ID_NET, n->config_size);

    n->max_queues = MAX(n->nic_conf.queues, 1);
    n->vqs = g_malloc0(sizeof(VirtIONetQueue) * n->max_queues);
    n->vqs[0].rx_vq = virtio_add_queue(vdev, 256, virtio_net_handle_rx);
    n->curr_queues = 1;
    n->vqs[0].n = n;
    n->tx_timeout = n->net_conf.txtimer;

    if (n->net_conf.tx && strcmp(n->net_conf.tx, "timer")
                       && strcmp(n->net_conf.tx, "bh")) {
        error_report("virtio-net: "
                     "Unknown option tx=%s, valid options: \"timer\" \"bh\"",
                     n->net_conf.tx);
        error_report("Defaulting to \"bh\"");
    }

    if (n->net_conf.tx && !strcmp(n->net_conf.tx, "timer")) {
        n->vqs[0].tx_vq = virtio_add_queue(vdev, 256,
                                           virtio_net_handle_tx_timer);
        n->vqs[0].tx_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, virtio_net_tx_timer,
                                               &n->vqs[0]);
    } else {
        n->vqs[0].tx_vq = virtio_add_queue(vdev, 256,
                                           virtio_net_handle_tx_bh);
        n->vqs[0].tx_bh = qemu_bh_new(virtio_net_tx_bh, &n->vqs[0]);
    }
    n->ctrl_vq = virtio_add_queue(vdev, 64, virtio_net_handle_ctrl);
    qemu_macaddr_default_if_unset(&n->nic_conf.macaddr);
    memcpy(&n->mac[0], &n->nic_conf.macaddr, sizeof(n->mac));
    n->status = VIRTIO_NET_S_LINK_UP;
    n->announce_timer = timer_new_ms(QEMU_CLOCK_VIRTUAL,
                                     virtio_net_announce_timer, n);

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
    virtio_net_set_mrg_rx_bufs(n, 0);
    n->promisc = 1; /* for compatibility */

    n->mac_table.macs = g_malloc0(MAC_TABLE_ENTRIES * ETH_ALEN);

    n->vlans = g_malloc0(MAX_VLAN >> 3);

    nc = qemu_get_queue(n->nic);
    nc->rxfilter_notify_enabled = 1;

    n->qdev = dev;
    register_savevm(dev, "virtio-net", -1, VIRTIO_NET_VM_VERSION,
                    virtio_net_save, virtio_net_load, n);

    add_boot_device_path(n->nic_conf.bootindex, dev, "/ethernet-phy@0");
}

static void virtio_net_device_unrealize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    VirtIONet *n = VIRTIO_NET(dev);
    int i;

    /* This will stop vhost backend if appropriate. */
    virtio_net_set_status(vdev, 0);

    unregister_savevm(dev, "virtio-net", n);

    g_free(n->netclient_name);
    n->netclient_name = NULL;
    g_free(n->netclient_type);
    n->netclient_type = NULL;

    g_free(n->mac_table.macs);
    g_free(n->vlans);

    for (i = 0; i < n->max_queues; i++) {
        VirtIONetQueue *q = &n->vqs[i];
        NetClientState *nc = qemu_get_subqueue(n->nic, i);

        qemu_purge_queued_packets(nc);

        if (q->tx_timer) {
            timer_del(q->tx_timer);
            timer_free(q->tx_timer);
        } else if (q->tx_bh) {
            qemu_bh_delete(q->tx_bh);
        }
    }

    timer_del(n->announce_timer);
    timer_free(n->announce_timer);
    g_free(n->vqs);
    qemu_del_nic(n->nic);
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
}

static Property virtio_net_properties[] = {
    DEFINE_NIC_PROPERTIES(VirtIONet, nic_conf),
    DEFINE_PROP_UINT32("x-txtimer", VirtIONet, net_conf.txtimer,
                                               TX_TIMER_INTERVAL),
    DEFINE_PROP_INT32("x-txburst", VirtIONet, net_conf.txburst, TX_BURST),
    DEFINE_PROP_STRING("tx", VirtIONet, net_conf.tx),
    DEFINE_PROP_END_OF_LIST(),
};

static void virtio_net_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_CLASS(klass);

    dc->props = virtio_net_properties;
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
