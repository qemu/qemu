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

#include "qemu/osdep.h"
#include "net/net.h"
#include "net/tap.h"
#include "net/vhost-vdpa.h"

#include "standard-headers/linux/vhost_types.h"
#include "hw/virtio/virtio-net.h"
#include "net/vhost_net.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"

#include <sys/socket.h>
#include <net/if.h>
#include <netinet/in.h>


#include "standard-headers/linux/virtio_ring.h"
#include "hw/virtio/vhost.h"
#include "hw/virtio/virtio-bus.h"
#include "linux-headers/linux/vhost.h"

void vhost_net_get_features_ex(struct vhost_net *net, uint64_t *features)
{
    vhost_get_features_ex(&net->dev, net->feature_bits, features);
}
int vhost_net_get_config(struct vhost_net *net,  uint8_t *config,
                         uint32_t config_len)
{
    return vhost_dev_get_config(&net->dev, config, config_len, NULL);
}
int vhost_net_set_config(struct vhost_net *net, const uint8_t *data,
                         uint32_t offset, uint32_t size, uint32_t flags)
{
    return vhost_dev_set_config(&net->dev, data, offset, size, flags);
}

void vhost_net_ack_features_ex(struct vhost_net *net, const uint64_t *features)
{
    virtio_features_copy(net->dev.acked_features_ex,
                         net->dev.backend_features_ex);
    vhost_ack_features_ex(&net->dev, net->feature_bits, features);
}

uint64_t vhost_net_get_max_queues(VHostNetState *net)
{
    return net->dev.max_queues;
}

void vhost_net_get_acked_features_ex(VHostNetState *net, uint64_t *features)
{
    virtio_features_copy(features, net->dev.acked_features_ex);
}

void vhost_net_save_acked_features(NetClientState *nc)
{
    struct vhost_net *net = get_vhost_net(nc);

    if (net && net->save_acked_features) {
        net->save_acked_features(nc);
    }
}

static void vhost_net_disable_notifiers_nvhosts(VirtIODevice *dev,
                NetClientState *ncs, int data_queue_pairs, int nvhosts)
{
    VirtIONet *n = VIRTIO_NET(dev);
    BusState *qbus = BUS(qdev_get_parent_bus(DEVICE(dev)));
    struct vhost_net *net;
    struct vhost_dev *hdev;
    int r, i, j;
    NetClientState *peer;

    /*
     * Batch all the host notifiers in a single transaction to avoid
     * quadratic time complexity in address_space_update_ioeventfds().
     */
    memory_region_transaction_begin();

    for (i = 0; i < nvhosts; i++) {
        if (i < data_queue_pairs) {
            peer = qemu_get_peer(ncs, i);
        } else {
            peer = qemu_get_peer(ncs, n->max_queue_pairs);
        }

        net = get_vhost_net(peer);
        hdev = &net->dev;
        for (j = 0; j < hdev->nvqs; j++) {
            r = virtio_bus_set_host_notifier(VIRTIO_BUS(qbus),
                                             hdev->vq_index + j,
                                             false);
            if (r < 0) {
                error_report("vhost %d VQ %d notifier cleanup failed: %d",
                              i, j, -r);
            }
            assert(r >= 0);
        }
    }
    /*
     * The transaction expects the ioeventfds to be open when it
     * commits. Do it now, before the cleanup loop.
     */
    memory_region_transaction_commit();

    for (i = 0; i < nvhosts; i++) {
        if (i < data_queue_pairs) {
            peer = qemu_get_peer(ncs, i);
        } else {
            peer = qemu_get_peer(ncs, n->max_queue_pairs);
        }

        net = get_vhost_net(peer);
        hdev = &net->dev;
        for (j = 0; j < hdev->nvqs; j++) {
            virtio_bus_cleanup_host_notifier(VIRTIO_BUS(qbus),
                                             hdev->vq_index + j);
        }
        virtio_device_release_ioeventfd(dev);
    }
}

