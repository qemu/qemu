/*
 * vhost-vdpa.c
 *
 * Copyright(c) 2017-2018 Intel Corporation.
 * Copyright(c) 2020 Red Hat, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "clients.h"
#include "hw/virtio/virtio-net.h"
#include "net/vhost_net.h"
#include "net/vhost-vdpa.h"
#include "hw/virtio/vhost-vdpa.h"
#include "qemu/config-file.h"
#include "qemu/error-report.h"
#include "qemu/log.h"
#include "qemu/memalign.h"
#include "qemu/option.h"
#include "qapi/error.h"
#include <linux/vhost.h>
#include <sys/ioctl.h>
#include <err.h>
#include "standard-headers/linux/virtio_net.h"
#include "monitor/monitor.h"
#include "hw/virtio/vhost.h"

/* Todo:need to add the multiqueue support here */
typedef struct VhostVDPAState {
    NetClientState nc;
    struct vhost_vdpa vhost_vdpa;
    VHostNetState *vhost_net;

    /* Control commands shadow buffers */
    void *cvq_cmd_out_buffer, *cvq_cmd_in_buffer;
    bool started;
} VhostVDPAState;

const int vdpa_feature_bits[] = {
    VIRTIO_F_NOTIFY_ON_EMPTY,
    VIRTIO_RING_F_INDIRECT_DESC,
    VIRTIO_RING_F_EVENT_IDX,
    VIRTIO_F_ANY_LAYOUT,
    VIRTIO_F_VERSION_1,
    VIRTIO_NET_F_CSUM,
    VIRTIO_NET_F_GUEST_CSUM,
    VIRTIO_NET_F_GSO,
    VIRTIO_NET_F_GUEST_TSO4,
    VIRTIO_NET_F_GUEST_TSO6,
    VIRTIO_NET_F_GUEST_ECN,
    VIRTIO_NET_F_GUEST_UFO,
    VIRTIO_NET_F_HOST_TSO4,
    VIRTIO_NET_F_HOST_TSO6,
    VIRTIO_NET_F_HOST_ECN,
    VIRTIO_NET_F_HOST_UFO,
    VIRTIO_NET_F_MRG_RXBUF,
    VIRTIO_NET_F_MTU,
    VIRTIO_NET_F_CTRL_RX,
    VIRTIO_NET_F_CTRL_RX_EXTRA,
    VIRTIO_NET_F_CTRL_VLAN,
    VIRTIO_NET_F_GUEST_ANNOUNCE,
    VIRTIO_NET_F_CTRL_MAC_ADDR,
    VIRTIO_NET_F_RSS,
    VIRTIO_NET_F_MQ,
    VIRTIO_NET_F_CTRL_VQ,
    VIRTIO_F_IOMMU_PLATFORM,
    VIRTIO_F_RING_PACKED,
    VIRTIO_NET_F_RSS,
    VIRTIO_NET_F_HASH_REPORT,
    VIRTIO_NET_F_GUEST_ANNOUNCE,
    VIRTIO_NET_F_STATUS,
    VHOST_INVALID_FEATURE_BIT
};

/** Supported device specific feature bits with SVQ */
static const uint64_t vdpa_svq_device_features =
    BIT_ULL(VIRTIO_NET_F_CSUM) |
    BIT_ULL(VIRTIO_NET_F_GUEST_CSUM) |
    BIT_ULL(VIRTIO_NET_F_MTU) |
    BIT_ULL(VIRTIO_NET_F_MAC) |
    BIT_ULL(VIRTIO_NET_F_GUEST_TSO4) |
    BIT_ULL(VIRTIO_NET_F_GUEST_TSO6) |
    BIT_ULL(VIRTIO_NET_F_GUEST_ECN) |
    BIT_ULL(VIRTIO_NET_F_GUEST_UFO) |
    BIT_ULL(VIRTIO_NET_F_HOST_TSO4) |
    BIT_ULL(VIRTIO_NET_F_HOST_TSO6) |
    BIT_ULL(VIRTIO_NET_F_HOST_ECN) |
    BIT_ULL(VIRTIO_NET_F_HOST_UFO) |
    BIT_ULL(VIRTIO_NET_F_MRG_RXBUF) |
    BIT_ULL(VIRTIO_NET_F_STATUS) |
    BIT_ULL(VIRTIO_NET_F_CTRL_VQ) |
    BIT_ULL(VIRTIO_F_ANY_LAYOUT) |
    BIT_ULL(VIRTIO_NET_F_CTRL_MAC_ADDR) |
    BIT_ULL(VIRTIO_NET_F_RSC_EXT) |
    BIT_ULL(VIRTIO_NET_F_STANDBY);

