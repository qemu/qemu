/*
 * Sharing QEMU block devices via vhost-user protocal
 *
 * Parts of the code based on nbd/server.c.
 *
 * Copyright (c) Coiby Xu <coiby.xu@gmail.com>.
 * Copyright (c) 2020 Red Hat, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 */
#include "qemu/osdep.h"
#include "block/block.h"
#include "vhost-user-blk-server.h"
#include "qapi/error.h"
#include "qom/object_interfaces.h"
#include "sysemu/block-backend.h"
#include "util/block-helpers.h"

enum {
    VHOST_USER_BLK_MAX_QUEUES = 1,
};
struct virtio_blk_inhdr {
    unsigned char status;
};

typedef struct VuBlockReq {
    VuVirtqElement elem;
    int64_t sector_num;
    size_t size;
    struct virtio_blk_inhdr *in;
    struct virtio_blk_outhdr out;
    VuServer *server;
    struct VuVirtq *vq;
} VuBlockReq;

static void vu_block_req_complete(VuBlockReq *req)
{
    VuDev *vu_dev = &req->server->vu_dev;

    /* IO size with 1 extra status byte */
    vu_queue_push(vu_dev, req->vq, &req->elem, req->size + 1);
    vu_queue_notify(vu_dev, req->vq);

    free(req);
}

static VuBlockDev *get_vu_block_device_by_server(VuServer *server)
{
    return container_of(server, VuBlockDev, vu_server);
}

static int coroutine_fn
vu_block_discard_write_zeroes(VuBlockReq *req, struct iovec *iov,
                              uint32_t iovcnt, uint32_t type)
{
    struct virtio_blk_discard_write_zeroes desc;
    ssize_t size = iov_to_buf(iov, iovcnt, 0, &desc, sizeof(desc));
    if (unlikely(size != sizeof(desc))) {
        error_report("Invalid size %zd, expect %zu", size, sizeof(desc));
        return -EINVAL;
    }

    VuBlockDev *vdev_blk = get_vu_block_device_by_server(req->server);
    uint64_t range[2] = { le64_to_cpu(desc.sector) << 9,
                          le32_to_cpu(desc.num_sectors) << 9 };
    if (type == VIRTIO_BLK_T_DISCARD) {
        if (blk_co_pdiscard(vdev_blk->backend, range[0], range[1]) == 0) {
            return 0;
        }
    } else if (type == VIRTIO_BLK_T_WRITE_ZEROES) {
        if (blk_co_pwrite_zeroes(vdev_blk->backend,
                                 range[0], range[1], 0) == 0) {
            return 0;
        }
    }

    return -EINVAL;
}

static void coroutine_fn vu_block_flush(VuBlockReq *req)
{
    VuBlockDev *vdev_blk = get_vu_block_device_by_server(req->server);
    BlockBackend *backend = vdev_blk->backend;
    blk_co_flush(backend);
}