static int vhost_net_enable_notifiers(VirtIODevice *dev,
                NetClientState *ncs, int data_queue_pairs, int cvq)
{
    VirtIONet *n = VIRTIO_NET(dev);
    BusState *qbus = BUS(qdev_get_parent_bus(DEVICE(dev)));
    int nvhosts = data_queue_pairs + cvq;
    struct vhost_net *net;
    struct vhost_dev *hdev;
    int r, i, j, k;
    NetClientState *peer;

    /*
     * We will pass the notifiers to the kernel, make sure that QEMU
     * doesn't interfere.
     */
    for (i = 0; i < nvhosts; i++) {
        r = virtio_device_grab_ioeventfd(dev);
        if (r < 0) {
            error_report("vhost %d binding does not support host notifiers", i);
            for (k = 0; k < i; k++) {
                virtio_device_release_ioeventfd(dev);
            }
            return r;
        }
    }

    /*
     * Batch all the host notifiers in a single transaction to avoid
     * quadratic time complexity in address_space_update_ioeventfds().
     */
    memory_region_transaction_begin();

    for (i = 0; i < nvhosts; i++) {
        if (i < data_queue_pairs) {
            peer = qemu_get_peer(ncs, i);
        } else {
            peer = qemu_get_peer(ncs, n->max_queue_pairs);
        }

        net = get_vhost_net(peer);
        hdev = &net->dev;

        for (j = 0; j < hdev->nvqs; j++) {
            r = virtio_bus_set_host_notifier(VIRTIO_BUS(qbus),
                                             hdev->vq_index + j,
                                             true);
            if (r < 0) {
                error_report("vhost %d VQ %d notifier binding failed: %d",
                              i, j, -r);
                memory_region_transaction_commit();
                vhost_dev_disable_notifiers_nvqs(hdev, dev, j);
                goto fail_nvhosts;
            }
        }
    }

    memory_region_transaction_commit();

    return 0;
fail_nvhosts:
    vhost_net_disable_notifiers_nvhosts(dev, ncs, data_queue_pairs, i);
    /*
     * This for loop starts from i+1, not i, because the i-th ioeventfd
     * has already been released in vhost_dev_disable_notifiers_nvqs().
     */
    for (k = i + 1; k < nvhosts; k++) {
        virtio_device_release_ioeventfd(dev);
    }

    return r;
}

/*
 * Stop processing guest IO notifications in qemu.
 * Start processing them in vhost in kernel.
 */
static void vhost_net_disable_notifiers(VirtIODevice *dev,
                NetClientState *ncs, int data_queue_pairs, int cvq)
{
    vhost_net_disable_notifiers_nvhosts(dev, ncs, data_queue_pairs,
                                        data_queue_pairs + cvq);
}

static int vhost_net_get_fd(NetClientState *backend)
{
    switch (backend->info->type) {
    case NET_CLIENT_DRIVER_TAP:
        return tap_get_fd(backend);
    default:
        fprintf(stderr, "vhost-net requires tap backend\n");
        return -ENOSYS;
    }
}

struct vhost_net *vhost_net_init(VhostNetOptions *options)
{
    int r;
    bool backend_kernel = options->backend_type == VHOST_BACKEND_TYPE_KERNEL;
    struct vhost_net *net = g_new0(struct vhost_net, 1);
    uint64_t missing_features[VIRTIO_FEATURES_NU64S];
    uint64_t features[VIRTIO_FEATURES_NU64S];
    Error *local_err = NULL;

    if (!options->net_backend) {
        fprintf(stderr, "vhost-net requires net backend to be setup\n");
        goto fail;
    }
    net->nc = options->net_backend;
    net->dev.nvqs = options->nvqs;
    net->feature_bits = options->feature_bits;
    net->save_acked_features = options->save_acked_features;
    net->max_tx_queue_size = options->max_tx_queue_size;
    net->is_vhost_user = options->is_vhost_user;
    virtio_features_clear(features);

    net->dev.max_queues = 1;
    net->dev.vqs = net->vqs;

    if (backend_kernel) {
        r = vhost_net_get_fd(options->net_backend);
        if (r < 0) {
            goto fail;
        }
        net->dev.backend_features = qemu_has_vnet_hdr(options->net_backend)
            ? 0 : (1ULL << VHOST_NET_F_VIRTIO_NET_HDR);
        net->backend = r;
        net->dev.protocol_features = 0;
    } else {
        virtio_features_clear(net->dev.backend_features_ex);
        net->dev.protocol_features = 0;
        net->backend = -1;

        /* vhost-user needs vq_index to initiate a specific queue pair */
        net->dev.vq_index = net->nc->queue_index * net->dev.nvqs;
    }