VHostNetState *vhost_vdpa_get_vhost_net(NetClientState *nc)
{
    VhostVDPAState *s = DO_UPCAST(VhostVDPAState, nc, nc);
    assert(nc->info->type == NET_CLIENT_DRIVER_VHOST_VDPA);
    return s->vhost_net;
}

static int vhost_vdpa_net_check_device_id(struct vhost_net *net)
{
    uint32_t device_id;
    int ret;
    struct vhost_dev *hdev;

    hdev = (struct vhost_dev *)&net->dev;
    ret = hdev->vhost_ops->vhost_get_device_id(hdev, &device_id);
    if (device_id != VIRTIO_ID_NET) {
        return -ENOTSUP;
    }
    return ret;
}

static int vhost_vdpa_add(NetClientState *ncs, void *be,
                          int queue_pair_index, int nvqs)
{
    VhostNetOptions options;
    struct vhost_net *net = NULL;
    VhostVDPAState *s;
    int ret;

    options.backend_type = VHOST_BACKEND_TYPE_VDPA;
    assert(ncs->info->type == NET_CLIENT_DRIVER_VHOST_VDPA);
    s = DO_UPCAST(VhostVDPAState, nc, ncs);
    options.net_backend = ncs;
    options.opaque      = be;
    options.busyloop_timeout = 0;
    options.nvqs = nvqs;

    net = vhost_net_init(&options);
    if (!net) {
        error_report("failed to init vhost_net for queue");
        goto err_init;
    }
    s->vhost_net = net;
    ret = vhost_vdpa_net_check_device_id(net);
    if (ret) {
        goto err_check;
    }
    return 0;
err_check:
    vhost_net_cleanup(net);
    g_free(net);
err_init:
    return -1;
}

static void vhost_vdpa_cleanup(NetClientState *nc)
{
    VhostVDPAState *s = DO_UPCAST(VhostVDPAState, nc, nc);
    struct vhost_dev *dev = &s->vhost_net->dev;

    qemu_vfree(s->cvq_cmd_out_buffer);
    qemu_vfree(s->cvq_cmd_in_buffer);
    if (dev->vq_index + dev->nvqs == dev->vq_index_end) {
        g_clear_pointer(&s->vhost_vdpa.iova_tree, vhost_iova_tree_delete);
    }
    if (s->vhost_net) {
        vhost_net_cleanup(s->vhost_net);
        g_free(s->vhost_net);
        s->vhost_net = NULL;
    }
     if (s->vhost_vdpa.device_fd >= 0) {
        qemu_close(s->vhost_vdpa.device_fd);
        s->vhost_vdpa.device_fd = -1;
    }
}

static bool vhost_vdpa_has_vnet_hdr(NetClientState *nc)
{
    assert(nc->info->type == NET_CLIENT_DRIVER_VHOST_VDPA);

    return true;
}

static bool vhost_vdpa_has_ufo(NetClientState *nc)
{
    assert(nc->info->type == NET_CLIENT_DRIVER_VHOST_VDPA);
    VhostVDPAState *s = DO_UPCAST(VhostVDPAState, nc, nc);
    uint64_t features = 0;
    features |= (1ULL << VIRTIO_NET_F_HOST_UFO);
    features = vhost_net_get_features(s->vhost_net, features);
    return !!(features & (1ULL << VIRTIO_NET_F_HOST_UFO));

}

static bool vhost_vdpa_check_peer_type(NetClientState *nc, ObjectClass *oc,
                                       Error **errp)
{
    const char *driver = object_class_get_name(oc);

    if (!g_str_has_prefix(driver, "virtio-net-")) {
        error_setg(errp, "vhost-vdpa requires frontend driver virtio-net-*");
        return false;
    }

    return true;
}

/** Dummy receive in case qemu falls back to userland tap networking */
static ssize_t vhost_vdpa_receive(NetClientState *nc, const uint8_t *buf,
                                  size_t size)
{
    return 0;
}

static NetClientInfo net_vhost_vdpa_info = {
        .type = NET_CLIENT_DRIVER_VHOST_VDPA,
        .size = sizeof(VhostVDPAState),
        .receive = vhost_vdpa_receive,
        .cleanup = vhost_vdpa_cleanup,
        .has_vnet_hdr = vhost_vdpa_has_vnet_hdr,
        .has_ufo = vhost_vdpa_has_ufo,
        .check_peer_type = vhost_vdpa_check_peer_type,
};