static void coroutine_fn vu_block_virtio_process_req(void *opaque)
{
    VuBlockReq *req = opaque;
    VuServer *server = req->server;
    VuVirtqElement *elem = &req->elem;
    uint32_t type;

    VuBlockDev *vdev_blk = get_vu_block_device_by_server(server);
    BlockBackend *backend = vdev_blk->backend;

    struct iovec *in_iov = elem->in_sg;
    struct iovec *out_iov = elem->out_sg;
    unsigned in_num = elem->in_num;
    unsigned out_num = elem->out_num;

    /* refer to hw/block/virtio_blk.c */
    if (elem->out_num < 1 || elem->in_num < 1) {
        error_report("virtio-blk request missing headers");
        goto err;
    }

    if (unlikely(iov_to_buf(out_iov, out_num, 0, &req->out,
                            sizeof(req->out)) != sizeof(req->out))) {
        error_report("virtio-blk request outhdr too short");
        goto err;
    }

    iov_discard_front(&out_iov, &out_num, sizeof(req->out));

    if (in_iov[in_num - 1].iov_len < sizeof(struct virtio_blk_inhdr)) {
        error_report("virtio-blk request inhdr too short");
        goto err;
    }

    /* We always touch the last byte, so just see how big in_iov is.  */
    req->in = (void *)in_iov[in_num - 1].iov_base
              + in_iov[in_num - 1].iov_len
              - sizeof(struct virtio_blk_inhdr);
    iov_discard_back(in_iov, &in_num, sizeof(struct virtio_blk_inhdr));

    type = le32_to_cpu(req->out.type);
    switch (type & ~VIRTIO_BLK_T_BARRIER) {
    case VIRTIO_BLK_T_IN:
    case VIRTIO_BLK_T_OUT: {
        ssize_t ret = 0;
        bool is_write = type & VIRTIO_BLK_T_OUT;
        req->sector_num = le64_to_cpu(req->out.sector);

        int64_t offset = req->sector_num * vdev_blk->blk_size;
        QEMUIOVector qiov;
        if (is_write) {
            qemu_iovec_init_external(&qiov, out_iov, out_num);
            ret = blk_co_pwritev(backend, offset, qiov.size,
                                 &qiov, 0);
        } else {
            qemu_iovec_init_external(&qiov, in_iov, in_num);
            ret = blk_co_preadv(backend, offset, qiov.size,
                                &qiov, 0);
        }
        if (ret >= 0) {
            req->in->status = VIRTIO_BLK_S_OK;
        } else {
            req->in->status = VIRTIO_BLK_S_IOERR;
        }
        break;
    }
    case VIRTIO_BLK_T_FLUSH:
        vu_block_flush(req);
        req->in->status = VIRTIO_BLK_S_OK;
        break;
    case VIRTIO_BLK_T_GET_ID: {
        size_t size = MIN(iov_size(&elem->in_sg[0], in_num),
                          VIRTIO_BLK_ID_BYTES);
        snprintf(elem->in_sg[0].iov_base, size, "%s", "vhost_user_blk");
        req->in->status = VIRTIO_BLK_S_OK;
        req->size = elem->in_sg[0].iov_len;
        break;
    }
    case VIRTIO_BLK_T_DISCARD:
    case VIRTIO_BLK_T_WRITE_ZEROES: {
        int rc;
        rc = vu_block_discard_write_zeroes(req, &elem->out_sg[1],
                                           out_num, type);
        if (rc == 0) {
            req->in->status = VIRTIO_BLK_S_OK;
        } else {
            req->in->status = VIRTIO_BLK_S_IOERR;
        }
        break;
    }
    default:
        req->in->status = VIRTIO_BLK_S_UNSUPP;
        break;
    }

    vu_block_req_complete(req);
    return;

err:
    free(elem);
}

static void vu_block_process_vq(VuDev *vu_dev, int idx)
{
    VuServer *server = container_of(vu_dev, VuServer, vu_dev);
    VuVirtq *vq = vu_get_queue(vu_dev, idx);

    while (1) {
        VuBlockReq *req;

        req = vu_queue_pop(vu_dev, vq, sizeof(VuBlockReq));
        if (!req) {
            break;
        }

        req->server = server;
        req->vq = vq;

        Coroutine *co =
            qemu_coroutine_create(vu_block_virtio_process_req, req);
        qemu_coroutine_enter(co);
    }
}

static void vu_block_queue_set_started(VuDev *vu_dev, int idx, bool started)
{
    VuVirtq *vq;

    assert(vu_dev);

    vq = vu_get_queue(vu_dev, idx);
    vu_set_queue_handler(vu_dev, vq, started ? vu_block_process_vq : NULL);
}

static uint64_t vu_block_get_features(VuDev *dev)
{
    uint64_t features;
    VuServer *server = container_of(dev, VuServer, vu_dev);
    VuBlockDev *vdev_blk = get_vu_block_device_by_server(server);
    features = 1ull << VIRTIO_BLK_F_SIZE_MAX |
               1ull << VIRTIO_BLK_F_SEG_MAX |
               1ull << VIRTIO_BLK_F_TOPOLOGY |
               1ull << VIRTIO_BLK_F_BLK_SIZE |
               1ull << VIRTIO_BLK_F_FLUSH |
               1ull << VIRTIO_BLK_F_DISCARD |
               1ull << VIRTIO_BLK_F_WRITE_ZEROES |
               1ull << VIRTIO_BLK_F_CONFIG_WCE |
               1ull << VIRTIO_F_VERSION_1 |
               1ull << VIRTIO_RING_F_INDIRECT_DESC |
               1ull << VIRTIO_RING_F_EVENT_IDX |
               1ull << VHOST_USER_F_PROTOCOL_FEATURES;

    if (!vdev_blk->writable) {
        features |= 1ull << VIRTIO_BLK_F_RO;
    }

    return features;
}

