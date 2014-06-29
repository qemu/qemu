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

#include "hw/virtio/virtio.h"
#include "hw/virtio/virtio-9p.h"
#include "hw/i386/pc.h"
#include "qemu/sockets.h"
#include "virtio-9p.h"
#include "fsdev/qemu-fsdev.h"
#include "virtio-9p-xattr.h"
#include "virtio-9p-coth.h"
#include "hw/virtio/virtio-access.h"

static uint32_t virtio_9p_get_features(VirtIODevice *vdev, uint32_t features)
{
    features |= 1 << VIRTIO_9P_MOUNT_TAG;
    return features;
}

static void virtio_9p_get_config(VirtIODevice *vdev, uint8_t *config)
{
    int len;
    struct virtio_9p_config *cfg;
    V9fsState *s = VIRTIO_9P(vdev);

    len = strlen(s->tag);
    cfg = g_malloc0(sizeof(struct virtio_9p_config) + len);
    virtio_stw_p(vdev, &cfg->tag_len, len);
    /* We don't copy the terminating null to config space */
    memcpy(cfg->tag, s->tag, len);
    memcpy(config, cfg, s->config_size);
    g_free(cfg);
}

static void virtio_9p_device_realize(DeviceState *dev, Error **errp)
{
    VirtIODevice *vdev = VIRTIO_DEVICE(dev);
    V9fsState *s = VIRTIO_9P(dev);
    int i, len;
    struct stat stat;
    FsDriverEntry *fse;
    V9fsPath path;

    virtio_init(vdev, "virtio-9p", VIRTIO_ID_9P,
                sizeof(struct virtio_9p_config) + MAX_TAG_LEN);

    /* initialize pdu allocator */
    QLIST_INIT(&s->free_list);
    QLIST_INIT(&s->active_list);
    for (i = 0; i < (MAX_REQ - 1); i++) {
        QLIST_INSERT_HEAD(&s->free_list, &s->pdus[i], next);
    }

    s->vq = virtio_add_queue(vdev, MAX_REQ, handle_9p_output);

    v9fs_path_init(&path);

    fse = get_fsdev_fsentry(s->fsconf.fsdev_id);

    if (!fse) {
        /* We don't have a fsdev identified by fsdev_id */
        error_setg(errp, "Virtio-9p device couldn't find fsdev with the "
                   "id = %s",
                   s->fsconf.fsdev_id ? s->fsconf.fsdev_id : "NULL");
        goto out;
    }

    if (!s->fsconf.tag) {
        /* we haven't specified a mount_tag */
        error_setg(errp, "fsdev with id %s needs mount_tag arguments",
                   s->fsconf.fsdev_id);
        goto out;
    }

    s->ctx.export_flags = fse->export_flags;
    s->ctx.fs_root = g_strdup(fse->path);
    s->ctx.exops.get_st_gen = NULL;
    len = strlen(s->fsconf.tag);
    if (len > MAX_TAG_LEN - 1) {
        error_setg(errp, "mount tag '%s' (%d bytes) is longer than "
                   "maximum (%d bytes)", s->fsconf.tag, len, MAX_TAG_LEN - 1);
        goto out;
    }

    s->tag = g_strdup(s->fsconf.tag);
    s->ctx.uid = -1;

    s->ops = fse->ops;
    s->config_size = sizeof(struct virtio_9p_config) + len;
    s->fid_list = NULL;
    qemu_co_rwlock_init(&s->rename_lock);

    if (s->ops->init(&s->ctx) < 0) {
        error_setg(errp, "Virtio-9p Failed to initialize fs-driver with id:%s"
                   " and export path:%s", s->fsconf.fsdev_id, s->ctx.fs_root);
        goto out;
    }
    if (v9fs_init_worker_threads() < 0) {
        error_setg(errp, "worker thread initialization failed");
        goto out;
    }

    /*
     * Check details of export path, We need to use fs driver
     * call back to do that. Since we are in the init path, we don't
     * use co-routines here.
     */
    if (s->ops->name_to_path(&s->ctx, NULL, "/", &path) < 0) {
        error_setg(errp,
                   "error in converting name to path %s", strerror(errno));
        goto out;
    }
    if (s->ops->lstat(&s->ctx, &path, &stat)) {
        error_setg(errp, "share path %s does not exist", fse->path);
        goto out;
    } else if (!S_ISDIR(stat.st_mode)) {
        error_setg(errp, "share path %s is not a directory", fse->path);
        goto out;
    }
    v9fs_path_free(&path);

    return;
out:
    g_free(s->ctx.fs_root);
    g_free(s->tag);
    virtio_cleanup(vdev);
    v9fs_path_free(&path);
}

/* virtio-9p device */

static Property virtio_9p_properties[] = {
    DEFINE_VIRTIO_9P_PROPERTIES(V9fsState, fsconf),
    DEFINE_PROP_END_OF_LIST(),
};

static void virtio_9p_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioDeviceClass *vdc = VIRTIO_DEVICE_CLASS(klass);

    dc->props = virtio_9p_properties;
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
    vdc->realize = virtio_9p_device_realize;
    vdc->get_features = virtio_9p_get_features;
    vdc->get_config = virtio_9p_get_config;
}

static const TypeInfo virtio_device_info = {
    .name = TYPE_VIRTIO_9P,
    .parent = TYPE_VIRTIO_DEVICE,
    .instance_size = sizeof(V9fsState),
    .class_init = virtio_9p_class_init,
};

static void virtio_9p_register_types(void)
{
    type_register_static(&virtio_device_info);
}

type_init(virtio_9p_register_types)