    r = vhost_dev_init(&net->dev, options->opaque,
                       options->backend_type, options->busyloop_timeout,
                       &local_err);
    if (r < 0) {
        error_report_err(local_err);
        goto fail;
    }
    if (backend_kernel) {
        if (!qemu_has_vnet_hdr_len(options->net_backend,
                               sizeof(struct virtio_net_hdr_mrg_rxbuf))) {
            net->dev.features &= ~(1ULL << VIRTIO_NET_F_MRG_RXBUF);
        }

        if (virtio_features_andnot(missing_features,
                                   net->dev.backend_features_ex,
                                   net->dev.features_ex)) {
            fprintf(stderr, "vhost lacks feature mask 0x" VIRTIO_FEATURES_FMT
                   " for backend\n", VIRTIO_FEATURES_PR(missing_features));
            goto fail;
        }
    }

    /* Set sane init value. Override when guest acks. */
    if (options->get_acked_features) {
        virtio_features_from_u64(features,
                                 options->get_acked_features(net->nc));
        if (virtio_features_andnot(missing_features, features,
                                   net->dev.features_ex)) {
            fprintf(stderr, "vhost lacks feature mask 0x" VIRTIO_FEATURES_FMT
                    " for backend\n", VIRTIO_FEATURES_PR(missing_features));
            goto fail;
        }
    }

    vhost_net_ack_features_ex(net, features);

    return net;

fail:
    vhost_dev_cleanup(&net->dev);
    g_free(net);
    return NULL;
}

static void vhost_net_set_vq_index(struct vhost_net *net, int vq_index,
                                   int vq_index_end)
{
    net->dev.vq_index = vq_index;
    net->dev.vq_index_end = vq_index_end;
}

static int vhost_net_start_one(struct vhost_net *net,
                               VirtIODevice *dev)
{
    struct vhost_vring_file file = { };
    int r;

    if (net->nc->info->start) {
        r = net->nc->info->start(net->nc);
        if (r < 0) {
            return r;
        }
    }

    r = vhost_dev_start(&net->dev, dev, false);
    if (r < 0) {
        goto fail_start;
    }

    if (net->nc->info->poll) {
        net->nc->info->poll(net->nc, false);
    }

    if (net->nc->info->type == NET_CLIENT_DRIVER_TAP) {
        qemu_set_fd_handler(net->backend, NULL, NULL, NULL);
        file.fd = net->backend;
        for (file.index = 0; file.index < net->dev.nvqs; ++file.index) {
            if (!virtio_queue_enabled(dev, net->dev.vq_index +
                                      file.index)) {
                /* Queue might not be ready for start */
                continue;
            }
            r = vhost_net_set_backend(&net->dev, &file);
            if (r < 0) {
                r = -errno;
                goto fail;
            }
        }
    }

    if (net->nc->info->load) {
        r = net->nc->info->load(net->nc);
        if (r < 0) {
            goto fail;
        }
    }
    return 0;
fail:
    file.fd = -1;
    if (net->nc->info->type == NET_CLIENT_DRIVER_TAP) {
        while (file.index-- > 0) {
            if (!virtio_queue_enabled(dev, net->dev.vq_index +
                                      file.index)) {
                /* Queue might not be ready for start */
                continue;
            }
            int ret = vhost_net_set_backend(&net->dev, &file);
            assert(ret >= 0);
        }
    }
    if (net->nc->info->poll) {
        net->nc->info->poll(net->nc, true);
    }
    vhost_dev_stop(&net->dev, dev, false);
fail_start:
    return r;
}

static void vhost_net_stop_one(struct vhost_net *net,
                               VirtIODevice *dev)
{
    struct vhost_vring_file file = { .fd = -1 };

    if (net->nc->info->type == NET_CLIENT_DRIVER_TAP) {
        for (file.index = 0; file.index < net->dev.nvqs; ++file.index) {
            int r = vhost_net_set_backend(&net->dev, &file);
            assert(r >= 0);
        }
    }
    if (net->nc->info->poll) {
        net->nc->info->poll(net->nc, true);
    }
    vhost_dev_stop(&net->dev, dev, false);
    if (net->nc->info->stop) {
        net->nc->info->stop(net->nc);
    }
}

