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

#include "virtio.h"
#include "net.h"
#include "qemu-timer.h"
#include "virtio-net.h"

#define VIRTIO_NET_VM_VERSION    8

#define MAC_TABLE_ENTRIES    32
#define MAX_VLAN    (1 << 12)   /* Per 802.1Q definition */

typedef struct VirtIONet
{
    VirtIODevice vdev;
    uint8_t mac[ETH_ALEN];
    uint16_t status;
    VirtQueue *rx_vq;
    VirtQueue *tx_vq;
    VirtQueue *ctrl_vq;
    VLANClientState *vc;
    QEMUTimer *tx_timer;
    int tx_timer_active;
    int mergeable_rx_bufs;
    uint8_t promisc;
    uint8_t allmulti;
    struct {
        int in_use;
        uint8_t *macs;
    } mac_table;
    uint32_t *vlans;
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

    netcfg.status = n->status;
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
        qemu_format_nic_info_str(n->vc, n->mac);
    }
}

static void virtio_net_set_link_status(VLANClientState *vc)
{
    VirtIONet *n = vc->opaque;
    uint16_t old_status = n->status;

    if (vc->link_down)
        n->status &= ~VIRTIO_NET_S_LINK_UP;
    else
        n->status |= VIRTIO_NET_S_LINK_UP;

    if (n->status != old_status)
        virtio_notify_config(&n->vdev);
}

static void virtio_net_reset(VirtIODevice *vdev)
{
    VirtIONet *n = to_virtio_net(vdev);

    /* Reset back to compatibility mode */
    n->promisc = 1;
    n->allmulti = 0;

    /* Flush any MAC and VLAN filter table state */
    n->mac_table.in_use = 0;
    memset(n->mac_table.macs, 0, MAC_TABLE_ENTRIES * ETH_ALEN);
    memset(n->vlans, 0, MAX_VLAN >> 3);
}

static uint32_t virtio_net_get_features(VirtIODevice *vdev)
{
    uint32_t features = (1 << VIRTIO_NET_F_MAC) |
                        (1 << VIRTIO_NET_F_STATUS) |
                        (1 << VIRTIO_NET_F_CTRL_VQ) |
                        (1 << VIRTIO_NET_F_CTRL_RX) |
                        (1 << VIRTIO_NET_F_CTRL_VLAN);

    return features;
}

static uint32_t virtio_net_bad_features(VirtIODevice *vdev)
{
    uint32_t features = 0;

    /* Linux kernel 2.6.25.  It understood MAC (as everyone must),
     * but also these: */
    features |= (1 << VIRTIO_NET_F_MAC);
    features |= (1 << VIRTIO_NET_F_GUEST_CSUM);
    features |= (1 << VIRTIO_NET_F_GUEST_TSO4);
    features |= (1 << VIRTIO_NET_F_GUEST_TSO6);
    features |= (1 << VIRTIO_NET_F_GUEST_ECN);

    return features & virtio_net_get_features(vdev);
}

static void virtio_net_set_features(VirtIODevice *vdev, uint32_t features)
{
    VirtIONet *n = to_virtio_net(vdev);

    n->mergeable_rx_bufs = !!(features & (1 << VIRTIO_NET_F_MRG_RXBUF));
}

static int virtio_net_handle_rx_mode(VirtIONet *n, uint8_t cmd,
                                     VirtQueueElement *elem)
{
    uint8_t on;

    if (elem->out_num != 2 || elem->out_sg[1].iov_len != sizeof(on)) {
        fprintf(stderr, "virtio-net ctrl invalid rx mode command\n");
        exit(1);
    }

    on = ldub_p(elem->out_sg[1].iov_base);

    if (cmd == VIRTIO_NET_CTRL_RX_MODE_PROMISC)
        n->promisc = on;
    else if (cmd == VIRTIO_NET_CTRL_RX_MODE_ALLMULTI)
        n->allmulti = on;
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
    memset(n->mac_table.macs, 0, MAC_TABLE_ENTRIES * ETH_ALEN);

    mac_data.entries = ldl_le_p(elem->out_sg[1].iov_base);

    if (sizeof(mac_data.entries) +
        (mac_data.entries * ETH_ALEN) > elem->out_sg[1].iov_len)
        return VIRTIO_NET_ERR;

    if (mac_data.entries <= MAC_TABLE_ENTRIES) {
        memcpy(n->mac_table.macs, elem->out_sg[1].iov_base + sizeof(mac_data),
               mac_data.entries * ETH_ALEN);
        n->mac_table.in_use += mac_data.entries;
    } else {
        n->promisc = 1;
        return VIRTIO_NET_OK;
    }

    mac_data.entries = ldl_le_p(elem->out_sg[2].iov_base);

    if (sizeof(mac_data.entries) +
        (mac_data.entries * ETH_ALEN) > elem->out_sg[2].iov_len)
        return VIRTIO_NET_ERR;

    if (mac_data.entries) {
        if (n->mac_table.in_use + mac_data.entries <= MAC_TABLE_ENTRIES) {
            memcpy(n->mac_table.macs + (n->mac_table.in_use * ETH_ALEN),
                   elem->out_sg[2].iov_base + sizeof(mac_data),
                   mac_data.entries * ETH_ALEN);
            n->mac_table.in_use += mac_data.entries;
        } else
            n->allmulti = 1;
    }

    return VIRTIO_NET_OK;
}