static uint64_t vu_block_get_protocol_features(VuDev *dev)
{
    return 1ull << VHOST_USER_PROTOCOL_F_CONFIG |
           1ull << VHOST_USER_PROTOCOL_F_INFLIGHT_SHMFD;
}

static int
vu_block_get_config(VuDev *vu_dev, uint8_t *config, uint32_t len)
{
    VuServer *server = container_of(vu_dev, VuServer, vu_dev);
    VuBlockDev *vdev_blk = get_vu_block_device_by_server(server);
    memcpy(config, &vdev_blk->blkcfg, len);

    return 0;
}

static int
vu_block_set_config(VuDev *vu_dev, const uint8_t *data,
                    uint32_t offset, uint32_t size, uint32_t flags)
{
    VuServer *server = container_of(vu_dev, VuServer, vu_dev);
    VuBlockDev *vdev_blk = get_vu_block_device_by_server(server);
    uint8_t wce;

    /* don't support live migration */
    if (flags != VHOST_SET_CONFIG_TYPE_MASTER) {
        return -EINVAL;
    }

    if (offset != offsetof(struct virtio_blk_config, wce) ||
        size != 1) {
        return -EINVAL;
    }

    wce = *data;
    vdev_blk->blkcfg.wce = wce;
    blk_set_enable_write_cache(vdev_blk->backend, wce);
    return 0;
}

/*
 * When the client disconnects, it sends a VHOST_USER_NONE request
 * and vu_process_message will simple call exit which cause the VM
 * to exit abruptly.
 * To avoid this issue,  process VHOST_USER_NONE request ahead
 * of vu_process_message.
 *
 */
static int vu_block_process_msg(VuDev *dev, VhostUserMsg *vmsg, int *do_reply)
{
    if (vmsg->request == VHOST_USER_NONE) {
        dev->panic(dev, "disconnect");
        return true;
    }
    return false;
}

static const VuDevIface vu_block_iface = {
    .get_features          = vu_block_get_features,
    .queue_set_started     = vu_block_queue_set_started,
    .get_protocol_features = vu_block_get_protocol_features,
    .get_config            = vu_block_get_config,
    .set_config            = vu_block_set_config,
    .process_msg           = vu_block_process_msg,
};

static void blk_aio_attached(AioContext *ctx, void *opaque)
{
    VuBlockDev *vub_dev = opaque;
    vhost_user_server_attach_aio_context(&vub_dev->vu_server, ctx);
}

static void blk_aio_detach(void *opaque)
{
    VuBlockDev *vub_dev = opaque;
    vhost_user_server_detach_aio_context(&vub_dev->vu_server);
}

static void
vu_block_initialize_config(BlockDriverState *bs,
                           struct virtio_blk_config *config, uint32_t blk_size)
{
    config->capacity = bdrv_getlength(bs) >> BDRV_SECTOR_BITS;
    config->blk_size = blk_size;
    config->size_max = 0;
    config->seg_max = 128 - 2;
    config->min_io_size = 1;
    config->opt_io_size = 1;
    config->num_queues = VHOST_USER_BLK_MAX_QUEUES;
    config->max_discard_sectors = 32768;
    config->max_discard_seg = 1;
    config->discard_sector_alignment = config->blk_size >> 9;
    config->max_write_zeroes_sectors = 32768;
    config->max_write_zeroes_seg = 1;
}

