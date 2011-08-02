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
    FsTypeEntry *fse;

    s = (V9fsState *)virtio_common_init("virtio-9p",
                                    VIRTIO_ID_9P,
                                    sizeof(struct virtio_9p_config)+
                                    MAX_TAG_LEN,
                                    sizeof(V9fsState));
    /* initialize pdu allocator */
    QLIST_INIT(&s->free_list);
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

    if (!strcmp(fse->security_model, "passthrough")) {
        /* Files on the Fileserver set to client user credentials */
        s->ctx.fs_sm = SM_PASSTHROUGH;
        s->ctx.xops = passthrough_xattr_ops;
    } else if (!strcmp(fse->security_model, "mapped")) {
        /* Files on the fileserver are set to QEMU credentials.
         * Client user credentials are saved in extended attributes.
         */
        s->ctx.fs_sm = SM_MAPPED;
        s->ctx.xops = mapped_xattr_ops;
    } else if (!strcmp(fse->security_model, "none")) {
        /*
         * Files on the fileserver are set to QEMU credentials.
         */
        s->ctx.fs_sm = SM_NONE;
        s->ctx.xops = none_xattr_ops;
    } else {
        fprintf(stderr, "Default to security_model=none. You may want"
                " enable advanced security model using "
                "security option:\n\t security_model=passthrough\n\t "
                "security_model=mapped\n");
        s->ctx.fs_sm = SM_NONE;
        s->ctx.xops = none_xattr_ops;
    }

    if (lstat(fse->path, &stat)) {
        fprintf(stderr, "share path %s does not exist\n", fse->path);
        exit(1);
    } else if (!S_ISDIR(stat.st_mode)) {
        fprintf(stderr, "share path %s is not a directory\n", fse->path);
        exit(1);
    }

    s->ctx.fs_root = g_strdup(fse->path);
    len = strlen(conf->tag);
    if (len > MAX_TAG_LEN) {
        len = MAX_TAG_LEN;
    }
    /* s->tag is non-NULL terminated string */
    s->tag = g_malloc(len);
    memcpy(s->tag, conf->tag, len);
    s->tag_len = len;
    s->ctx.uid = -1;
    s->ctx.flags = 0;

    s->ops = fse->ops;
    s->vdev.get_features = virtio_9p_get_features;
    s->config_size = sizeof(struct virtio_9p_config) +
                        s->tag_len;
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