static int virtio_net_handle_vlan_table(VirtIONet *n, uint8_t cmd,
                                        VirtQueueElement *elem)
{
    uint16_t vid;

    if (elem->out_num != 2 || elem->out_sg[1].iov_len != sizeof(vid)) {
        fprintf(stderr, "virtio-net ctrl invalid vlan command\n");
        return VIRTIO_NET_ERR;
    }

    vid = lduw_le_p(elem->out_sg[1].iov_base);

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
            fprintf(stderr, "virtio-net ctrl missing headers\n");
            exit(1);
        }

        if (elem.out_sg[0].iov_len < sizeof(ctrl) ||
            elem.in_sg[elem.in_num - 1].iov_len < sizeof(status)) {
            fprintf(stderr, "virtio-net ctrl header not in correct element\n");
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

    qemu_flush_queued_packets(n->vc);
}

static int do_virtio_net_can_receive(VirtIONet *n, int bufsize)
{
    if (!virtio_queue_ready(n->rx_vq) ||
        !(n->vdev.status & VIRTIO_CONFIG_S_DRIVER_OK))
        return 0;

    if (virtio_queue_empty(n->rx_vq) ||
        (n->mergeable_rx_bufs &&
         !virtqueue_avail_bytes(n->rx_vq, bufsize, 0))) {
        virtio_queue_set_notification(n->rx_vq, 1);
        return 0;
    }

    virtio_queue_set_notification(n->rx_vq, 0);
    return 1;
}

static int virtio_net_can_receive(VLANClientState *vc)
{
    VirtIONet *n = vc->opaque;

    return do_virtio_net_can_receive(n, VIRTIO_NET_MAX_BUFSIZE);
}

static int iov_fill(struct iovec *iov, int iovcnt, const void *buf, int count)
{
    int offset, i;

    offset = i = 0;
    while (offset < count && i < iovcnt) {
        int len = MIN(iov[i].iov_len, count - offset);
        memcpy(iov[i].iov_base, buf + offset, len);
        offset += len;
        i++;
    }

    return offset;
}

static int receive_header(VirtIONet *n, struct iovec *iov, int iovcnt,
                          const void *buf, size_t size, size_t hdr_len)
{
    struct virtio_net_hdr *hdr = (struct virtio_net_hdr *)iov[0].iov_base;
    int offset = 0;

    hdr->flags = 0;
    hdr->gso_type = VIRTIO_NET_HDR_GSO_NONE;

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

    if (!memcmp(&ptr[12], vlan, sizeof(vlan))) {
        int vid = be16_to_cpup((uint16_t *)(ptr + 14)) & 0xfff;
        if (!(n->vlans[vid >> 5] & (1U << (vid & 0x1f))))
            return 0;
    }

    if (ptr[0] & 1) { // multicast
        if (!memcmp(ptr, bcast, sizeof(bcast))) {
            return 1;
        } else if (n->allmulti) {
            return 1;
        }
    } else { // unicast
        if (!memcmp(ptr, n->mac, ETH_ALEN)) {
            return 1;
        }
    }

    for (i = 0; i < n->mac_table.in_use; i++) {
        if (!memcmp(ptr, &n->mac_table.macs[i * ETH_ALEN], ETH_ALEN))
            return 1;
    }

    return 0;
}

