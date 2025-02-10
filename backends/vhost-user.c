/*
 * QEMU vhost-user backend
 *
 * Copyright (C) 2018 Red Hat Inc
 *
 * Authors:
 *  Marc-André Lureau <marcandre.lureau@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */


#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qom/object_interfaces.h"
#include "system/vhost-user-backend.h"
#include "system/kvm.h"
#include "io/channel-command.h"
#include "hw/virtio/virtio-bus.h"

int
vhost_user_backend_dev_init(VhostUserBackend *b, VirtIODevice *vdev,
                            unsigned nvqs, Error **errp)
{
    int ret;

    assert(!b->vdev && vdev);

    if (!vhost_user_init(&b->vhost_user, &b->chr, errp)) {
        return -1;
    }

    b->vdev = vdev;
    b->dev.nvqs = nvqs;
    b->dev.vqs = g_new0(struct vhost_virtqueue, nvqs);

    ret = vhost_dev_init(&b->dev, &b->vhost_user, VHOST_BACKEND_TYPE_USER, 0,
                         errp);
    if (ret < 0) {
        return -1;
    }

    return 0;
}

void
vhost_user_backend_start(VhostUserBackend *b)
{
    BusState *qbus = BUS(qdev_get_parent_bus(DEVICE(b->vdev)));
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(qbus);
    int ret, i ;

    if (b->started) {
        return;
    }

    if (!k->set_guest_notifiers) {
        error_report("binding does not support guest notifiers");
        return;
    }

    ret = vhost_dev_enable_notifiers(&b->dev, b->vdev);
    if (ret < 0) {
        return;
    }

    ret = k->set_guest_notifiers(qbus->parent, b->dev.nvqs, true);
    if (ret < 0) {
        error_report("Error binding guest notifier");
        goto err_host_notifiers;
    }

    b->dev.acked_features = b->vdev->guest_features;
    ret = vhost_dev_start(&b->dev, b->vdev, true);
    if (ret < 0) {
        error_report("Error start vhost dev");
        goto err_guest_notifiers;
    }

    /* guest_notifier_mask/pending not used yet, so just unmask
     * everything here.  virtio-pci will do the right thing by
     * enabling/disabling irqfd.
     */
    for (i = 0; i < b->dev.nvqs; i++) {
        vhost_virtqueue_mask(&b->dev, b->vdev,
                             b->dev.vq_index + i, false);
    }

    b->started = true;
    return;

err_guest_notifiers:
    k->set_guest_notifiers(qbus->parent, b->dev.nvqs, false);
err_host_notifiers:
    vhost_dev_disable_notifiers(&b->dev, b->vdev);
}

void
vhost_user_backend_stop(VhostUserBackend *b)
{
    BusState *qbus = BUS(qdev_get_parent_bus(DEVICE(b->vdev)));
    VirtioBusClass *k = VIRTIO_BUS_GET_CLASS(qbus);
    int ret = 0;

    if (!b->started) {
        return;
    }

    vhost_dev_stop(&b->dev, b->vdev, true);

    if (k->set_guest_notifiers) {
        ret = k->set_guest_notifiers(qbus->parent,
                                     b->dev.nvqs, false);
        if (ret < 0) {
            error_report("vhost guest notifier cleanup failed: %d", ret);
        }
    }
    assert(ret >= 0);

    vhost_dev_disable_notifiers(&b->dev, b->vdev);
    b->started = false;
}

static void set_chardev(Object *obj, const char *value, Error **errp)
{
    VhostUserBackend *b = VHOST_USER_BACKEND(obj);
    Chardev *chr;

    if (b->completed) {
        error_setg(errp, "Property 'chardev' can no longer be set");
        return;
    }

    g_free(b->chr_name);
    b->chr_name = g_strdup(value);

    chr = qemu_chr_find(b->chr_name);
    if (chr == NULL) {
        error_set(errp, ERROR_CLASS_DEVICE_NOT_FOUND,
                  "Chardev '%s' not found", b->chr_name);
        return;
    }

    if (!qemu_chr_fe_init(&b->chr, chr, errp)) {
        return;
    }

    b->completed = true;
    /* could call vhost_dev_init() so early message can be exchanged */
}

static char *get_chardev(Object *obj, Error **errp)
{
    VhostUserBackend *b = VHOST_USER_BACKEND(obj);
    Chardev *chr = qemu_chr_fe_get_driver(&b->chr);

    if (chr && chr->label) {
        return g_strdup(chr->label);
    }

    return NULL;
}

static void vhost_user_backend_class_init(ObjectClass *oc, void *data)
{
    object_class_property_add_str(oc, "chardev", get_chardev, set_chardev);
}

static void vhost_user_backend_finalize(Object *obj)
{
    VhostUserBackend *b = VHOST_USER_BACKEND(obj);

    g_free(b->dev.vqs);
    g_free(b->chr_name);

    vhost_user_cleanup(&b->vhost_user);
    qemu_chr_fe_deinit(&b->chr, true);
}

static const TypeInfo vhost_user_backend_info = {
    .name = TYPE_VHOST_USER_BACKEND,
    .parent = TYPE_OBJECT,
    .instance_size = sizeof(VhostUserBackend),
    .class_init = vhost_user_backend_class_init,
    .instance_finalize = vhost_user_backend_finalize,
};

static void register_types(void)
{
    type_register_static(&vhost_user_backend_info);
}

type_init(register_types);