static VuBlockDev *vu_block_init(VuBlockDev *vu_block_device, Error **errp)
{

    BlockBackend *blk;
    Error *local_error = NULL;
    const char *node_name = vu_block_device->node_name;
    bool writable = vu_block_device->writable;
    uint64_t perm = BLK_PERM_CONSISTENT_READ;
    int ret;

    AioContext *ctx;

    BlockDriverState *bs = bdrv_lookup_bs(node_name, node_name, &local_error);

    if (!bs) {
        error_propagate(errp, local_error);
        return NULL;
    }

    if (bdrv_is_read_only(bs)) {
        writable = false;
    }

    if (writable) {
        perm |= BLK_PERM_WRITE;
    }

    ctx = bdrv_get_aio_context(bs);
    aio_context_acquire(ctx);
    bdrv_invalidate_cache(bs, NULL);
    aio_context_release(ctx);

    /*
     * Don't allow resize while the vhost user server is running,
     * otherwise we don't care what happens with the node.
     */
    blk = blk_new(bdrv_get_aio_context(bs), perm,
                  BLK_PERM_CONSISTENT_READ | BLK_PERM_WRITE_UNCHANGED |
                  BLK_PERM_WRITE | BLK_PERM_GRAPH_MOD);
    ret = blk_insert_bs(blk, bs, errp);

    if (ret < 0) {
        goto fail;
    }

    blk_set_enable_write_cache(blk, false);

    blk_set_allow_aio_context_change(blk, true);

    vu_block_device->blkcfg.wce = 0;
    vu_block_device->backend = blk;
    if (!vu_block_device->blk_size) {
        vu_block_device->blk_size = BDRV_SECTOR_SIZE;
    }
    vu_block_device->blkcfg.blk_size = vu_block_device->blk_size;
    blk_set_guest_block_size(blk, vu_block_device->blk_size);
    vu_block_initialize_config(bs, &vu_block_device->blkcfg,
                                   vu_block_device->blk_size);
    return vu_block_device;

fail:
    blk_unref(blk);
    return NULL;
}

static void vu_block_deinit(VuBlockDev *vu_block_device)
{
    if (vu_block_device->backend) {
        blk_remove_aio_context_notifier(vu_block_device->backend, blk_aio_attached,
                                        blk_aio_detach, vu_block_device);
    }

    blk_unref(vu_block_device->backend);
}

static void vhost_user_blk_server_stop(VuBlockDev *vu_block_device)
{
    vhost_user_server_stop(&vu_block_device->vu_server);
    vu_block_deinit(vu_block_device);
}

static void vhost_user_blk_server_start(VuBlockDev *vu_block_device,
                                        Error **errp)
{
    AioContext *ctx;
    SocketAddress *addr = vu_block_device->addr;

    if (!vu_block_init(vu_block_device, errp)) {
        return;
    }

    ctx = bdrv_get_aio_context(blk_bs(vu_block_device->backend));

    if (!vhost_user_server_start(&vu_block_device->vu_server, addr, ctx,
                                 VHOST_USER_BLK_MAX_QUEUES, &vu_block_iface,
                                 errp)) {
        goto error;
    }

    blk_add_aio_context_notifier(vu_block_device->backend, blk_aio_attached,
                                 blk_aio_detach, vu_block_device);
    vu_block_device->running = true;
    return;

 error:
    vu_block_deinit(vu_block_device);
}

static bool vu_prop_modifiable(VuBlockDev *vus, Error **errp)
{
    if (vus->running) {
            error_setg(errp, "The property can't be modified "
                       "while the server is running");
            return false;
    }
    return true;
}

static void vu_set_node_name(Object *obj, const char *value, Error **errp)
{
    VuBlockDev *vus = VHOST_USER_BLK_SERVER(obj);

    if (!vu_prop_modifiable(vus, errp)) {
        return;
    }

    if (vus->node_name) {
        g_free(vus->node_name);
    }

    vus->node_name = g_strdup(value);
}

static char *vu_get_node_name(Object *obj, Error **errp)
{
    VuBlockDev *vus = VHOST_USER_BLK_SERVER(obj);
    return g_strdup(vus->node_name);
}

static void free_socket_addr(SocketAddress *addr)
{
        g_free(addr->u.q_unix.path);
        g_free(addr);
}