int vhost_net_start(VirtIODevice *dev, NetClientState *ncs,
                    int data_queue_pairs, int cvq)
{
    BusState *qbus = BUS(qdev_get_parent_bus(DEVICE(dev)));
    VirtioBusState *vbus = VIRTIO_BUS(qbus);
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(vbus);
    int total_notifiers = data_queue_pairs * 2 + cvq;
    VirtIONet *n = VIRTIO_NET(dev);
    int nvhosts = data_queue_pairs + cvq;
    struct vhost_net *net;
    int r, e, i, index_end = data_queue_pairs * 2;
    NetClientState *peer;

    if (cvq) {
        index_end += 1;
    }

    if (!k->set_guest_notifiers) {
        error_report("binding does not support guest notifiers");
        return -ENOSYS;
    }

    for (i = 0; i < nvhosts; i++) {

        if (i < data_queue_pairs) {
            peer = qemu_get_peer(ncs, i);
        } else { /* Control Virtqueue */
            peer = qemu_get_peer(ncs, n->max_queue_pairs);
        }

        net = get_vhost_net(peer);
        vhost_net_set_vq_index(net, i * 2, index_end);

        /* Suppress the masking guest notifiers on vhost user
         * because vhost user doesn't interrupt masking/unmasking
         * properly.
         */
        if (net->is_vhost_user) {
            dev->use_guest_notifier_mask = false;
        }
     }

    r = vhost_net_enable_notifiers(dev, ncs, data_queue_pairs, cvq);
    if (r < 0) {
        error_report("Error enabling host notifiers: %d", -r);
        goto err;
    }

    r = k->set_guest_notifiers(qbus->parent, total_notifiers, true);
    if (r < 0) {
        error_report("Error binding guest notifier: %d", -r);
        goto err_host_notifiers;
    }

    for (i = 0; i < nvhosts; i++) {
        if (i < data_queue_pairs) {
            peer = qemu_get_peer(ncs, i);
        } else {
            peer = qemu_get_peer(ncs, n->max_queue_pairs);
        }

        if (peer->vring_enable) {
            /* restore vring enable state */
            r = vhost_net_set_vring_enable(peer, peer->vring_enable);

            if (r < 0) {
                goto err_guest_notifiers;
            }
        }

        r = vhost_net_start_one(get_vhost_net(peer), dev);
        if (r < 0) {
            goto err_guest_notifiers;
        }
    }

    return 0;

err_guest_notifiers:
    while (--i >= 0) {
        peer = qemu_get_peer(ncs, i < data_queue_pairs ?
                                  i : n->max_queue_pairs);
        vhost_net_stop_one(get_vhost_net(peer), dev);
    }
    e = k->set_guest_notifiers(qbus->parent, total_notifiers, false);
    if (e < 0) {
        fprintf(stderr, "vhost guest notifier cleanup failed: %d\n", e);
        fflush(stderr);
    }
err_host_notifiers:
    vhost_net_disable_notifiers(dev, ncs, data_queue_pairs, cvq);
err:
    return r;
}

void vhost_net_stop(VirtIODevice *dev, NetClientState *ncs,
                    int data_queue_pairs, int cvq)
{
    BusState *qbus = BUS(qdev_get_parent_bus(DEVICE(dev)));
    VirtioBusState *vbus = VIRTIO_BUS(qbus);
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(vbus);
    VirtIONet *n = VIRTIO_NET(dev);
    NetClientState *peer;
    int total_notifiers = data_queue_pairs * 2 + cvq;
    int nvhosts = data_queue_pairs + cvq;
    int i, r;

    for (i = 0; i < nvhosts; i++) {
        if (i < data_queue_pairs) {
            peer = qemu_get_peer(ncs, i);
        } else {
            peer = qemu_get_peer(ncs, n->max_queue_pairs);
        }
        vhost_net_stop_one(get_vhost_net(peer), dev);
    }

    r = k->set_guest_notifiers(qbus->parent, total_notifiers, false);
    if (r < 0) {
        fprintf(stderr, "vhost guest notifier cleanup failed: %d\n", r);
        fflush(stderr);
    }
    assert(r >= 0);

    vhost_net_disable_notifiers(dev, ncs, data_queue_pairs, cvq);
}

void vhost_net_cleanup(struct vhost_net *net)
{
    vhost_dev_cleanup(&net->dev);
}

int vhost_net_notify_migration_done(struct vhost_net *net, char* mac_addr)
{
    const VhostOps *vhost_ops = net->dev.vhost_ops;

    assert(vhost_ops->backend_type == VHOST_BACKEND_TYPE_USER);
    assert(vhost_ops->vhost_migration_done);

    return vhost_ops->vhost_migration_done(&net->dev, mac_addr);
}

