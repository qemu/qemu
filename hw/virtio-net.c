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

#include "iov.h"
#include "virtio.h"
#include "net.h"
#include "net/checksum.h"
#include "net/tap.h"
#include "qemu-error.h"
#include "qemu-timer.h"
#include "virtio-net.h"
#include "vhost_net.h"

#define VIRTIO_NET_VM_VERSION    11

#define MAC_TABLE_ENTRIES    64
#define MAX_VLAN    (1 << 12)   /* Per 802.1Q definition */

typedef struct VirtIONet
{
    VirtIODevice vdev;
    uint8_t mac[ETH_ALEN];
    uint16_t status;
    VirtQueue *rx_vq;
    VirtQueue *tx_vq;
    VirtQueue *ctrl_vq;
    NICState *nic;
    QEMUTimer *tx_timer;
    QEMUBH *tx_bh;
    uint32_t tx_timeout;
    int32_t tx_burst;
    int tx_waiting;
    uint32_t has_vnet_hdr;
    uint8_t has_ufo;
    struct {
        VirtQueueElement elem;
        ssize_t len;
    } async_tx;
    int mergeable_rx_bufs;
    uint8_t promisc;
    uint8_t allmulti;
    uint8_t alluni;
    uint8_t nomulti;
    uint8_t nouni;
    uint8_t nobcast;
    uint8_t vhost_started;
    struct {
        int in_use;
        int first_multi;
        uint8_t multi_overflow;
        uint8_t uni_overflow;
        uint8_t *macs;
    } mac_table;
    uint32_t *vlans;
    DeviceState *qdev;
} VirtIONet;

/* TODO
 * - we could suppress RX interrupt if we were so inclined.
 */

static VirtIONet *to_virtio_net(VirtIODevice *vdev)
{
    return (VirtIONet *)vdev;
}

static void virtio_net_get_config(VirtIODevice *vdev, uint8_t *config)
{
    VirtIONet *n = to_virtio_net(vdev);
    struct virtio_net_config netcfg;

    stw_p(&netcfg.status, n->status);
    memcpy(netcfg.mac, n->mac, ETH_ALEN);
    memcpy(config, &netcfg, sizeof(netcfg));
}

static void virtio_net_set_config(VirtIODevice *vdev, const uint8_t *config)
{
    VirtIONet *n = to_virtio_net(vdev);
    struct virtio_net_config netcfg;

    memcpy(&netcfg, config, sizeof(netcfg));

    if (memcmp(netcfg.mac, n->mac, ETH_ALEN)) {
        memcpy(n->mac, netcfg.mac, ETH_ALEN);
        qemu_format_nic_info_str(&n->nic->nc, n->mac);
    }
}

static bool virtio_net_started(VirtIONet *n, uint8_t status)
{
    return (status & VIRTIO_CONFIG_S_DRIVER_OK) &&
        (n->status & VIRTIO_NET_S_LINK_UP) && n->vdev.vm_running;
}

static void virtio_net_vhost_status(VirtIONet *n, uint8_t status)
{
    if (!n->nic->nc.peer) {
        return;
    }
    if (n->nic->nc.peer->info->type != NET_CLIENT_TYPE_TAP) {
        return;
    }

    if (!tap_get_vhost_net(n->nic->nc.peer)) {
        return;
    }
    if (!!n->vhost_started == virtio_net_started(n, status) &&
                              !n->nic->nc.peer->link_down) {
        return;
    }
    if (!n->vhost_started) {
        int r;
        if (!vhost_net_query(tap_get_vhost_net(n->nic->nc.peer), &n->vdev)) {
            return;
        }
        r = vhost_net_start(tap_get_vhost_net(n->nic->nc.peer), &n->vdev);
        if (r < 0) {
            error_report("unable to start vhost net: %d: "
                         "falling back on userspace virtio", -r);
        } else {
            n->vhost_started = 1;
        }
    } else {
        vhost_net_stop(tap_get_vhost_net(n->nic->nc.peer), &n->vdev);
        n->vhost_started = 0;
    }
}