static void vu_set_unix_socket(Object *obj, const char *value,
                               Error **errp)
{
    VuBlockDev *vus = VHOST_USER_BLK_SERVER(obj);

    if (!vu_prop_modifiable(vus, errp)) {
        return;
    }

    if (vus->addr) {
        free_socket_addr(vus->addr);
    }

    SocketAddress *addr = g_new0(SocketAddress, 1);
    addr->type = SOCKET_ADDRESS_TYPE_UNIX;
    addr->u.q_unix.path = g_strdup(value);
    vus->addr = addr;
}

static char *vu_get_unix_socket(Object *obj, Error **errp)
{
    VuBlockDev *vus = VHOST_USER_BLK_SERVER(obj);
    return g_strdup(vus->addr->u.q_unix.path);
}

static bool vu_get_block_writable(Object *obj, Error **errp)
{
    VuBlockDev *vus = VHOST_USER_BLK_SERVER(obj);
    return vus->writable;
}

static void vu_set_block_writable(Object *obj, bool value, Error **errp)
{
    VuBlockDev *vus = VHOST_USER_BLK_SERVER(obj);

    if (!vu_prop_modifiable(vus, errp)) {
            return;
    }

    vus->writable = value;
}

static void vu_get_blk_size(Object *obj, Visitor *v, const char *name,
                            void *opaque, Error **errp)
{
    VuBlockDev *vus = VHOST_USER_BLK_SERVER(obj);
    uint32_t value = vus->blk_size;

    visit_type_uint32(v, name, &value, errp);
}

static void vu_set_blk_size(Object *obj, Visitor *v, const char *name,
                            void *opaque, Error **errp)
{
    VuBlockDev *vus = VHOST_USER_BLK_SERVER(obj);

    Error *local_err = NULL;
    uint32_t value;

    if (!vu_prop_modifiable(vus, errp)) {
            return;
    }

    visit_type_uint32(v, name, &value, &local_err);
    if (local_err) {
        goto out;
    }

    check_block_size(object_get_typename(obj), name, value, &local_err);
    if (local_err) {
        goto out;
    }

    vus->blk_size = value;

out:
    error_propagate(errp, local_err);
}

static void vhost_user_blk_server_instance_finalize(Object *obj)
{
    VuBlockDev *vub = VHOST_USER_BLK_SERVER(obj);

    vhost_user_blk_server_stop(vub);

    /*
     * Unlike object_property_add_str, object_class_property_add_str
     * doesn't have a release method. Thus manual memory freeing is
     * needed.
     */
    free_socket_addr(vub->addr);
    g_free(vub->node_name);
}

static void vhost_user_blk_server_complete(UserCreatable *obj, Error **errp)
{
    VuBlockDev *vub = VHOST_USER_BLK_SERVER(obj);

    vhost_user_blk_server_start(vub, errp);
}

static void vhost_user_blk_server_class_init(ObjectClass *klass,
                                             void *class_data)
{
    UserCreatableClass *ucc = USER_CREATABLE_CLASS(klass);
    ucc->complete = vhost_user_blk_server_complete;

    object_class_property_add_bool(klass, "writable",
                                   vu_get_block_writable,
                                   vu_set_block_writable);

    object_class_property_add_str(klass, "node-name",
                                  vu_get_node_name,
                                  vu_set_node_name);

    object_class_property_add_str(klass, "unix-socket",
                                  vu_get_unix_socket,
                                  vu_set_unix_socket);

    object_class_property_add(klass, "logical-block-size", "uint32",
                              vu_get_blk_size, vu_set_blk_size,
                              NULL, NULL);
}

static const TypeInfo vhost_user_blk_server_info = {
    .name = TYPE_VHOST_USER_BLK_SERVER,
    .parent = TYPE_OBJECT,
    .instance_size = sizeof(VuBlockDev),
    .instance_finalize = vhost_user_blk_server_instance_finalize,
    .class_init = vhost_user_blk_server_class_init,
    .interfaces = (InterfaceInfo[]) {
        {TYPE_USER_CREATABLE},
        {}
    },
};

static void vhost_user_blk_server_register_types(void)
{
    type_register_static(&vhost_user_blk_server_info);
}

type_init(vhost_user_blk_server_register_types)