static void vhost_vdpa_cvq_unmap_buf(struct vhost_vdpa *v, void *addr)
{
    VhostIOVATree *tree = v->iova_tree;
    DMAMap needle = {
        /*
         * No need to specify size or to look for more translations since
         * this contiguous chunk was allocated by us.
         */
        .translated_addr = (hwaddr)(uintptr_t)addr,
    };
    const DMAMap *map = vhost_iova_tree_find_iova(tree, &needle);
    int r;

    if (unlikely(!map)) {
        error_report("Cannot locate expected map");
        return;
    }

    r = vhost_vdpa_dma_unmap(v, map->iova, map->size + 1);
    if (unlikely(r != 0)) {
        error_report("Device cannot unmap: %s(%d)", g_strerror(r), r);
    }

    vhost_iova_tree_remove(tree, *map);
}

static size_t vhost_vdpa_net_cvq_cmd_len(void)
{
    /*
     * MAC_TABLE_SET is the ctrl command that produces the longer out buffer.
     * In buffer is always 1 byte, so it should fit here
     */
    return sizeof(struct virtio_net_ctrl_hdr) +
           2 * sizeof(struct virtio_net_ctrl_mac) +
           MAC_TABLE_ENTRIES * ETH_ALEN;
}

static size_t vhost_vdpa_net_cvq_cmd_page_len(void)
{
    return ROUND_UP(vhost_vdpa_net_cvq_cmd_len(), qemu_real_host_page_size());
}

/** Map CVQ buffer. */
static int vhost_vdpa_cvq_map_buf(struct vhost_vdpa *v, void *buf, size_t size,
                                  bool write)
{
    DMAMap map = {};
    int r;

    map.translated_addr = (hwaddr)(uintptr_t)buf;
    map.size = size - 1;
    map.perm = write ? IOMMU_RW : IOMMU_RO,
    r = vhost_iova_tree_map_alloc(v->iova_tree, &map);
    if (unlikely(r != IOVA_OK)) {
        error_report("Cannot map injected element");
        return r;
    }

    r = vhost_vdpa_dma_map(v, map.iova, vhost_vdpa_net_cvq_cmd_page_len(), buf,
                           !write);
    if (unlikely(r < 0)) {
        goto dma_map_err;
    }

    return 0;

dma_map_err:
    vhost_iova_tree_remove(v->iova_tree, map);
    return r;
}

static int vhost_vdpa_net_cvq_start(NetClientState *nc)
{
    VhostVDPAState *s;
    int r;

    assert(nc->info->type == NET_CLIENT_DRIVER_VHOST_VDPA);

    s = DO_UPCAST(VhostVDPAState, nc, nc);
    if (!s->vhost_vdpa.shadow_vqs_enabled) {
        return 0;
    }

    r = vhost_vdpa_cvq_map_buf(&s->vhost_vdpa, s->cvq_cmd_out_buffer,
                               vhost_vdpa_net_cvq_cmd_page_len(), false);
    if (unlikely(r < 0)) {
        return r;
    }

    r = vhost_vdpa_cvq_map_buf(&s->vhost_vdpa, s->cvq_cmd_in_buffer,
                               vhost_vdpa_net_cvq_cmd_page_len(), true);
    if (unlikely(r < 0)) {
        vhost_vdpa_cvq_unmap_buf(&s->vhost_vdpa, s->cvq_cmd_out_buffer);
    }

    return r;
}

static void vhost_vdpa_net_cvq_stop(NetClientState *nc)
{
    VhostVDPAState *s = DO_UPCAST(VhostVDPAState, nc, nc);

    assert(nc->info->type == NET_CLIENT_DRIVER_VHOST_VDPA);

    if (s->vhost_vdpa.shadow_vqs_enabled) {
        vhost_vdpa_cvq_unmap_buf(&s->vhost_vdpa, s->cvq_cmd_out_buffer);
        vhost_vdpa_cvq_unmap_buf(&s->vhost_vdpa, s->cvq_cmd_in_buffer);
    }
}

