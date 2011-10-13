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

#include "hw/virtio.h"
#include "hw/pc.h"
#include "qemu_socket.h"
#include "hw/virtio-pci.h"
#include "virtio-9p.h"
#include "fsdev/qemu-fsdev.h"
#include "virtio-9p-xattr.h"
#include "virtio-9p-coth.h"

static uint32_t virtio_9p_get_features(VirtIODevice *vdev, uint32_t features)
{
    features |= 1 << VIRTIO_9P_MOUNT_TAG;
    return features;
}

static V9fsState *to_virtio_9p(VirtIODevice *vdev)
{
    return (V9fsState *)vdev;
}

static void virtio_9p_get_config(VirtIODevice *vdev, uint8_t *config)
{
    struct virtio_9p_config *cfg;
    V9fsState *s = to_virtio_9p(vdev);

    cfg = g_malloc0(sizeof(struct virtio_9p_config) +
                        s->tag_len);
    stw_raw(&cfg->tag_len, s->tag_len);
    memcpy(cfg->tag, s->tag, s->tag_len);
    memcpy(config, cfg, s->config_size);
    g_free(cfg);
}

VirtIODevice *virtio_9p_init(DeviceState *dev, V9fsConf *conf)
{
    V9fsState *s;
    int i, len;
    struct stat stat;
    FsDriverEntry *fse;
    V9fsPath path;

    s = (V9fsState *)virtio_common_init("virtio-9p",
                                    VIRTIO_ID_9P,
                                    sizeof(struct virtio_9p_config)+
                                    MAX_TAG_LEN,
                                    sizeof(V9fsState));
    /* initialize pdu allocator */
    QLIST_INIT(&s->free_list);
    QLIST_INIT(&s->active_list);
    for (i = 0; i < (MAX_REQ - 1); i++) {
        QLIST_INSERT_HEAD(&s->free_list, &s->pdus[i], next);
    }

    s->vq = virtio_add_queue(&s->vdev, MAX_REQ, handle_9p_output);

    fse = get_fsdev_fsentry(conf->fsdev_id);

    if (!fse) {
        /* We don't have a fsdev identified by fsdev_id */
        fprintf(stderr, "Virtio-9p device couldn't find fsdev with the "
                "id = %s\n", conf->fsdev_id ? conf->fsdev_id : "NULL");
        exit(1);
    }

    if (!fse->path || !conf->tag) {
        /* we haven't specified a mount_tag or the path */
        fprintf(stderr, "fsdev with id %s needs path "
                "and Virtio-9p device needs mount_tag arguments\n",
                conf->fsdev_id);
        exit(1);
    }

    s->ctx.export_flags = fse->export_flags;
    s->ctx.fs_root = g_strdup(fse->path);
    s->ctx.exops.get_st_gen = NULL;

    if (fse->export_flags & V9FS_SM_PASSTHROUGH) {
        s->ctx.xops = passthrough_xattr_ops;
    } else if (fse->export_flags & V9FS_SM_MAPPED) {
        s->ctx.xops = mapped_xattr_ops;
    } else if (fse->export_flags & V9FS_SM_NONE) {
        s->ctx.xops = none_xattr_ops;
    }

    len = strlen(conf->tag);
    if (len > MAX_TAG_LEN) {
        fprintf(stderr, "mount tag '%s' (%d bytes) is longer than "
                "maximum (%d bytes)", conf->tag, len, MAX_TAG_LEN);
        exit(1);
    }
    /* s->tag is non-NULL terminated string */
    s->tag = g_malloc(len);
    memcpy(s->tag, conf->tag, len);
    s->tag_len = len;
    s->ctx.uid = -1;

    s->ops = fse->ops;
    s->vdev.get_features = virtio_9p_get_features;
    s->config_size = sizeof(struct virtio_9p_config) + s->tag_len;
    s->vdev.get_config = virtio_9p_get_config;
    s->fid_list = NULL;
    qemu_co_rwlock_init(&s->rename_lock);

    if (s->ops->init(&s->ctx) < 0) {
        fprintf(stderr, "Virtio-9p Failed to initialize fs-driver with id:%s"
                " and export path:%s\n", conf->fsdev_id, s->ctx.fs_root);
        exit(1);
    }
    if (v9fs_init_worker_threads() < 0) {
        fprintf(stderr, "worker thread initialization failed\n");
        exit(1);
    }

    /*
     * Check details of export path, We need to use fs driver
     * call back to do that. Since we are in the init path, we don't
     * use co-routines here.
     */
    v9fs_path_init(&path);
    if (s->ops->name_to_path(&s->ctx, NULL, "/", &path) < 0) {
        fprintf(stderr,
                "error in converting name to path %s", strerror(errno));
        exit(1);
    }
    if (s->ops->lstat(&s->ctx, &path, &stat)) {
        fprintf(stderr, "share path %s does not exist\n", fse->path);
        exit(1);
    } else if (!S_ISDIR(stat.st_mode)) {
        fprintf(stderr, "share path %s is not a directory\n", fse->path);
        exit(1);
    }
    v9fs_path_free(&path);

    return &s->vdev;
}

static int virtio_9p_init_pci(PCIDevice *pci_dev)
{
    VirtIOPCIProxy *proxy = DO_UPCAST(VirtIOPCIProxy, pci_dev, pci_dev);
    VirtIODevice *vdev;

    vdev = virtio_9p_init(&pci_dev->qdev, &proxy->fsconf);
    vdev->nvectors = proxy->nvectors;
    virtio_init_pci(proxy, vdev);
    /* make the actual value visible */
    proxy->nvectors = vdev->nvectors;
    return 0;
}

static PCIDeviceInfo virtio_9p_info = {
    .qdev.name = "virtio-9p-pci",
    .qdev.size = sizeof(VirtIOPCIProxy),
    .init      = virtio_9p_init_pci,
    .vendor_id = PCI_VENDOR_ID_REDHAT_QUMRANET,
    .device_id = 0x1009,
    .revision  = VIRTIO_PCI_ABI_VERSION,
    .class_id  = 0x2,
    .qdev.props = (Property[]) {
        DEFINE_PROP_BIT("ioeventfd", VirtIOPCIProxy, flags,
                        VIRTIO_PCI_FLAG_USE_IOEVENTFD_BIT, true),
        DEFINE_PROP_UINT32("vectors", VirtIOPCIProxy, nvectors, 2),
        DEFINE_VIRTIO_COMMON_FEATURES(VirtIOPCIProxy, host_features),
        DEFINE_PROP_STRING("mount_tag", VirtIOPCIProxy, fsconf.tag),
        DEFINE_PROP_STRING("fsdev", VirtIOPCIProxy, fsconf.fsdev_id),
        DEFINE_PROP_END_OF_LIST(),
    }
};

static void virtio_9p_register_devices(void)
{
    pci_qdev_register(&virtio_9p_info);
    virtio_9p_set_fd_limit();
}

device_init(virtio_9p_register_devices)