static void virtio_net_set_status(struct VirtIODevice *vdev, uint8_t status)
{
    VirtIONet *n = to_virtio_net(vdev);

    virtio_net_vhost_status(n, status);

    if (!n->tx_waiting) {
        return;
    }

    if (virtio_net_started(n, status) && !n->vhost_started) {
        if (n->tx_timer) {
            qemu_mod_timer(n->tx_timer,
                           qemu_get_clock_ns(vm_clock) + n->tx_timeout);
        } else {
            qemu_bh_schedule(n->tx_bh);
        }
    } else {
        if (n->tx_timer) {
            qemu_del_timer(n->tx_timer);
        } else {
            qemu_bh_cancel(n->tx_bh);
        }
    }
}

static void virtio_net_set_link_status(VLANClientState *nc)
{
    VirtIONet *n = DO_UPCAST(NICState, nc, nc)->opaque;
    uint16_t old_status = n->status;

    if (nc->link_down)
        n->status &= ~VIRTIO_NET_S_LINK_UP;
    else
        n->status |= VIRTIO_NET_S_LINK_UP;

    if (n->status != old_status)
        virtio_notify_config(&n->vdev);

    virtio_net_set_status(&n->vdev, n->vdev.status);
}

static void virtio_net_reset(VirtIODevice *vdev)
{
    VirtIONet *n = to_virtio_net(vdev);

    /* Reset back to compatibility mode */
    n->promisc = 1;
    n->allmulti = 0;
    n->alluni = 0;
    n->nomulti = 0;
    n->nouni = 0;
    n->nobcast = 0;

    /* Flush any MAC and VLAN filter table state */
    n->mac_table.in_use = 0;
    n->mac_table.first_multi = 0;
    n->mac_table.multi_overflow = 0;
    n->mac_table.uni_overflow = 0;
    memset(n->mac_table.macs, 0, MAC_TABLE_ENTRIES * ETH_ALEN);
    memset(n->vlans, 0, MAX_VLAN >> 3);
}

static int peer_has_vnet_hdr(VirtIONet *n)
{
    if (!n->nic->nc.peer)
        return 0;

    if (n->nic->nc.peer->info->type != NET_CLIENT_TYPE_TAP)
        return 0;

    n->has_vnet_hdr = tap_has_vnet_hdr(n->nic->nc.peer);

    return n->has_vnet_hdr;
}

static int peer_has_ufo(VirtIONet *n)
{
    if (!peer_has_vnet_hdr(n))
        return 0;

    n->has_ufo = tap_has_ufo(n->nic->nc.peer);

    return n->has_ufo;
}