static ssize_t virtio_net_receive(VLANClientState *vc, const uint8_t *buf, size_t size)
{
    VirtIONet *n = vc->opaque;
    struct virtio_net_hdr_mrg_rxbuf *mhdr = NULL;
    size_t hdr_len, offset, i;

    if (!do_virtio_net_can_receive(n, size))
        return 0;

    if (!receive_filter(n, buf, size))
        return size;

    /* hdr_len refers to the header we supply to the guest */
    hdr_len = n->mergeable_rx_bufs ?
        sizeof(struct virtio_net_hdr_mrg_rxbuf) : sizeof(struct virtio_net_hdr);

    offset = i = 0;

    while (offset < size) {
        VirtQueueElement elem;
        int len, total;
        struct iovec sg[VIRTQUEUE_MAX_SIZE];

        len = total = 0;

        if ((i != 0 && !n->mergeable_rx_bufs) ||
            virtqueue_pop(n->rx_vq, &elem) == 0) {
            if (i == 0)
                return -1;
            fprintf(stderr, "virtio-net truncating packet\n");
            exit(1);
        }

        if (elem.in_num < 1) {
            fprintf(stderr, "virtio-net receive queue contains no in buffers\n");
            exit(1);
        }

        if (!n->mergeable_rx_bufs && elem.in_sg[0].iov_len != hdr_len) {
            fprintf(stderr, "virtio-net header not in first element\n");
            exit(1);
        }

        memcpy(&sg, &elem.in_sg[0], sizeof(sg[0]) * elem.in_num);

        if (i == 0) {
            if (n->mergeable_rx_bufs)
                mhdr = (struct virtio_net_hdr_mrg_rxbuf *)sg[0].iov_base;

            offset += receive_header(n, sg, elem.in_num,
                                     buf + offset, size - offset, hdr_len);
            total += hdr_len;
        }

        /* copy in packet.  ugh */
        len = iov_fill(sg, elem.in_num,
                       buf + offset, size - offset);
        total += len;

        /* signal other side */
        virtqueue_fill(n->rx_vq, &elem, total, i++);

        offset += len;
    }

    if (mhdr)
        mhdr->num_buffers = i;

    virtqueue_flush(n->rx_vq, i);
    virtio_notify(&n->vdev, n->rx_vq);

    return size;
}

/* TX */
static void virtio_net_flush_tx(VirtIONet *n, VirtQueue *vq)
{
    VirtQueueElement elem;
    int has_vnet_hdr = 0;

    if (!(n->vdev.status & VIRTIO_CONFIG_S_DRIVER_OK))
        return;

    while (virtqueue_pop(vq, &elem)) {
        ssize_t len = 0;
        unsigned int out_num = elem.out_num;
        struct iovec *out_sg = &elem.out_sg[0];
        unsigned hdr_len;

        /* hdr_len refers to the header received from the guest */
        hdr_len = n->mergeable_rx_bufs ?
            sizeof(struct virtio_net_hdr_mrg_rxbuf) :
            sizeof(struct virtio_net_hdr);

        if (out_num < 1 || out_sg->iov_len != hdr_len) {
            fprintf(stderr, "virtio-net header not in first element\n");
            exit(1);
        }

        /* ignore the header if GSO is not supported */
        if (!has_vnet_hdr) {
            out_num--;
            out_sg++;
            len += hdr_len;
        } else if (n->mergeable_rx_bufs) {
            /* tapfd expects a struct virtio_net_hdr */
            hdr_len -= sizeof(struct virtio_net_hdr);
            out_sg->iov_len -= hdr_len;
            len += hdr_len;
        }

        len += qemu_sendv_packet(n->vc, out_sg, out_num);

        virtqueue_push(vq, &elem, len);
        virtio_notify(&n->vdev, vq);
    }
}

static void virtio_net_handle_tx(VirtIODevice *vdev, VirtQueue *vq)
{
    VirtIONet *n = to_virtio_net(vdev);

    if (n->tx_timer_active) {
        virtio_queue_set_notification(vq, 1);
        qemu_del_timer(n->tx_timer);
        n->tx_timer_active = 0;
        virtio_net_flush_tx(n, vq);
    } else {
        qemu_mod_timer(n->tx_timer,
                       qemu_get_clock(vm_clock) + TX_TIMER_INTERVAL);
        n->tx_timer_active = 1;
        virtio_queue_set_notification(vq, 0);
    }
}

static void virtio_net_tx_timer(void *opaque)
{
    VirtIONet *n = opaque;

    n->tx_timer_active = 0;

    /* Just in case the driver is not ready on more */
    if (!(n->vdev.status & VIRTIO_CONFIG_S_DRIVER_OK))
        return;

    virtio_queue_set_notification(n->tx_vq, 1);
    virtio_net_flush_tx(n, n->tx_vq);
}

