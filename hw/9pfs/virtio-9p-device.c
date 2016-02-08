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
#include "hw/i386/pc.h"
#include "qemu/sockets.h"
#include "virtio-9p.h"
#include "fsdev/qemu-fsdev.h"
#include "9p-xattr.h"
#include "coth.h"
#include "hw/virtio/virtio-access.h"
#include "qemu/iov.h"

void virtio_9p_push_and_notify(V9fsPDU *pdu)
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

    while ((pdu = pdu_alloc(s))) {
        struct {
            uint32_t size_le;
            uint8_t id;
            uint16_t tag_le;
        } QEMU_PACKED out;
        VirtQueueElement *elem;

        elem = virtqueue_pop(vq, sizeof(VirtQueueElement));
        if (!elem) {
            pdu_free(pdu);
            break;
        }

        BUG_ON(elem->out_num == 0 || elem->in_num == 0);
        QEMU_BUILD_BUG_ON(sizeof out != 7);

        v->elems[pdu->idx] = elem;
        len = iov_to_buf(elem->out_sg, elem->out_num, 0,
                         &out, sizeof out);
        BUG_ON(len != sizeof out);

        pdu->size = le32_to_cpu(out.size_le);

        pdu->id = out.id;
        pdu->tag = le16_to_cpu(out.tag_le);

        qemu_co_queue_init(&pdu->complete);
        pdu_submit(pdu);
    }
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

static void virtio_9p_save(QEMUFile *f, void *opaque)
{
    virtio_save(VIRTIO_DEVICE(opaque), f);
}

static int virtio_9p_load(QEMUFile *f, void *opaque, int version_id)
{
    return virtio_load(VIRTIO_DEVICE(opaque), f, version_id);
}

static void virtio_9p_device_realize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    V9fsVirtioState *v = VIRTIO_9P(dev);
    V9fsState *s = &v->state;

    if (v9fs_device_realize_common(s, errp)) {
        goto out;
    }

    v->config_size = sizeof(struct virtio_9p_config) + strlen(s->fsconf.tag);
    virtio_init(vdev, "virtio-9p", VIRTIO_ID_9P, v->config_size);
    v->vq = virtio_add_queue(vdev, MAX_REQ, handle_9p_output);
    register_savevm(dev, "virtio-9p", -1, 1, virtio_9p_save, virtio_9p_load, v);

out:
    return;
}

static void virtio_9p_device_unrealize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    V9fsVirtioState *v = VIRTIO_9P(dev);
    V9fsState *s = &v->state;

    virtio_cleanup(vdev);
    unregister_savevm(dev, "virtio-9p", v);
    v9fs_device_unrealize_common(s, errp);
}

ssize_t virtio_pdu_vmarshal(V9fsPDU *pdu, size_t offset,
                            const char *fmt, va_list ap)
{
    V9fsState *s = pdu->s;
    V9fsVirtioState *v = container_of(s, V9fsVirtioState, state);
    VirtQueueElement *elem = v->elems[pdu->idx];

    return v9fs_iov_vmarshal(elem->in_sg, elem->in_num, offset, 1, fmt, ap);
}

ssize_t virtio_pdu_vunmarshal(V9fsPDU *pdu, size_t offset,
                              const char *fmt, va_list ap)
{
    V9fsState *s = pdu->s;
    V9fsVirtioState *v = container_of(s, V9fsVirtioState, state);
    VirtQueueElement *elem = v->elems[pdu->idx];

    return v9fs_iov_vunmarshal(elem->out_sg, elem->out_num, offset, 1, fmt, ap);
}

void virtio_init_iov_from_pdu(V9fsPDU *pdu, struct iovec **piov,
                              unsigned int *pniov, bool is_write)
{
    V9fsState *s = pdu->s;
    V9fsVirtioState *v = container_of(s, V9fsVirtioState, state);
    VirtQueueElement *elem = v->elems[pdu->idx];

    if (is_write) {
        *piov = elem->out_sg;
        *pniov = elem->out_num;
    } else {
        *piov = elem->in_sg;
        *pniov = elem->in_num;
    }
}

/* virtio-9p device */

static Property virtio_9p_properties[] = {
    DEFINE_PROP_STRING("mount_tag", V9fsVirtioState, state.fsconf.tag),
    DEFINE_PROP_STRING("fsdev", V9fsVirtioState, state.fsconf.fsdev_id),
    DEFINE_PROP_END_OF_LIST(),
};

static void virtio_9p_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_CLASS(klass);

    dc->props = virtio_9p_properties;
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
    vdc->realize = virtio_9p_device_realize;
    vdc->unrealize = virtio_9p_device_unrealize;
    vdc->get_features = virtio_9p_get_features;
    vdc->get_config = virtio_9p_get_config;
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
