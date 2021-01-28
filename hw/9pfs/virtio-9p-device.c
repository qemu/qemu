/*
 * Virtio 9p backend
 *
 * Copyright IBM, Corp. 2010
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "hw/virtio/virtio.h"
#include "qemu/sockets.h"
#include "virtio-9p.h"
#include "fsdev/qemu-fsdev.h"
#include "coth.h"
#include "hw/qdev-properties.h"
#include "hw/virtio/virtio-access.h"
#include "qemu/iov.h"
#include "qemu/module.h"
#include "sysemu/qtest.h"

static void virtio_9p_push_and_notify(V9fsPDU *pdu)
{
    V9fsState *s = pdu->s;
    V9fsVirtioState *v = container_of(s, V9fsVirtioState, state);
    VirtQueueElement *elem = v->elems[pdu->idx];

    /* push onto queue and notify */
    virtqueue_push(v->vq, elem, pdu->size);
    g_free(elem);
    v->elems[pdu->idx] = NULL;

    /* FIXME: we should batch these completions */
    virtio_notify(VIRTIO_DEVICE(v), v->vq);
}

static void handle_9p_output(VirtIODevice *vdev, VirtQueue *vq)
{
    V9fsVirtioState *v = (V9fsVirtioState *)vdev;
    V9fsState *s = &v->state;
    V9fsPDU *pdu;
    ssize_t len;
    VirtQueueElement *elem;

    while ((pdu = pdu_alloc(s))) {
        P9MsgHeader out;

        elem = virtqueue_pop(vq, sizeof(VirtQueueElement));
        if (!elem) {
            goto out_free_pdu;
        }

        if (iov_size(elem->in_sg, elem->in_num) < 7) {
            virtio_error(vdev,
                         "The guest sent a VirtFS request without space for "
                         "the reply");
            goto out_free_req;
        }

        len = iov_to_buf(elem->out_sg, elem->out_num, 0, &out, 7);
        if (len != 7) {
            virtio_error(vdev, "The guest sent a malformed VirtFS request: "
                         "header size is %zd, should be 7", len);
            goto out_free_req;
        }

        v->elems[pdu->idx] = elem;

        pdu_submit(pdu, &out);
    }

    return;

out_free_req:
    virtqueue_detach_element(vq, elem, 0);
    g_free(elem);
out_free_pdu:
    pdu_free(pdu);
}

static uint64_t virtio_9p_get_features(VirtIODevice *vdev, uint64_t features,
                                       Error **errp)
{
    virtio_add_feature(&features, VIRTIO_9P_MOUNT_TAG);
    return features;
}

static void virtio_9p_get_config(VirtIODevice *vdev, uint8_t *config)
{
    int len;
    struct virtio_9p_config *cfg;
    V9fsVirtioState *v = VIRTIO_9P(vdev);
    V9fsState *s = &v->state;

    len = strlen(s->tag);
    cfg = g_malloc0(sizeof(struct virtio_9p_config) + len);
    virtio_stw_p(vdev, &cfg->tag_len, len);
    /* We don't copy the terminating null to config space */
    memcpy(cfg->tag, s->tag, len);
    memcpy(config, cfg, v->config_size);
    g_free(cfg);
}

static void virtio_9p_reset(VirtIODevice *vdev)
{
    V9fsVirtioState *v = (V9fsVirtioState *)vdev;

    v9fs_reset(&v->state);
}

static ssize_t virtio_pdu_vmarshal(V9fsPDU *pdu, size_t offset,
                                   const char *fmt, va_list ap)
{
    V9fsState *s = pdu->s;
    V9fsVirtioState *v = container_of(s, V9fsVirtioState, state);
    VirtQueueElement *elem = v->elems[pdu->idx];
    ssize_t ret;

    ret = v9fs_iov_vmarshal(elem->in_sg, elem->in_num, offset, 1, fmt, ap);
    if (ret < 0) {
        VirtIODevice *vdev = VIRTIO_DEVICE(v);

        virtio_error(vdev, "Failed to encode VirtFS reply type %d",
                     pdu->id + 1);
    }
    return ret;
}

static ssize_t virtio_pdu_vunmarshal(V9fsPDU *pdu, size_t offset,
                                     const char *fmt, va_list ap)
{
    V9fsState *s = pdu->s;
    V9fsVirtioState *v = container_of(s, V9fsVirtioState, state);
    VirtQueueElement *elem = v->elems[pdu->idx];
    ssize_t ret;

    ret = v9fs_iov_vunmarshal(elem->out_sg, elem->out_num, offset, 1, fmt, ap);
    if (ret < 0) {
        VirtIODevice *vdev = VIRTIO_DEVICE(v);

        virtio_error(vdev, "Failed to decode VirtFS request type %d", pdu->id);
    }
    return ret;
}