static void virtio_net_save(QEMUFile *f, void *opaque)
{
    VirtIONet *n = opaque;

    virtio_save(&n->vdev, f);

    qemu_put_buffer(f, n->mac, ETH_ALEN);
    qemu_put_be32(f, n->tx_timer_active);
    qemu_put_be32(f, n->mergeable_rx_bufs);
    qemu_put_be16(f, n->status);
    qemu_put_byte(f, n->promisc);
    qemu_put_byte(f, n->allmulti);
    qemu_put_be32(f, n->mac_table.in_use);
    qemu_put_buffer(f, n->mac_table.macs, n->mac_table.in_use * ETH_ALEN);
    qemu_put_buffer(f, (uint8_t *)n->vlans, MAX_VLAN >> 3);
    qemu_put_be32(f, 0); /* vnet-hdr placeholder */
}

static int virtio_net_load(QEMUFile *f, void *opaque, int version_id)
{
    VirtIONet *n = opaque;

    if (version_id < 2 || version_id > VIRTIO_NET_VM_VERSION)
        return -EINVAL;

    virtio_load(&n->vdev, f);

    qemu_get_buffer(f, n->mac, ETH_ALEN);
    n->tx_timer_active = qemu_get_be32(f);
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
            n->promisc = 1;
            n->mac_table.in_use = 0;
        }
    }
 
    if (version_id >= 6)
        qemu_get_buffer(f, (uint8_t *)n->vlans, MAX_VLAN >> 3);

    if (version_id >= 7 && qemu_get_be32(f)) {
        fprintf(stderr,
                "virtio-net: saved image requires vnet header support\n");
        exit(1);
    }

    if (n->tx_timer_active) {
        qemu_mod_timer(n->tx_timer,
                       qemu_get_clock(vm_clock) + TX_TIMER_INTERVAL);
    }

    return 0;
}

static void virtio_net_cleanup(VLANClientState *vc)
{
    VirtIONet *n = vc->opaque;

    unregister_savevm("virtio-net", n);

    qemu_free(n->mac_table.macs);
    qemu_free(n->vlans);

    qemu_del_timer(n->tx_timer);
    qemu_free_timer(n->tx_timer);

    virtio_cleanup(&n->vdev);
}

VirtIODevice *virtio_net_init(DeviceState *dev)
{
    VirtIONet *n;
    static int virtio_net_id;

    n = (VirtIONet *)virtio_common_init("virtio-net", VIRTIO_ID_NET,
                                        sizeof(struct virtio_net_config),
                                        sizeof(VirtIONet));

    n->vdev.get_config = virtio_net_get_config;
    n->vdev.set_config = virtio_net_set_config;
    n->vdev.get_features = virtio_net_get_features;
    n->vdev.set_features = virtio_net_set_features;
    n->vdev.bad_features = virtio_net_bad_features;
    n->vdev.reset = virtio_net_reset;
    n->rx_vq = virtio_add_queue(&n->vdev, 256, virtio_net_handle_rx);
    n->tx_vq = virtio_add_queue(&n->vdev, 256, virtio_net_handle_tx);
    n->ctrl_vq = virtio_add_queue(&n->vdev, 16, virtio_net_handle_ctrl);
    qdev_get_macaddr(dev, n->mac);
    n->status = VIRTIO_NET_S_LINK_UP;
    n->vc = qdev_get_vlan_client(dev,
                                 virtio_net_can_receive,
                                 virtio_net_receive, NULL,
                                 virtio_net_cleanup, n);
    n->vc->link_status_changed = virtio_net_set_link_status;

    qemu_format_nic_info_str(n->vc, n->mac);

    n->tx_timer = qemu_new_timer(vm_clock, virtio_net_tx_timer, n);
    n->tx_timer_active = 0;
    n->mergeable_rx_bufs = 0;
    n->promisc = 1; /* for compatibility */

    n->mac_table.macs = qemu_mallocz(MAC_TABLE_ENTRIES * ETH_ALEN);

    n->vlans = qemu_mallocz(MAX_VLAN >> 3);

    register_savevm("virtio-net", virtio_net_id++, VIRTIO_NET_VM_VERSION,
                    virtio_net_save, virtio_net_load, n);

    return &n->vdev;
}