static ssize_t vhost_vdpa_net_cvq_add(VhostVDPAState *s, size_t out_len,
                                      size_t in_len)
{
    /* Buffers for the device */
    const struct iovec out = {
        .iov_base = s->cvq_cmd_out_buffer,
        .iov_len = out_len,
    };
    const struct iovec in = {
        .iov_base = s->cvq_cmd_in_buffer,
        .iov_len = sizeof(virtio_net_ctrl_ack),
    };
    VhostShadowVirtqueue *svq = g_ptr_array_index(s->vhost_vdpa.shadow_vqs, 0);
    int r;

    r = vhost_svq_add(svq, &out, 1, &in, 1, NULL);
    if (unlikely(r != 0)) {
        if (unlikely(r == -ENOSPC)) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: No space on device queue\n",
                          __func__);
        }
        return r;
    }

    /*
     * We can poll here since we've had BQL from the time we sent the
     * descriptor. Also, we need to take the answer before SVQ pulls by itself,
     * when BQL is released
     */
    return vhost_svq_poll(svq);
}

static int vhost_vdpa_net_load(NetClientState *nc)
{
    VhostVDPAState *s = DO_UPCAST(VhostVDPAState, nc, nc);
    const struct vhost_vdpa *v = &s->vhost_vdpa;
    const VirtIONet *n;
    uint64_t features;

    assert(nc->info->type == NET_CLIENT_DRIVER_VHOST_VDPA);

    if (!v->shadow_vqs_enabled) {
        return 0;
    }

    n = VIRTIO_NET(v->dev->vdev);
    features = n->parent_obj.guest_features;
    if (features & BIT_ULL(VIRTIO_NET_F_CTRL_MAC_ADDR)) {
        const struct virtio_net_ctrl_hdr ctrl = {
            .class = VIRTIO_NET_CTRL_MAC,
            .cmd = VIRTIO_NET_CTRL_MAC_ADDR_SET,
        };
        char *cursor = s->cvq_cmd_out_buffer;
        ssize_t dev_written;

        memcpy(cursor, &ctrl, sizeof(ctrl));
        cursor += sizeof(ctrl);
        memcpy(cursor, n->mac, sizeof(n->mac));

        dev_written = vhost_vdpa_net_cvq_add(s, sizeof(ctrl) + sizeof(n->mac),
                                             sizeof(virtio_net_ctrl_ack));
        if (unlikely(dev_written < 0)) {
            return dev_written;
        }

        return *((virtio_net_ctrl_ack *)s->cvq_cmd_in_buffer) != VIRTIO_NET_OK;
    }

    return 0;
}

static NetClientInfo net_vhost_vdpa_cvq_info = {
    .type = NET_CLIENT_DRIVER_VHOST_VDPA,
    .size = sizeof(VhostVDPAState),
    .receive = vhost_vdpa_receive,
    .start = vhost_vdpa_net_cvq_start,
    .load = vhost_vdpa_net_load,
    .stop = vhost_vdpa_net_cvq_stop,
    .cleanup = vhost_vdpa_cleanup,
    .has_vnet_hdr = vhost_vdpa_has_vnet_hdr,
    .has_ufo = vhost_vdpa_has_ufo,
    .check_peer_type = vhost_vdpa_check_peer_type,
};

/**
 * Do not forward commands not supported by SVQ. Otherwise, the device could
 * accept it and qemu would not know how to update the device model.
 */
static bool vhost_vdpa_net_cvq_validate_cmd(const void *out_buf, size_t len)
{
    struct virtio_net_ctrl_hdr ctrl;

    if (unlikely(len < sizeof(ctrl))) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid legnth of out buffer %zu\n", __func__, len);
        return false;
    }

    memcpy(&ctrl, out_buf, sizeof(ctrl));
    switch (ctrl.class) {
    case VIRTIO_NET_CTRL_MAC:
        switch (ctrl.cmd) {
        case VIRTIO_NET_CTRL_MAC_ADDR_SET:
            return true;
        default:
            qemu_log_mask(LOG_GUEST_ERROR, "%s: invalid mac cmd %u\n",
                          __func__, ctrl.cmd);
        };
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: invalid control class %u\n",
                      __func__, ctrl.class);
    };

    return false;
}

/**
 * Validate and copy control virtqueue commands.
 *
 * Following QEMU guidelines, we offer a copy of the buffers to the device to
 * prevent TOCTOU bugs.
 */