static void virtio_init_in_iov_from_pdu(V9fsPDU *pdu, struct iovec **piov,
                                        unsigned int *pniov, size_t size)
{
    V9fsState *s = pdu->s;
    V9fsVirtioState *v = container_of(s, V9fsVirtioState, state);
    VirtQueueElement *elem = v->elems[pdu->idx];
    size_t buf_size = iov_size(elem->in_sg, elem->in_num);

    if (buf_size < size) {
        VirtIODevice *vdev = VIRTIO_DEVICE(v);

        virtio_error(vdev,
                     "VirtFS reply type %d needs %zu bytes, buffer has %zu",
                     pdu->id + 1, size, buf_size);
    }

    *piov = elem->in_sg;
    *pniov = elem->in_num;
}

static void virtio_init_out_iov_from_pdu(V9fsPDU *pdu, struct iovec **piov,
                                         unsigned int *pniov, size_t size)
{
    V9fsState *s = pdu->s;
    V9fsVirtioState *v = container_of(s, V9fsVirtioState, state);
    VirtQueueElement *elem = v->elems[pdu->idx];
    size_t buf_size = iov_size(elem->out_sg, elem->out_num);

    if (buf_size < size) {
        VirtIODevice *vdev = VIRTIO_DEVICE(v);

        virtio_error(vdev,
                     "VirtFS request type %d needs %zu bytes, buffer has %zu",
                     pdu->id, size, buf_size);
    }

    *piov = elem->out_sg;
    *pniov = elem->out_num;
}

static const V9fsTransport virtio_9p_transport = {
    .pdu_vmarshal = virtio_pdu_vmarshal,
    .pdu_vunmarshal = virtio_pdu_vunmarshal,
    .init_in_iov_from_pdu = virtio_init_in_iov_from_pdu,
    .init_out_iov_from_pdu = virtio_init_out_iov_from_pdu,
    .push_and_notify = virtio_9p_push_and_notify,
};

static void virtio_9p_device_realize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    V9fsVirtioState *v = VIRTIO_9P(dev);
    V9fsState *s = &v->state;
    FsDriverEntry *fse = get_fsdev_fsentry(s->fsconf.fsdev_id);

    if (qtest_enabled() && fse) {
        fse->export_flags |= V9FS_NO_PERF_WARN;
    }

    if (v9fs_device_realize_common(s, &virtio_9p_transport, errp)) {
        return;
    }

    v->config_size = sizeof(struct virtio_9p_config) + strlen(s->fsconf.tag);
    virtio_init(vdev, "virtio-9p", VIRTIO_ID_9P, v->config_size);
    v->vq = virtio_add_queue(vdev, MAX_REQ, handle_9p_output);
}

static void virtio_9p_device_unrealize(DeviceState *dev)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    V9fsVirtioState *v = VIRTIO_9P(dev);
    V9fsState *s = &v->state;

    virtio_delete_queue(v->vq);
    virtio_cleanup(vdev);
    v9fs_device_unrealize_common(s);
}

/* virtio-9p device */

static const VMStateDescription vmstate_virtio_9p = {
    .name = "virtio-9p",
    .minimum_version_id = 1,
    .version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_VIRTIO_DEVICE,
        VMSTATE_END_OF_LIST()
    },
};

static Property virtio_9p_properties[] = {
    DEFINE_PROP_STRING("mount_tag", V9fsVirtioState, state.fsconf.tag),
    DEFINE_PROP_STRING("fsdev", V9fsVirtioState, state.fsconf.fsdev_id),
    DEFINE_PROP_END_OF_LIST(),
};

static void virtio_9p_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_CLASS(klass);

    device_class_set_props(dc, virtio_9p_properties);
    dc->vmsd = &vmstate_virtio_9p;
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
    vdc->realize = virtio_9p_device_realize;
    vdc->unrealize = virtio_9p_device_unrealize;
    vdc->get_features = virtio_9p_get_features;
    vdc->get_config = virtio_9p_get_config;
    vdc->reset = virtio_9p_reset;
}

static const TypeInfo virtio_device_info = {
    .name = TYPE_VIRTIO_9P,
    .parent = TYPE_VIRTIO_DEVICE,
    .instance_size = sizeof(V9fsVirtioState),
    .class_init = virtio_9p_class_init,
};

static void virtio_9p_register_types(void)
{
    type_register_static(&virtio_device_info);
}

type_init(virtio_9p_register_types)
