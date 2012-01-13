/*
 * vhost-net support
 *
 * Copyright Red Hat, Inc. 2010
 *
 * Authors:
 *  Michael S. Tsirkin <mst@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "net.h"
#include "net/tap.h"

#include "virtio-net.h"
#include "vhost_net.h"
#include "qemu-error.h"

#include "config.h"

#ifdef CONFIG_VHOST_NET
#include <linux/vhost.h>
#include <sys/socket.h>
#include <linux/kvm.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/virtio_ring.h>
#include <netpacket/packet.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <netinet/in.h>

#include <stdio.h>

#include "vhost.h"

struct vhost_net {
    struct vhost_dev dev;
    struct vhost_virtqueue vqs[2];
    int backend;
    VLANClientState *vc;
};

unsigned vhost_net_get_features(struct vhost_net *net, unsigned features)
{
    /* Clear features not supported by host kernel. */
    if (!(net->dev.features & (1 << VIRTIO_F_NOTIFY_ON_EMPTY))) {
        features &= ~(1 << VIRTIO_F_NOTIFY_ON_EMPTY);
    }
    if (!(net->dev.features & (1 << VIRTIO_RING_F_INDIRECT_DESC))) {
        features &= ~(1 << VIRTIO_RING_F_INDIRECT_DESC);
    }
    if (!(net->dev.features & (1 << VIRTIO_RING_F_EVENT_IDX))) {
        features &= ~(1 << VIRTIO_RING_F_EVENT_IDX);
    }
    if (!(net->dev.features & (1 << VIRTIO_NET_F_MRG_RXBUF))) {
        features &= ~(1 << VIRTIO_NET_F_MRG_RXBUF);
    }
    return features;
}

void vhost_net_ack_features(struct vhost_net *net, unsigned features)
{
    net->dev.acked_features = net->dev.backend_features;
    if (features & (1 << VIRTIO_F_NOTIFY_ON_EMPTY)) {
        net->dev.acked_features |= (1 << VIRTIO_F_NOTIFY_ON_EMPTY);
    }
    if (features & (1 << VIRTIO_RING_F_INDIRECT_DESC)) {
        net->dev.acked_features |= (1 << VIRTIO_RING_F_INDIRECT_DESC);
    }
    if (features & (1 << VIRTIO_RING_F_EVENT_IDX)) {
        net->dev.acked_features |= (1 << VIRTIO_RING_F_EVENT_IDX);
    }
    if (features & (1 << VIRTIO_NET_F_MRG_RXBUF)) {
        net->dev.acked_features |= (1 << VIRTIO_NET_F_MRG_RXBUF);
    }
}

static int vhost_net_get_fd(VLANClientState *backend)
{
    switch (backend->info->type) {
    case NET_CLIENT_TYPE_TAP:
        return tap_get_fd(backend);
    default:
        fprintf(stderr, "vhost-net requires tap backend\n");
        return -EBADFD;
    }
}

struct vhost_net *vhost_net_init(VLANClientState *backend, int devfd,
                                 bool force)
{
    int r;
    struct vhost_net *net = g_malloc(sizeof *net);
    if (!backend) {
        fprintf(stderr, "vhost-net requires backend to be setup\n");
        goto fail;
    }
    r = vhost_net_get_fd(backend);
    if (r < 0) {
        goto fail;
    }
    net->vc = backend;
    net->dev.backend_features = tap_has_vnet_hdr(backend) ? 0 :
        (1 << VHOST_NET_F_VIRTIO_NET_HDR);
    net->backend = r;

    r = vhost_dev_init(&net->dev, devfd, force);
    if (r < 0) {
        goto fail;
    }
    if (!tap_has_vnet_hdr_len(backend,
                              sizeof(struct virtio_net_hdr_mrg_rxbuf))) {
        net->dev.features &= ~(1 << VIRTIO_NET_F_MRG_RXBUF);
    }
    if (~net->dev.features & net->dev.backend_features) {
        fprintf(stderr, "vhost lacks feature mask %" PRIu64 " for backend\n",
                (uint64_t)(~net->dev.features & net->dev.backend_features));
        vhost_dev_cleanup(&net->dev);
        goto fail;
    }

    /* Set sane init value. Override when guest acks. */
    vhost_net_ack_features(net, 0);
    return net;
fail:
    g_free(net);
    return NULL;
}