static int vhost_vdpa_net_handle_ctrl_avail(VhostShadowVirtqueue *svq,
                                            VirtQueueElement *elem,
                                            void *opaque)
{
    VhostVDPAState *s = opaque;
    size_t in_len;
    virtio_net_ctrl_ack status = VIRTIO_NET_ERR;
    /* Out buffer sent to both the vdpa device and the device model */
    struct iovec out = {
        .iov_base = s->cvq_cmd_out_buffer,
    };
    /* in buffer used for device model */
    const struct iovec in = {
        .iov_base = &status,
        .iov_len = sizeof(status),
    };
    ssize_t dev_written = -EINVAL;
    bool ok;

    out.iov_len = iov_to_buf(elem->out_sg, elem->out_num, 0,
                             s->cvq_cmd_out_buffer,
                             vhost_vdpa_net_cvq_cmd_len());
    ok = vhost_vdpa_net_cvq_validate_cmd(s->cvq_cmd_out_buffer, out.iov_len);
    if (unlikely(!ok)) {
        goto out;
    }

    dev_written = vhost_vdpa_net_cvq_add(s, out.iov_len, sizeof(status));
    if (unlikely(dev_written < 0)) {
        goto out;
    }

    if (unlikely(dev_written < sizeof(status))) {
        error_report("Insufficient written data (%zu)", dev_written);
        goto out;
    }

    memcpy(&status, s->cvq_cmd_in_buffer, sizeof(status));
    if (status != VIRTIO_NET_OK) {
        return VIRTIO_NET_ERR;
    }

    status = VIRTIO_NET_ERR;
    virtio_net_handle_ctrl_iov(svq->vdev, &in, 1, &out, 1);
    if (status != VIRTIO_NET_OK) {
        error_report("Bad CVQ processing in model");
    }

out:
    in_len = iov_from_buf(elem->in_sg, elem->in_num, 0, &status,
                          sizeof(status));
    if (unlikely(in_len < sizeof(status))) {
        error_report("Bad device CVQ written length");
    }
    vhost_svq_push_elem(svq, elem, MIN(in_len, sizeof(status)));
    g_free(elem);
    return dev_written < 0 ? dev_written : 0;
}

static const VhostShadowVirtqueueOps vhost_vdpa_net_svq_ops = {
    .avail_handler = vhost_vdpa_net_handle_ctrl_avail,
};

static NetClientState *net_vhost_vdpa_init(NetClientState *peer,
                                           const char *device,
                                           const char *name,
                                           int vdpa_device_fd,
                                           int queue_pair_index,
                                           int nvqs,
                                           bool is_datapath,
                                           bool svq,
                                           VhostIOVATree *iova_tree)
{
    NetClientState *nc = NULL;
    VhostVDPAState *s;
    int ret = 0;
    assert(name);
    if (is_datapath) {
        nc = qemu_new_net_client(&net_vhost_vdpa_info, peer, device,
                                 name);
    } else {
        nc = qemu_new_net_control_client(&net_vhost_vdpa_cvq_info, peer,
                                         device, name);
    }
    snprintf(nc->info_str, sizeof(nc->info_str), TYPE_VHOST_VDPA);
    s = DO_UPCAST(VhostVDPAState, nc, nc);

    s->vhost_vdpa.device_fd = vdpa_device_fd;
    s->vhost_vdpa.index = queue_pair_index;
    s->vhost_vdpa.shadow_vqs_enabled = svq;
    s->vhost_vdpa.iova_tree = iova_tree;
    if (!is_datapath) {
        s->cvq_cmd_out_buffer = qemu_memalign(qemu_real_host_page_size(),
                                            vhost_vdpa_net_cvq_cmd_page_len());
        memset(s->cvq_cmd_out_buffer, 0, vhost_vdpa_net_cvq_cmd_page_len());
        s->cvq_cmd_in_buffer = qemu_memalign(qemu_real_host_page_size(),
                                            vhost_vdpa_net_cvq_cmd_page_len());
        memset(s->cvq_cmd_in_buffer, 0, vhost_vdpa_net_cvq_cmd_page_len());

        s->vhost_vdpa.shadow_vq_ops = &vhost_vdpa_net_svq_ops;
        s->vhost_vdpa.shadow_vq_ops_opaque = s;
    }
    ret = vhost_vdpa_add(nc, (void *)&s->vhost_vdpa, queue_pair_index, nvqs);
    if (ret) {
        qemu_del_net_client(nc);
        return NULL;
    }
    return nc;
}

static int vhost_vdpa_get_iova_range(int fd,
                                     struct vhost_vdpa_iova_range *iova_range)
{
    int ret = ioctl(fd, VHOST_VDPA_GET_IOVA_RANGE, iova_range);

    return ret < 0 ? -errno : 0;
}