bool vhost_net_virtqueue_pending(VHostNetState *net, int idx)
{
    return vhost_virtqueue_pending(&net->dev, idx);
}

void vhost_net_virtqueue_mask(VHostNetState *net, VirtIODevice *dev,
                              int idx, bool mask)
{
    vhost_virtqueue_mask(&net->dev, dev, idx, mask);
}

bool vhost_net_config_pending(VHostNetState *net)
{
    return vhost_config_pending(&net->dev);
}

void vhost_net_config_mask(VHostNetState *net, VirtIODevice *dev, bool mask)
{
    vhost_config_mask(&net->dev, dev, mask);
}

VHostNetState *get_vhost_net(NetClientState *nc)
{
    if (!nc) {
        return 0;
    }

    if (nc->info->get_vhost_net) {
        return nc->info->get_vhost_net(nc);
    }

    return NULL;
}

int vhost_net_set_vring_enable(NetClientState *nc, int enable)
{
    VHostNetState *net = get_vhost_net(nc);
    const VhostOps *vhost_ops = net->dev.vhost_ops;

    /*
     * vhost-vdpa network devices need to enable dataplane virtqueues after
     * DRIVER_OK, so they can recover device state before starting dataplane.
     * Because of that, we don't enable virtqueues here and leave it to
     * net/vhost-vdpa.c.
     */
    if (nc->info->type == NET_CLIENT_DRIVER_VHOST_VDPA) {
        return 0;
    }

    nc->vring_enable = enable;

    if (vhost_ops && vhost_ops->vhost_set_vring_enable) {
        return vhost_ops->vhost_set_vring_enable(&net->dev, enable);
    }

    return 0;
}

int vhost_net_set_mtu(struct vhost_net *net, uint16_t mtu)
{
    const VhostOps *vhost_ops = net->dev.vhost_ops;

    if (!vhost_ops->vhost_net_set_mtu) {
        return 0;
    }

    return vhost_ops->vhost_net_set_mtu(&net->dev, mtu);
}

void vhost_net_virtqueue_reset(VirtIODevice *vdev, NetClientState *nc,
                               int vq_index)
{
    VHostNetState *net = get_vhost_net(nc->peer);
    const VhostOps *vhost_ops = net->dev.vhost_ops;
    struct vhost_vring_file file = { .fd = -1 };
    int idx;

    /* should only be called after backend is connected */
    assert(vhost_ops);

    idx = vhost_ops->vhost_get_vq_index(&net->dev, vq_index);

    if (net->nc->info->type == NET_CLIENT_DRIVER_TAP) {
        file.index = idx;
        int r = vhost_net_set_backend(&net->dev, &file);
        assert(r >= 0);
    }

    vhost_virtqueue_stop(&net->dev,
                         vdev,
                         net->dev.vqs + idx,
                         net->dev.vq_index + idx);
}

int vhost_net_virtqueue_restart(VirtIODevice *vdev, NetClientState *nc,
                                int vq_index)
{
    VHostNetState *net = get_vhost_net(nc->peer);
    const VhostOps *vhost_ops = net->dev.vhost_ops;
    struct vhost_vring_file file = { };
    int idx, r;

    if (!net->dev.started) {
        return -EBUSY;
    }

    /* should only be called after backend is connected */
    assert(vhost_ops);

    idx = vhost_ops->vhost_get_vq_index(&net->dev, vq_index);

    r = vhost_virtqueue_start(&net->dev,
                              vdev,
                              net->dev.vqs + idx,
                              net->dev.vq_index + idx);
    if (r < 0) {
        goto err_start;
    }

    if (net->nc->info->type == NET_CLIENT_DRIVER_TAP) {
        file.index = idx;
        file.fd = net->backend;
        r = vhost_net_set_backend(&net->dev, &file);
        if (r < 0) {
            r = -errno;
            goto err_start;
        }
    }

    return 0;

err_start:
    error_report("Error when restarting the queue.");

    if (net->nc->info->type == NET_CLIENT_DRIVER_TAP) {
        file.fd = VHOST_FILE_UNBIND;
        file.index = idx;
        int ret = vhost_net_set_backend(&net->dev, &file);
        assert(ret >= 0);
    }

    vhost_dev_stop(&net->dev, vdev, false);

    return r;
}