static uint32_t virtio_net_get_features(VirtIODevice *vdev, uint32_t features)
{
    VirtIONet *n = to_virtio_net(vdev);

    features |= (1 << VIRTIO_NET_F_MAC);

    if (peer_has_vnet_hdr(n)) {
        tap_using_vnet_hdr(n->nic->nc.peer, 1);
    } else {
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

    if (!n->nic->nc.peer ||
        n->nic->nc.peer->info->type != NET_CLIENT_TYPE_TAP) {
        return features;
    }
    if (!tap_get_vhost_net(n->nic->nc.peer)) {
        return features;
    }
    return vhost_net_get_features(tap_get_vhost_net(n->nic->nc.peer), features);
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

static void virtio_net_set_features(VirtIODevice *vdev, uint32_t features)
{
    VirtIONet *n = to_virtio_net(vdev);

    n->mergeable_rx_bufs = !!(features & (1 << VIRTIO_NET_F_MRG_RXBUF));

    if (n->has_vnet_hdr) {
        tap_set_offload(n->nic->nc.peer,
                        (features >> VIRTIO_NET_F_GUEST_CSUM) & 1,
                        (features >> VIRTIO_NET_F_GUEST_TSO4) & 1,
                        (features >> VIRTIO_NET_F_GUEST_TSO6) & 1,
                        (features >> VIRTIO_NET_F_GUEST_ECN)  & 1,
                        (features >> VIRTIO_NET_F_GUEST_UFO)  & 1);
    }
    if (!n->nic->nc.peer ||
        n->nic->nc.peer->info->type != NET_CLIENT_TYPE_TAP) {
        return;
    }
    if (!tap_get_vhost_net(n->nic->nc.peer)) {
        return;
    }
    vhost_net_ack_features(tap_get_vhost_net(n->nic->nc.peer), features);
}

static int virtio_net_handle_rx_mode(VirtIONet *n, uint8_t cmd,
                                     VirtQueueElement *elem)
{
    uint8_t on;

    if (elem->out_num != 2 || elem->out_sg[1].iov_len != sizeof(on)) {
        error_report("virtio-net ctrl invalid rx mode command");
        exit(1);
    }

    on = ldub_p(elem->out_sg[1].iov_base);

    if (cmd == VIRTIO_NET_CTRL_RX_MODE_PROMISC)
        n->promisc = on;
    else if (cmd == VIRTIO_NET_CTRL_RX_MODE_ALLMULTI)
        n->allmulti = on;
    else if (cmd == VIRTIO_NET_CTRL_RX_MODE_ALLUNI)
        n->alluni = on;
    else if (cmd == VIRTIO_NET_CTRL_RX_MODE_NOMULTI)
        n->nomulti = on;
    else if (cmd == VIRTIO_NET_CTRL_RX_MODE_NOUNI)
        n->nouni = on;
    else if (cmd == VIRTIO_NET_CTRL_RX_MODE_NOBCAST)
        n->nobcast = on;
    else
        return VIRTIO_NET_ERR;

    return VIRTIO_NET_OK;
}

static int virtio_net_handle_mac(VirtIONet *n, uint8_t cmd,
                                 VirtQueueElement *elem)
{
    struct virtio_net_ctrl_mac mac_data;

    if (cmd != VIRTIO_NET_CTRL_MAC_TABLE_SET || elem->out_num != 3 ||
        elem->out_sg[1].iov_len < sizeof(mac_data) ||
        elem->out_sg[2].iov_len < sizeof(mac_data))
        return VIRTIO_NET_ERR;

    n->mac_table.in_use = 0;
    n->mac_table.first_multi = 0;
    n->mac_table.uni_overflow = 0;
    n->mac_table.multi_overflow = 0;
    memset(n->mac_table.macs, 0, MAC_TABLE_ENTRIES * ETH_ALEN);

    mac_data.entries = ldl_p(elem->out_sg[1].iov_base);

    if (sizeof(mac_data.entries) +
        (mac_data.entries * ETH_ALEN) > elem->out_sg[1].iov_len)
        return VIRTIO_NET_ERR;

    if (mac_data.entries <= MAC_TABLE_ENTRIES) {
        memcpy(n->mac_table.macs, elem->out_sg[1].iov_base + sizeof(mac_data),
               mac_data.entries * ETH_ALEN);
        n->mac_table.in_use += mac_data.entries;
    } else {
        n->mac_table.uni_overflow = 1;
    }

    n->mac_table.first_multi = n->mac_table.in_use;

    mac_data.entries = ldl_p(elem->out_sg[2].iov_base);

    if (sizeof(mac_data.entries) +
        (mac_data.entries * ETH_ALEN) > elem->out_sg[2].iov_len)
        return VIRTIO_NET_ERR;

    if (mac_data.entries) {
        if (n->mac_table.in_use + mac_data.entries <= MAC_TABLE_ENTRIES) {
            memcpy(n->mac_table.macs + (n->mac_table.in_use * ETH_ALEN),
                   elem->out_sg[2].iov_base + sizeof(mac_data),
                   mac_data.entries * ETH_ALEN);
            n->mac_table.in_use += mac_data.entries;
        } else {
            n->mac_table.multi_overflow = 1;
        }
    }

    return VIRTIO_NET_OK;
}

static int virtio_net_handle_vlan_table(VirtIONet *n, uint8_t cmd,
                                        VirtQueueElement *elem)
{
    uint16_t vid;

    if (elem->out_num != 2 || elem->out_sg[1].iov_len != sizeof(vid)) {
        error_report("virtio-net ctrl invalid vlan command");
        return VIRTIO_NET_ERR;
    }

    vid = lduw_p(elem->out_sg[1].iov_base);

    if (vid >= MAX_VLAN)
        return VIRTIO_NET_ERR;

    if (cmd == VIRTIO_NET_CTRL_VLAN_ADD)
        n->vlans[vid >> 5] |= (1U << (vid & 0x1f));
    else if (cmd == VIRTIO_NET_CTRL_VLAN_DEL)
        n->vlans[vid >> 5] &= ~(1U << (vid & 0x1f));
    else
        return VIRTIO_NET_ERR;

    return VIRTIO_NET_OK;
}

static void virtio_net_handle_ctrl(VirtIODevice *vdev, VirtQueue *vq)
{
    VirtIONet *n = to_virtio_net(vdev);
    struct virtio_net_ctrl_hdr ctrl;
    virtio_net_ctrl_ack status = VIRTIO_NET_ERR;
    VirtQueueElement elem;

    while (virtqueue_pop(vq, &elem)) {
        if ((elem.in_num < 1) || (elem.out_num < 1)) {
            error_report("virtio-net ctrl missing headers");
            exit(1);
        }

        if (elem.out_sg[0].iov_len < sizeof(ctrl) ||
            elem.in_sg[elem.in_num - 1].iov_len < sizeof(status)) {
            error_report("virtio-net ctrl header not in correct element");
            exit(1);
        }

        ctrl.class = ldub_p(elem.out_sg[0].iov_base);
        ctrl.cmd = ldub_p(elem.out_sg[0].iov_base + sizeof(ctrl.class));

        if (ctrl.class == VIRTIO_NET_CTRL_RX_MODE)
            status = virtio_net_handle_rx_mode(n, ctrl.cmd, &elem);
        else if (ctrl.class == VIRTIO_NET_CTRL_MAC)
            status = virtio_net_handle_mac(n, ctrl.cmd, &elem);
        else if (ctrl.class == VIRTIO_NET_CTRL_VLAN)
            status = virtio_net_handle_vlan_table(n, ctrl.cmd, &elem);

        stb_p(elem.in_sg[elem.in_num - 1].iov_base, status);

        virtqueue_push(vq, &elem, sizeof(status));
        virtio_notify(vdev, vq);
    }
}

/* RX */

static void virtio_net_handle_rx(VirtIODevice *vdev, VirtQueue *vq)
{
    VirtIONet *n = to_virtio_net(vdev);

    qemu_flush_queued_packets(&n->nic->nc);

    /* We now have RX buffers, signal to the IO thread to break out of the
     * select to re-poll the tap file descriptor */
    qemu_notify_event();
}

static int virtio_net_can_receive(VLANClientState *nc)
{
    VirtIONet *n = DO_UPCAST(NICState, nc, nc)->opaque;
    if (!n->vdev.vm_running) {
        return 0;
    }

    if (!virtio_queue_ready(n->rx_vq) ||
        !(n->vdev.status & VIRTIO_CONFIG_S_DRIVER_OK))
        return 0;

    return 1;
}

static int virtio_net_has_buffers(VirtIONet *n, int bufsize)
{
    if (virtio_queue_empty(n->rx_vq) ||
        (n->mergeable_rx_bufs &&
         !virtqueue_avail_bytes(n->rx_vq, bufsize, 0))) {
        virtio_queue_set_notification(n->rx_vq, 1);

        /* To avoid a race condition where the guest has made some buffers
         * available after the above check but before notification was
         * enabled, check for available buffers again.
         */
        if (virtio_queue_empty(n->rx_vq) ||
            (n->mergeable_rx_bufs &&
             !virtqueue_avail_bytes(n->rx_vq, bufsize, 0)))
            return 0;
    }

    virtio_queue_set_notification(n->rx_vq, 0);
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
                                        const uint8_t *buf, size_t size)
{
    if ((hdr->flags & VIRTIO_NET_HDR_F_NEEDS_CSUM) && /* missing csum */
        (size > 27 && size < 1500) && /* normal sized MTU */
        (buf[12] == 0x08 && buf[13] == 0x00) && /* ethertype == IPv4 */
        (buf[23] == 17) && /* ip.protocol == UDP */
        (buf[34] == 0 && buf[35] == 67)) { /* udp.srcport == bootps */
        /* FIXME this cast is evil */
        net_checksum_calculate((uint8_t *)buf, size);
        hdr->flags &= ~VIRTIO_NET_HDR_F_NEEDS_CSUM;
    }
}

static int receive_header(VirtIONet *n, struct iovec *iov, int iovcnt,
                          const void *buf, size_t size, size_t hdr_len)
{
    struct virtio_net_hdr *hdr = (struct virtio_net_hdr *)iov[0].iov_base;
    int offset = 0;

    hdr->flags = 0;
    hdr->gso_type = VIRTIO_NET_HDR_GSO_NONE;

    if (n->has_vnet_hdr) {
        memcpy(hdr, buf, sizeof(*hdr));
        offset = sizeof(*hdr);
        work_around_broken_dhclient(hdr, buf + offset, size - offset);
    }

    /* We only ever receive a struct virtio_net_hdr from the tapfd,
     * but we may be passing along a larger header to the guest.
     */
    iov[0].iov_base += hdr_len;
    iov[0].iov_len  -= hdr_len;

    return offset;
}

static int receive_filter(VirtIONet *n, const uint8_t *buf, int size)
{
    static const uint8_t bcast[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    static const uint8_t vlan[] = {0x81, 0x00};
    uint8_t *ptr = (uint8_t *)buf;
    int i;

    if (n->promisc)
        return 1;

    if (n->has_vnet_hdr) {
        ptr += sizeof(struct virtio_net_hdr);
    }

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

static ssize_t virtio_net_receive(VLANClientState *nc, const uint8_t *buf, size_t size)
{
    VirtIONet *n = DO_UPCAST(NICState, nc, nc)->opaque;
    struct virtio_net_hdr_mrg_rxbuf *mhdr = NULL;
    size_t guest_hdr_len, offset, i, host_hdr_len;

    if (!virtio_net_can_receive(&n->nic->nc))
        return -1;

    /* hdr_len refers to the header we supply to the guest */
    guest_hdr_len = n->mergeable_rx_bufs ?
        sizeof(struct virtio_net_hdr_mrg_rxbuf) : sizeof(struct virtio_net_hdr);


    host_hdr_len = n->has_vnet_hdr ? sizeof(struct virtio_net_hdr) : 0;
    if (!virtio_net_has_buffers(n, size + guest_hdr_len - host_hdr_len))
        return 0;

    if (!receive_filter(n, buf, size))
        return size;

    offset = i = 0;

    while (offset < size) {
        VirtQueueElement elem;
        int len, total;
        struct iovec sg[VIRTQUEUE_MAX_SIZE];

        total = 0;

        if (virtqueue_pop(n->rx_vq, &elem) == 0) {
            if (i == 0)
                return -1;
            error_report("virtio-net unexpected empty queue: "
                    "i %zd mergeable %d offset %zd, size %zd, "
                    "guest hdr len %zd, host hdr len %zd guest features 0x%x",
                    i, n->mergeable_rx_bufs, offset, size,
                    guest_hdr_len, host_hdr_len, n->vdev.guest_features);
            exit(1);
        }

        if (elem.in_num < 1) {
            error_report("virtio-net receive queue contains no in buffers");
            exit(1);
        }

        if (!n->mergeable_rx_bufs && elem.in_sg[0].iov_len != guest_hdr_len) {
            error_report("virtio-net header not in first element");
            exit(1);
        }

        memcpy(&sg, &elem.in_sg[0], sizeof(sg[0]) * elem.in_num);

        if (i == 0) {
            if (n->mergeable_rx_bufs)
                mhdr = (struct virtio_net_hdr_mrg_rxbuf *)sg[0].iov_base;

            offset += receive_header(n, sg, elem.in_num,
                                     buf + offset, size - offset, guest_hdr_len);
            total += guest_hdr_len;
        }

        /* copy in packet.  ugh */
        len = iov_from_buf(sg, elem.in_num,
                           buf + offset, 0, size - offset);
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
                         offset, size, guest_hdr_len, host_hdr_len);
#endif
            return size;
        }

        /* signal other side */
        virtqueue_fill(n->rx_vq, &elem, total, i++);
    }

    if (mhdr) {
        stw_p(&mhdr->num_buffers, i);
    }

    virtqueue_flush(n->rx_vq, i);
    virtio_notify(&n->vdev, n->rx_vq);

    return size;
}

static int32_t virtio_net_flush_tx(VirtIONet *n, VirtQueue *vq);

static void virtio_net_tx_complete(VLANClientState *nc, ssize_t len)
{
    VirtIONet *n = DO_UPCAST(NICState, nc, nc)->opaque;

    virtqueue_push(n->tx_vq, &n->async_tx.elem, n->async_tx.len);
    virtio_notify(&n->vdev, n->tx_vq);

    n->async_tx.elem.out_num = n->async_tx.len = 0;

    virtio_queue_set_notification(n->tx_vq, 1);
    virtio_net_flush_tx(n, n->tx_vq);
}

/* TX */
static int32_t virtio_net_flush_tx(VirtIONet *n, VirtQueue *vq)
{
    VirtQueueElement elem;
    int32_t num_packets = 0;
    if (!(n->vdev.status & VIRTIO_CONFIG_S_DRIVER_OK)) {
        return num_packets;
    }

    assert(n->vdev.vm_running);

    if (n->async_tx.elem.out_num) {
        virtio_queue_set_notification(n->tx_vq, 0);
        return num_packets;
    }

    while (virtqueue_pop(vq, &elem)) {
        ssize_t ret, len = 0;
        unsigned int out_num = elem.out_num;
        struct iovec *out_sg = &elem.out_sg[0];
        unsigned hdr_len;

        /* hdr_len refers to the header received from the guest */
        hdr_len = n->mergeable_rx_bufs ?
            sizeof(struct virtio_net_hdr_mrg_rxbuf) :
            sizeof(struct virtio_net_hdr);

        if (out_num < 1 || out_sg->iov_len != hdr_len) {
            error_report("virtio-net header not in first element");
            exit(1);
        }

        /* ignore the header if GSO is not supported */
        if (!n->has_vnet_hdr) {
            out_num--;
            out_sg++;
            len += hdr_len;
        } else if (n->mergeable_rx_bufs) {
            /* tapfd expects a struct virtio_net_hdr */
            hdr_len -= sizeof(struct virtio_net_hdr);
            out_sg->iov_len -= hdr_len;
            len += hdr_len;
        }

        ret = qemu_sendv_packet_async(&n->nic->nc, out_sg, out_num,
                                      virtio_net_tx_complete);
        if (ret == 0) {
            virtio_queue_set_notification(n->tx_vq, 0);
            n->async_tx.elem = elem;
            n->async_tx.len  = len;
            return -EBUSY;
        }

        len += ret;

        virtqueue_push(vq, &elem, len);
        virtio_notify(&n->vdev, vq);

        if (++num_packets >= n->tx_burst) {
            break;
        }
    }
    return num_packets;
}

static void virtio_net_handle_tx_timer(VirtIODevice *vdev, VirtQueue *vq)
{
    VirtIONet *n = to_virtio_net(vdev);

    /* This happens when device was stopped but VCPU wasn't. */
    if (!n->vdev.vm_running) {
        n->tx_waiting = 1;
        return;
    }

    if (n->tx_waiting) {
        virtio_queue_set_notification(vq, 1);
        qemu_del_timer(n->tx_timer);
        n->tx_waiting = 0;
        virtio_net_flush_tx(n, vq);
    } else {
        qemu_mod_timer(n->tx_timer,
                       qemu_get_clock_ns(vm_clock) + n->tx_timeout);
        n->tx_waiting = 1;
        virtio_queue_set_notification(vq, 0);
    }
}

static void virtio_net_handle_tx_bh(VirtIODevice *vdev, VirtQueue *vq)
{
    VirtIONet *n = to_virtio_net(vdev);

    if (unlikely(n->tx_waiting)) {
        return;
    }
    n->tx_waiting = 1;
    /* This happens when device was stopped but VCPU wasn't. */
    if (!n->vdev.vm_running) {
        return;
    }
    virtio_queue_set_notification(vq, 0);
    qemu_bh_schedule(n->tx_bh);
}

static void virtio_net_tx_timer(void *opaque)
{
    VirtIONet *n = opaque;
    assert(n->vdev.vm_running);

    n->tx_waiting = 0;

    /* Just in case the driver is not ready on more */
    if (!(n->vdev.status & VIRTIO_CONFIG_S_DRIVER_OK))
        return;

    virtio_queue_set_notification(n->tx_vq, 1);
    virtio_net_flush_tx(n, n->tx_vq);
}

static void virtio_net_tx_bh(void *opaque)
{
    VirtIONet *n = opaque;
    int32_t ret;

    assert(n->vdev.vm_running);

    n->tx_waiting = 0;

    /* Just in case the driver is not ready on more */
    if (unlikely(!(n->vdev.status & VIRTIO_CONFIG_S_DRIVER_OK)))
        return;

    ret = virtio_net_flush_tx(n, n->tx_vq);
    if (ret == -EBUSY) {
        return; /* Notification re-enable handled by tx_complete */
    }

    /* If we flush a full burst of packets, assume there are
     * more coming and immediately reschedule */
    if (ret >= n->tx_burst) {
        qemu_bh_schedule(n->tx_bh);
        n->tx_waiting = 1;
        return;
    }

    /* If less than a full burst, re-enable notification and flush
     * anything that may have come in while we weren't looking.  If
     * we find something, assume the guest is still active and reschedule */
    virtio_queue_set_notification(n->tx_vq, 1);
    if (virtio_net_flush_tx(n, n->tx_vq) > 0) {
        virtio_queue_set_notification(n->tx_vq, 0);
        qemu_bh_schedule(n->tx_bh);
        n->tx_waiting = 1;
    }
}

static void virtio_net_save(QEMUFile *f, void *opaque)
{
    VirtIONet *n = opaque;

    /* At this point, backend must be stopped, otherwise
     * it might keep writing to memory. */
    assert(!n->vhost_started);
    virtio_save(&n->vdev, f);

    qemu_put_buffer(f, n->mac, ETH_ALEN);
    qemu_put_be32(f, n->tx_waiting);
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
}

static int virtio_net_load(QEMUFile *f, void *opaque, int version_id)
{
    VirtIONet *n = opaque;
    int i;

    if (version_id < 2 || version_id > VIRTIO_NET_VM_VERSION)
        return -EINVAL;

    virtio_load(&n->vdev, f);

    qemu_get_buffer(f, n->mac, ETH_ALEN);
    n->tx_waiting = qemu_get_be32(f);
    n->mergeable_rx_bufs = qemu_get_be32(f);

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
        } else if (n->mac_table.in_use) {
            qemu_fseek(f, n->mac_table.in_use * ETH_ALEN, SEEK_CUR);
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

        if (n->has_vnet_hdr) {
            tap_using_vnet_hdr(n->nic->nc.peer, 1);
            tap_set_offload(n->nic->nc.peer,
                    (n->vdev.guest_features >> VIRTIO_NET_F_GUEST_CSUM) & 1,
                    (n->vdev.guest_features >> VIRTIO_NET_F_GUEST_TSO4) & 1,
                    (n->vdev.guest_features >> VIRTIO_NET_F_GUEST_TSO6) & 1,
                    (n->vdev.guest_features >> VIRTIO_NET_F_GUEST_ECN)  & 1,
                    (n->vdev.guest_features >> VIRTIO_NET_F_GUEST_UFO)  & 1);
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

    /* Find the first multicast entry in the saved MAC filter */
    for (i = 0; i < n->mac_table.in_use; i++) {
        if (n->mac_table.macs[i * ETH_ALEN] & 1) {
            break;
        }
    }
    n->mac_table.first_multi = i;
    return 0;
}

static void virtio_net_cleanup(VLANClientState *nc)
{
    VirtIONet *n = DO_UPCAST(NICState, nc, nc)->opaque;

    n->nic = NULL;
}

static NetClientInfo net_virtio_info = {
    .type = NET_CLIENT_TYPE_NIC,
    .size = sizeof(NICState),
    .can_receive = virtio_net_can_receive,
    .receive = virtio_net_receive,
        .cleanup = virtio_net_cleanup,
    .link_status_changed = virtio_net_set_link_status,
};

VirtIODevice *virtio_net_init(DeviceState *dev, NICConf *conf,
                              virtio_net_conf *net)
{
    VirtIONet *n;

    n = (VirtIONet *)virtio_common_init("virtio-net", VIRTIO_ID_NET,
                                        sizeof(struct virtio_net_config),
                                        sizeof(VirtIONet));

    n->vdev.get_config = virtio_net_get_config;
    n->vdev.set_config = virtio_net_set_config;
    n->vdev.get_features = virtio_net_get_features;
    n->vdev.set_features = virtio_net_set_features;
    n->vdev.bad_features = virtio_net_bad_features;
    n->vdev.reset = virtio_net_reset;
    n->vdev.set_status = virtio_net_set_status;
    n->rx_vq = virtio_add_queue(&n->vdev, 256, virtio_net_handle_rx);

    if (net->tx && strcmp(net->tx, "timer") && strcmp(net->tx, "bh")) {
        error_report("virtio-net: "
                     "Unknown option tx=%s, valid options: \"timer\" \"bh\"",
                     net->tx);
        error_report("Defaulting to \"bh\"");
    }

    if (net->tx && !strcmp(net->tx, "timer")) {
        n->tx_vq = virtio_add_queue(&n->vdev, 256, virtio_net_handle_tx_timer);
        n->tx_timer = qemu_new_timer_ns(vm_clock, virtio_net_tx_timer, n);
        n->tx_timeout = net->txtimer;
    } else {
        n->tx_vq = virtio_add_queue(&n->vdev, 256, virtio_net_handle_tx_bh);
        n->tx_bh = qemu_bh_new(virtio_net_tx_bh, n);
    }
    n->ctrl_vq = virtio_add_queue(&n->vdev, 64, virtio_net_handle_ctrl);
    qemu_macaddr_default_if_unset(&conf->macaddr);
    memcpy(&n->mac[0], &conf->macaddr, sizeof(n->mac));
    n->status = VIRTIO_NET_S_LINK_UP;

    n->nic = qemu_new_nic(&net_virtio_info, conf, dev->info->name, dev->id, n);

    qemu_format_nic_info_str(&n->nic->nc, conf->macaddr.a);

    n->tx_waiting = 0;
    n->tx_burst = net->txburst;
    n->mergeable_rx_bufs = 0;
    n->promisc = 1; /* for compatibility */

    n->mac_table.macs = qemu_mallocz(MAC_TABLE_ENTRIES * ETH_ALEN);

    n->vlans = qemu_mallocz(MAX_VLAN >> 3);

    n->qdev = dev;
    register_savevm(dev, "virtio-net", -1, VIRTIO_NET_VM_VERSION,
                    virtio_net_save, virtio_net_load, n);

    add_boot_device_path(conf->bootindex, dev, "/ethernet-phy@0");

    return &n->vdev;
}

void virtio_net_exit(VirtIODevice *vdev)
{
    VirtIONet *n = DO_UPCAST(VirtIONet, vdev, vdev);

    /* This will stop vhost backend if appropriate. */
    virtio_net_set_status(vdev, 0);

    qemu_purge_queued_packets(&n->nic->nc);

    unregister_savevm(n->qdev, "virtio-net", n);

    qemu_free(n->mac_table.macs);
    qemu_free(n->vlans);

    if (n->tx_timer) {
        qemu_del_timer(n->tx_timer);
        qemu_free_timer(n->tx_timer);
    } else {
        qemu_bh_delete(n->tx_bh);
    }

    qemu_del_vlan_client(&n->nic->nc);
    virtio_cleanup(&n->vdev);
}