bool vhost_net_query(VHostNetState *net, VirtIODevice *dev)
{
    return vhost_dev_query(&net->dev, dev);
}

int vhost_net_start(struct vhost_net *net,
                    VirtIODevice *dev)
{
    struct vhost_vring_file file = { };
    int r;

    net->dev.nvqs = 2;
    net->dev.vqs = net->vqs;

    r = vhost_dev_enable_notifiers(&net->dev, dev);
    if (r < 0) {
        goto fail_notifiers;
    }
    if (net->dev.acked_features & (1 << VIRTIO_NET_F_MRG_RXBUF)) {
        tap_set_vnet_hdr_len(net->vc,
                             sizeof(struct virtio_net_hdr_mrg_rxbuf));
    }

    r = vhost_dev_start(&net->dev, dev);
    if (r < 0) {
        goto fail_start;
    }

    net->vc->info->poll(net->vc, false);
    qemu_set_fd_handler(net->backend, NULL, NULL, NULL);
    file.fd = net->backend;
    for (file.index = 0; file.index < net->dev.nvqs; ++file.index) {
        r = ioctl(net->dev.control, VHOST_NET_SET_BACKEND, &file);
        if (r < 0) {
            r = -errno;
            goto fail;
        }
    }
    return 0;
fail:
    file.fd = -1;
    while (file.index-- > 0) {
        int r = ioctl(net->dev.control, VHOST_NET_SET_BACKEND, &file);
        assert(r >= 0);
    }
    net->vc->info->poll(net->vc, true);
    vhost_dev_stop(&net->dev, dev);
    if (net->dev.acked_features & (1 << VIRTIO_NET_F_MRG_RXBUF)) {
        tap_set_vnet_hdr_len(net->vc, sizeof(struct virtio_net_hdr));
    }
fail_start:
    vhost_dev_disable_notifiers(&net->dev, dev);
fail_notifiers:
    return r;
}

void vhost_net_stop(struct vhost_net *net,
                    VirtIODevice *dev)
{
    struct vhost_vring_file file = { .fd = -1 };

    for (file.index = 0; file.index < net->dev.nvqs; ++file.index) {
        int r = ioctl(net->dev.control, VHOST_NET_SET_BACKEND, &file);
        assert(r >= 0);
    }
    net->vc->info->poll(net->vc, true);
    vhost_dev_stop(&net->dev, dev);
    if (net->dev.acked_features & (1 << VIRTIO_NET_F_MRG_RXBUF)) {
        tap_set_vnet_hdr_len(net->vc, sizeof(struct virtio_net_hdr));
    }
    vhost_dev_disable_notifiers(&net->dev, dev);
}

void vhost_net_cleanup(struct vhost_net *net)
{
    vhost_dev_cleanup(&net->dev);
    if (net->dev.acked_features & (1 << VIRTIO_NET_F_MRG_RXBUF)) {
        tap_set_vnet_hdr_len(net->vc, sizeof(struct virtio_net_hdr));
    }
    g_free(net);
}
#else
struct vhost_net *vhost_net_init(VLANClientState *backend, int devfd,
                                 bool force)
{
    error_report("vhost-net support is not compiled in");
    return NULL;
}

bool vhost_net_query(VHostNetState *net, VirtIODevice *dev)
{
    return false;
}

int vhost_net_start(struct vhost_net *net,
		    VirtIODevice *dev)
{
    return -ENOSYS;
}
void vhost_net_stop(struct vhost_net *net,
		    VirtIODevice *dev)
{
}

void vhost_net_cleanup(struct vhost_net *net)
{
}

unsigned vhost_net_get_features(struct vhost_net *net, unsigned features)
{
    return features;
}
void vhost_net_ack_features(struct vhost_net *net, unsigned features)
{
}
#endif