static int vhost_vdpa_get_features(int fd, uint64_t *features, Error **errp)
{
    int ret = ioctl(fd, VHOST_GET_FEATURES, features);
    if (unlikely(ret < 0)) {
        error_setg_errno(errp, errno,
                         "Fail to query features from vhost-vDPA device");
    }
    return ret;
}

static int vhost_vdpa_get_max_queue_pairs(int fd, uint64_t features,
                                          int *has_cvq, Error **errp)
{
    unsigned long config_size = offsetof(struct vhost_vdpa_config, buf);
    g_autofree struct vhost_vdpa_config *config = NULL;
    __virtio16 *max_queue_pairs;
    int ret;

    if (features & (1 << VIRTIO_NET_F_CTRL_VQ)) {
        *has_cvq = 1;
    } else {
        *has_cvq = 0;
    }

    if (features & (1 << VIRTIO_NET_F_MQ)) {
        config = g_malloc0(config_size + sizeof(*max_queue_pairs));
        config->off = offsetof(struct virtio_net_config, max_virtqueue_pairs);
        config->len = sizeof(*max_queue_pairs);

        ret = ioctl(fd, VHOST_VDPA_GET_CONFIG, config);
        if (ret) {
            error_setg(errp, "Fail to get config from vhost-vDPA device");
            return -ret;
        }

        max_queue_pairs = (__virtio16 *)&config->buf;

        return lduw_le_p(max_queue_pairs);
    }

    return 1;
}

int net_init_vhost_vdpa(const Netdev *netdev, const char *name,
                        NetClientState *peer, Error **errp)
{
    const NetdevVhostVDPAOptions *opts;
    uint64_t features;
    int vdpa_device_fd;
    g_autofree NetClientState **ncs = NULL;
    g_autoptr(VhostIOVATree) iova_tree = NULL;
    NetClientState *nc;
    int queue_pairs, r, i = 0, has_cvq = 0;

    assert(netdev->type == NET_CLIENT_DRIVER_VHOST_VDPA);
    opts = &netdev->u.vhost_vdpa;
    if (!opts->vhostdev) {
        error_setg(errp, "vdpa character device not specified with vhostdev");
        return -1;
    }

    vdpa_device_fd = qemu_open(opts->vhostdev, O_RDWR, errp);
    if (vdpa_device_fd == -1) {
        return -errno;
    }

    r = vhost_vdpa_get_features(vdpa_device_fd, &features, errp);
    if (unlikely(r < 0)) {
        goto err;
    }

    queue_pairs = vhost_vdpa_get_max_queue_pairs(vdpa_device_fd, features,
                                                 &has_cvq, errp);
    if (queue_pairs < 0) {
        qemu_close(vdpa_device_fd);
        return queue_pairs;
    }

    if (opts->x_svq) {
        struct vhost_vdpa_iova_range iova_range;

        uint64_t invalid_dev_features =
            features & ~vdpa_svq_device_features &
            /* Transport are all accepted at this point */
            ~MAKE_64BIT_MASK(VIRTIO_TRANSPORT_F_START,
                             VIRTIO_TRANSPORT_F_END - VIRTIO_TRANSPORT_F_START);

        if (invalid_dev_features) {
            error_setg(errp, "vdpa svq does not work with features 0x%" PRIx64,
                       invalid_dev_features);
            goto err_svq;
        }

        vhost_vdpa_get_iova_range(vdpa_device_fd, &iova_range);
        iova_tree = vhost_iova_tree_new(iova_range.first, iova_range.last);
    }

    ncs = g_malloc0(sizeof(*ncs) * queue_pairs);

    for (i = 0; i < queue_pairs; i++) {
        ncs[i] = net_vhost_vdpa_init(peer, TYPE_VHOST_VDPA, name,
                                     vdpa_device_fd, i, 2, true, opts->x_svq,
                                     iova_tree);
        if (!ncs[i])
            goto err;
    }

    if (has_cvq) {
        nc = net_vhost_vdpa_init(peer, TYPE_VHOST_VDPA, name,
                                 vdpa_device_fd, i, 1, false,
                                 opts->x_svq, iova_tree);
        if (!nc)
            goto err;
    }

    /* iova_tree ownership belongs to last NetClientState */
    g_steal_pointer(&iova_tree);
    return 0;

err:
    if (i) {
        for (i--; i >= 0; i--) {
            qemu_del_net_client(ncs[i]);
        }
    }

err_svq:
    qemu_close(vdpa_device_fd);

    return -1;
}
