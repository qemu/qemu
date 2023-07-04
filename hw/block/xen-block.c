/*
 * Copyright (c) 2018  Citrix Systems Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "qemu/main-loop.h"
#include "qemu/module.h"
#include "qemu/option.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-block-core.h"
#include "qapi/qapi-commands-qom.h"
#include "qapi/qapi-visit-block-core.h"
#include "qapi/qobject-input-visitor.h"
#include "qapi/visitor.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qstring.h"
#include "qom/object_interfaces.h"
#include "hw/block/xen_blkif.h"
#include "hw/qdev-properties.h"
#include "hw/xen/xen-block.h"
#include "hw/xen/xen-backend.h"
#include "sysemu/blockdev.h"
#include "sysemu/block-backend.h"
#include "sysemu/iothread.h"
#include "dataplane/xen-block.h"
#include "trace.h"

static char *xen_block_get_name(XenDevice *xendev, Error **errp)
{
    XenBlockDevice *blockdev = XEN_BLOCK_DEVICE(xendev);
    XenBlockVdev *vdev = &blockdev->props.vdev;

    return g_strdup_printf("%lu", vdev->number);
}

static void xen_block_disconnect(XenDevice *xendev, Error **errp)
{
    XenBlockDevice *blockdev = XEN_BLOCK_DEVICE(xendev);
    const char *type = object_get_typename(OBJECT(blockdev));
    XenBlockVdev *vdev = &blockdev->props.vdev;

    trace_xen_block_disconnect(type, vdev->disk, vdev->partition);

    xen_block_dataplane_stop(blockdev->dataplane);
}

static void xen_block_connect(XenDevice *xendev, Error **errp)
{
    XenBlockDevice *blockdev = XEN_BLOCK_DEVICE(xendev);
    const char *type = object_get_typename(OBJECT(blockdev));
    XenBlockVdev *vdev = &blockdev->props.vdev;
    BlockConf *conf = &blockdev->props.conf;
    unsigned int feature_large_sector_size;
    unsigned int order, nr_ring_ref, *ring_ref, event_channel, protocol;
    char *str;

    trace_xen_block_connect(type, vdev->disk, vdev->partition);

    if (xen_device_frontend_scanf(xendev, "feature-large-sector-size", "%u",
                                  &feature_large_sector_size) != 1) {
        feature_large_sector_size = 0;
    }

    if (feature_large_sector_size != 1 &&
        conf->logical_block_size != XEN_BLKIF_SECTOR_SIZE) {
        error_setg(errp, "logical_block_size != %u not supported by frontend",
                   XEN_BLKIF_SECTOR_SIZE);
        return;
    }

    if (xen_device_frontend_scanf(xendev, "ring-page-order", "%u",
                                  &order) != 1) {
        nr_ring_ref = 1;
        ring_ref = g_new(unsigned int, nr_ring_ref);

        if (xen_device_frontend_scanf(xendev, "ring-ref", "%u",
                                      &ring_ref[0]) != 1) {
            error_setg(errp, "failed to read ring-ref");
            g_free(ring_ref);
            return;
        }
    } else if (qemu_xen_gnttab_can_map_multi() &&
               order <= blockdev->props.max_ring_page_order) {
        unsigned int i;

        nr_ring_ref = 1 << order;
        ring_ref = g_new(unsigned int, nr_ring_ref);

        for (i = 0; i < nr_ring_ref; i++) {
            const char *key = g_strdup_printf("ring-ref%u", i);

            if (xen_device_frontend_scanf(xendev, key, "%u",
                                          &ring_ref[i]) != 1) {
                error_setg(errp, "failed to read %s", key);
                g_free((gpointer)key);
                g_free(ring_ref);
                return;
            }

            g_free((gpointer)key);
        }
    } else {
        error_setg(errp, "invalid ring-page-order (%d)", order);
        return;
    }

    if (xen_device_frontend_scanf(xendev, "event-channel", "%u",
                                  &event_channel) != 1) {
        error_setg(errp, "failed to read event-channel");
        g_free(ring_ref);
        return;
    }

    if (xen_device_frontend_scanf(xendev, "protocol", "%ms",
                                  &str) != 1) {
        protocol = BLKIF_PROTOCOL_NATIVE;
    } else {
        if (strcmp(str, XEN_IO_PROTO_ABI_X86_32) == 0) {
            protocol = BLKIF_PROTOCOL_X86_32;
        } else if (strcmp(str, XEN_IO_PROTO_ABI_X86_64) == 0) {
            protocol = BLKIF_PROTOCOL_X86_64;
        } else {
            protocol = BLKIF_PROTOCOL_NATIVE;
        }

        free(str);
    }

    xen_block_dataplane_start(blockdev->dataplane, ring_ref, nr_ring_ref,
                              event_channel, protocol, errp);

    g_free(ring_ref);
}

static void xen_block_unrealize(XenDevice *xendev)
{
    XenBlockDevice *blockdev = XEN_BLOCK_DEVICE(xendev);
    XenBlockDeviceClass *blockdev_class =
        XEN_BLOCK_DEVICE_GET_CLASS(xendev);
    const char *type = object_get_typename(OBJECT(blockdev));
    XenBlockVdev *vdev = &blockdev->props.vdev;

    if (vdev->type == XEN_BLOCK_VDEV_TYPE_INVALID) {
        return;
    }

    trace_xen_block_unrealize(type, vdev->disk, vdev->partition);

    /* Disconnect from the frontend in case this has not already happened */
    xen_block_disconnect(xendev, NULL);

    xen_block_dataplane_destroy(blockdev->dataplane);
    blockdev->dataplane = NULL;

    if (blockdev_class->unrealize) {
        blockdev_class->unrealize(blockdev);
    }
}

static void xen_block_set_size(XenBlockDevice *blockdev)
{
    const char *type = object_get_typename(OBJECT(blockdev));
    XenBlockVdev *vdev = &blockdev->props.vdev;
    BlockConf *conf = &blockdev->props.conf;
    int64_t sectors = blk_getlength(conf->blk) / conf->logical_block_size;
    XenDevice *xendev = XEN_DEVICE(blockdev);

    trace_xen_block_size(type, vdev->disk, vdev->partition, sectors);

    xen_device_backend_printf(xendev, "sectors", "%"PRIi64, sectors);
}

static void xen_block_resize_cb(void *opaque)
{
    XenBlockDevice *blockdev = opaque;
    XenDevice *xendev = XEN_DEVICE(blockdev);
    enum xenbus_state state = xen_device_backend_get_state(xendev);

    xen_block_set_size(blockdev);

    /*
     * Mimic the behaviour of Linux xen-blkback and re-write the state
     * to trigger the frontend watch.
     */
    xen_device_backend_printf(xendev, "state", "%u", state);
}

static const BlockDevOps xen_block_dev_ops = {
    .resize_cb = xen_block_resize_cb,
};

static void xen_block_realize(XenDevice *xendev, Error **errp)
{
    ERRP_GUARD();
    XenBlockDevice *blockdev = XEN_BLOCK_DEVICE(xendev);
    XenBlockDeviceClass *blockdev_class =
        XEN_BLOCK_DEVICE_GET_CLASS(xendev);
    const char *type = object_get_typename(OBJECT(blockdev));
    XenBlockVdev *vdev = &blockdev->props.vdev;
    BlockConf *conf = &blockdev->props.conf;
    BlockBackend *blk = conf->blk;

    if (vdev->type == XEN_BLOCK_VDEV_TYPE_INVALID) {
        error_setg(errp, "vdev property not set");
        return;
    }

    trace_xen_block_realize(type, vdev->disk, vdev->partition);

    if (blockdev_class->realize) {
        blockdev_class->realize(blockdev, errp);
        if (*errp) {
            return;
        }
    }

    /*
     * The blkif protocol does not deal with removable media, so it must
     * always be present, even for CDRom devices.
     */
    assert(blk);
    if (!blk_is_inserted(blk)) {
        error_setg(errp, "device needs media, but drive is empty");
        return;
    }

    if (!blkconf_apply_backend_options(conf, blockdev->info & VDISK_READONLY,
                                       true, errp)) {
        return;
    }

    if (!(blockdev->info & VDISK_CDROM) &&
        !blkconf_geometry(conf, NULL, 65535, 255, 255, errp)) {
        return;
    }

    if (!blkconf_blocksizes(conf, errp)) {
        return;
    }

    blk_set_dev_ops(blk, &xen_block_dev_ops, blockdev);

    if (conf->discard_granularity == -1) {
        conf->discard_granularity = conf->physical_block_size;
    }

    if (blk_get_flags(blk) & BDRV_O_UNMAP) {
        xen_device_backend_printf(xendev, "feature-discard", "%u", 1);
        xen_device_backend_printf(xendev, "discard-granularity", "%u",
                                  conf->discard_granularity);
        xen_device_backend_printf(xendev, "discard-alignment", "%u", 0);
    }

    xen_device_backend_printf(xendev, "feature-flush-cache", "%u", 1);

    if (qemu_xen_gnttab_can_map_multi()) {
        xen_device_backend_printf(xendev, "max-ring-page-order", "%u",
                                  blockdev->props.max_ring_page_order);
    }

    xen_device_backend_printf(xendev, "info", "%u", blockdev->info);

    xen_device_frontend_printf(xendev, "virtual-device", "%lu",
                               vdev->number);
    xen_device_frontend_printf(xendev, "device-type", "%s",
                               blockdev->device_type);

    xen_device_backend_printf(xendev, "sector-size", "%u",
                              conf->logical_block_size);

    xen_block_set_size(blockdev);

    blockdev->dataplane =
        xen_block_dataplane_create(xendev, blk, conf->logical_block_size,
                                   blockdev->props.iothread);
}

static void xen_block_frontend_changed(XenDevice *xendev,
                                       enum xenbus_state frontend_state,
                                       Error **errp)
{
    ERRP_GUARD();
    enum xenbus_state backend_state = xen_device_backend_get_state(xendev);

    switch (frontend_state) {
    case XenbusStateInitialised:
    case XenbusStateConnected:
        if (backend_state == XenbusStateConnected) {
            break;
        }

        xen_block_disconnect(xendev, errp);
        if (*errp) {
            break;
        }

        xen_block_connect(xendev, errp);
        if (*errp) {
            break;
        }

        xen_device_backend_set_state(xendev, XenbusStateConnected);
        break;

    case XenbusStateClosing:
        xen_device_backend_set_state(xendev, XenbusStateClosing);
        break;

    case XenbusStateClosed:
    case XenbusStateUnknown:
        xen_block_disconnect(xendev, errp);
        if (*errp) {
            break;
        }

        xen_device_backend_set_state(xendev, XenbusStateClosed);
        break;

    default:
        break;
    }
}

static char *disk_to_vbd_name(unsigned int disk)
{
    char *name, *prefix = (disk >= 26) ?
        disk_to_vbd_name((disk / 26) - 1) : g_strdup("");

    name = g_strdup_printf("%s%c", prefix, 'a' + disk % 26);
    g_free(prefix);

    return name;
}

static void xen_block_get_vdev(Object *obj, Visitor *v, const char *name,
                               void *opaque, Error **errp)
{
    Property *prop = opaque;
    XenBlockVdev *vdev = object_field_prop_ptr(obj, prop);
    char *str;

    switch (vdev->type) {
    case XEN_BLOCK_VDEV_TYPE_DP:
        str = g_strdup_printf("d%lup%lu", vdev->disk, vdev->partition);
        break;

    case XEN_BLOCK_VDEV_TYPE_XVD:
    case XEN_BLOCK_VDEV_TYPE_HD:
    case XEN_BLOCK_VDEV_TYPE_SD: {
        char *name = disk_to_vbd_name(vdev->disk);

        str = g_strdup_printf("%s%s%lu",
                              (vdev->type == XEN_BLOCK_VDEV_TYPE_XVD) ?
                              "xvd" :
                              (vdev->type == XEN_BLOCK_VDEV_TYPE_HD) ?
                              "hd" :
                              "sd",
                              name, vdev->partition);
        g_free(name);
        break;
    }
    default:
        error_setg(errp, "invalid vdev type");
        return;
    }

    visit_type_str(v, name, &str, errp);
    g_free(str);
}

static int vbd_name_to_disk(const char *name, const char **endp,
                            unsigned long *disk)
{
    unsigned int n = 0;

    while (*name != '\0') {
        if (!g_ascii_isalpha(*name) || !g_ascii_islower(*name)) {
            break;
        }

        n *= 26;
        n += *name++ - 'a' + 1;
    }
    *endp = name;

    if (!n) {
        return -1;
    }

    *disk = n - 1;

    return 0;
}

static void xen_block_set_vdev(Object *obj, Visitor *v, const char *name,
                               void *opaque, Error **errp)
{
    Property *prop = opaque;
    XenBlockVdev *vdev = object_field_prop_ptr(obj, prop);
    char *str, *p;
    const char *end;

    if (!visit_type_str(v, name, &str, errp)) {
        return;
    }

    p = strchr(str, 'd');
    if (!p) {
        goto invalid;
    }

    *p++ = '\0';
    if (*str == '\0') {
        vdev->type = XEN_BLOCK_VDEV_TYPE_DP;
    } else if (strcmp(str, "xv") == 0) {
        vdev->type = XEN_BLOCK_VDEV_TYPE_XVD;
    } else if (strcmp(str, "h") == 0) {
        vdev->type = XEN_BLOCK_VDEV_TYPE_HD;
    } else if (strcmp(str, "s") == 0) {
        vdev->type = XEN_BLOCK_VDEV_TYPE_SD;
    } else {
        goto invalid;
    }

    if (vdev->type == XEN_BLOCK_VDEV_TYPE_DP) {
        if (qemu_strtoul(p, &end, 10, &vdev->disk)) {
            goto invalid;
        }

        if (*end == 'p') {
            if (*(++end) == '\0') {
                goto invalid;
            }
        }
    } else {
        if (vbd_name_to_disk(p, &end, &vdev->disk)) {
            goto invalid;
        }
    }

    if (*end != '\0') {
        p = (char *)end;

        if (qemu_strtoul(p, &end, 10, &vdev->partition)) {
            goto invalid;
        }

        if (*end != '\0') {
            goto invalid;
        }
    } else {
        vdev->partition = 0;
    }

    switch (vdev->type) {
    case XEN_BLOCK_VDEV_TYPE_DP:
    case XEN_BLOCK_VDEV_TYPE_XVD:
        if (vdev->disk < (1 << 4) && vdev->partition < (1 << 4)) {
            vdev->number = (202 << 8) | (vdev->disk << 4) |
                vdev->partition;
        } else if (vdev->disk < (1 << 20) && vdev->partition < (1 << 8)) {
            vdev->number = (1 << 28) | (vdev->disk << 8) |
                vdev->partition;
        } else {
            goto invalid;
        }
        break;

    case XEN_BLOCK_VDEV_TYPE_HD:
        if ((vdev->disk == 0 || vdev->disk == 1) &&
            vdev->partition < (1 << 6)) {
            vdev->number = (3 << 8) | (vdev->disk << 6) | vdev->partition;
        } else if ((vdev->disk == 2 || vdev->disk == 3) &&
                   vdev->partition < (1 << 6)) {
            vdev->number = (22 << 8) | ((vdev->disk - 2) << 6) |
                vdev->partition;
        } else {
            goto invalid;
        }
        break;

    case XEN_BLOCK_VDEV_TYPE_SD:
        if (vdev->disk < (1 << 4) && vdev->partition < (1 << 4)) {
            vdev->number = (8 << 8) | (vdev->disk << 4) | vdev->partition;
        } else {
            goto invalid;
        }
        break;

    default:
        goto invalid;
    }

    g_free(str);
    return;

invalid:
    error_setg(errp, "invalid virtual disk specifier");

    vdev->type = XEN_BLOCK_VDEV_TYPE_INVALID;
    g_free(str);
}

/*
 * This property deals with 'vdev' names adhering to the Xen VBD naming
 * scheme described in:
 *
 * https://xenbits.xen.org/docs/unstable/man/xen-vbd-interface.7.html
 */
const PropertyInfo xen_block_prop_vdev = {
    .name  = "str",
    .description = "Virtual Disk specifier: d*p*/xvd*/hd*/sd*",
    .get = xen_block_get_vdev,
    .set = xen_block_set_vdev,
};

static Property xen_block_props[] = {
    DEFINE_PROP("vdev", XenBlockDevice, props.vdev,
                xen_block_prop_vdev, XenBlockVdev),
    DEFINE_BLOCK_PROPERTIES(XenBlockDevice, props.conf),
    DEFINE_PROP_UINT32("max-ring-page-order", XenBlockDevice,
                       props.max_ring_page_order, 4),
    DEFINE_PROP_LINK("iothread", XenBlockDevice, props.iothread,
                     TYPE_IOTHREAD, IOThread *),
    DEFINE_PROP_END_OF_LIST()
};

static void xen_block_class_init(ObjectClass *class, void *data)
{
    DeviceClass *dev_class = DEVICE_CLASS(class);
    XenDeviceClass *xendev_class = XEN_DEVICE_CLASS(class);

    xendev_class->backend = "qdisk";
    xendev_class->device = "vbd";
    xendev_class->get_name = xen_block_get_name;
    xendev_class->realize = xen_block_realize;
    xendev_class->frontend_changed = xen_block_frontend_changed;
    xendev_class->unrealize = xen_block_unrealize;

    device_class_set_props(dev_class, xen_block_props);
}

static const TypeInfo xen_block_type_info = {
    .name = TYPE_XEN_BLOCK_DEVICE,
    .parent = TYPE_XEN_DEVICE,
    .instance_size = sizeof(XenBlockDevice),
    .abstract = true,
    .class_size = sizeof(XenBlockDeviceClass),
    .class_init = xen_block_class_init,
};

static void xen_disk_unrealize(XenBlockDevice *blockdev)
{
    trace_xen_disk_unrealize();
}

static void xen_disk_realize(XenBlockDevice *blockdev, Error **errp)
{
    BlockConf *conf = &blockdev->props.conf;

    trace_xen_disk_realize();

    blockdev->device_type = "disk";

    if (!conf->blk) {
        error_setg(errp, "drive property not set");
        return;
    }

    blockdev->info = blk_supports_write_perm(conf->blk) ? 0 : VDISK_READONLY;
}

static void xen_disk_class_init(ObjectClass *class, void *data)
{
    DeviceClass *dev_class = DEVICE_CLASS(class);
    XenBlockDeviceClass *blockdev_class = XEN_BLOCK_DEVICE_CLASS(class);

    blockdev_class->realize = xen_disk_realize;
    blockdev_class->unrealize = xen_disk_unrealize;

    dev_class->desc = "Xen Disk Device";
}

static const TypeInfo xen_disk_type_info = {
    .name = TYPE_XEN_DISK_DEVICE,
    .parent = TYPE_XEN_BLOCK_DEVICE,
    .instance_size = sizeof(XenDiskDevice),
    .class_init = xen_disk_class_init,
};

static void xen_cdrom_unrealize(XenBlockDevice *blockdev)
{
    trace_xen_cdrom_unrealize();
}

static void xen_cdrom_realize(XenBlockDevice *blockdev, Error **errp)
{
    BlockConf *conf = &blockdev->props.conf;

    trace_xen_cdrom_realize();

    blockdev->device_type = "cdrom";

    if (!conf->blk) {
        int rc;

        /* Set up an empty drive */
        conf->blk = blk_new(qemu_get_aio_context(), 0, BLK_PERM_ALL);

        rc = blk_attach_dev(conf->blk, DEVICE(blockdev));
        if (!rc) {
            error_setg_errno(errp, -rc, "failed to create drive");
            return;
        }
    }

    blockdev->info = VDISK_READONLY | VDISK_CDROM;
}

static void xen_cdrom_class_init(ObjectClass *class, void *data)
{
    DeviceClass *dev_class = DEVICE_CLASS(class);
    XenBlockDeviceClass *blockdev_class = XEN_BLOCK_DEVICE_CLASS(class);

    blockdev_class->realize = xen_cdrom_realize;
    blockdev_class->unrealize = xen_cdrom_unrealize;

    dev_class->desc = "Xen CD-ROM Device";
}

static const TypeInfo xen_cdrom_type_info = {
    .name = TYPE_XEN_CDROM_DEVICE,
    .parent = TYPE_XEN_BLOCK_DEVICE,
    .instance_size = sizeof(XenCDRomDevice),
    .class_init = xen_cdrom_class_init,
};

static void xen_block_register_types(void)
{
    type_register_static(&xen_block_type_info);
    type_register_static(&xen_disk_type_info);
    type_register_static(&xen_cdrom_type_info);
}

type_init(xen_block_register_types)

static void xen_block_blockdev_del(const char *node_name, Error **errp)
{
    trace_xen_block_blockdev_del(node_name);

    qmp_blockdev_del(node_name, errp);
}

static char *xen_block_blockdev_add(const char *id, QDict *qdict,
                                    Error **errp)
{
    ERRP_GUARD();
    const char *driver = qdict_get_try_str(qdict, "driver");
    BlockdevOptions *options = NULL;
    char *node_name;
    Visitor *v;

    if (!driver) {
        error_setg(errp, "no 'driver' parameter");
        return NULL;
    }

    node_name = g_strdup_printf("%s-%s", id, driver);
    qdict_put_str(qdict, "node-name", node_name);

    trace_xen_block_blockdev_add(node_name);

    v = qobject_input_visitor_new(QOBJECT(qdict));
    visit_type_BlockdevOptions(v, NULL, &options, errp);
    visit_free(v);
    if (!options) {
        goto fail;
    }

    qmp_blockdev_add(options, errp);

    if (*errp) {
        goto fail;
    }

    qapi_free_BlockdevOptions(options);

    return node_name;

fail:
    if (options) {
        qapi_free_BlockdevOptions(options);
    }
    g_free(node_name);

    return NULL;
}

static void xen_block_drive_destroy(XenBlockDrive *drive, Error **errp)
{
    ERRP_GUARD();
    char *node_name = drive->node_name;

    if (node_name) {
        xen_block_blockdev_del(node_name, errp);
        if (*errp) {
            return;
        }
        g_free(node_name);
        drive->node_name = NULL;
    }
    g_free(drive->id);
    g_free(drive);
}

static XenBlockDrive *xen_block_drive_create(const char *id,
                                             const char *device_type,
                                             QDict *opts, Error **errp)
{
    ERRP_GUARD();
    const char *params = qdict_get_try_str(opts, "params");
    const char *mode = qdict_get_try_str(opts, "mode");
    const char *direct_io_safe = qdict_get_try_str(opts, "direct-io-safe");
    const char *discard_enable = qdict_get_try_str(opts, "discard-enable");
    char *driver = NULL;
    char *filename = NULL;
    XenBlockDrive *drive = NULL;
    QDict *file_layer;
    QDict *driver_layer;
    struct stat st;
    int rc;

    if (params) {
        char **v = g_strsplit(params, ":", 2);

        if (v[1] == NULL) {
            filename = g_strdup(v[0]);
            driver = g_strdup("raw");
        } else {
            if (strcmp(v[0], "aio") == 0) {
                driver = g_strdup("raw");
            } else if (strcmp(v[0], "vhd") == 0) {
                driver = g_strdup("vpc");
            } else {
                driver = g_strdup(v[0]);
            }
            filename = g_strdup(v[1]);
        }

        g_strfreev(v);
    } else {
        error_setg(errp, "no params");
        goto done;
    }

    assert(filename);
    assert(driver);

    drive = g_new0(XenBlockDrive, 1);
    drive->id = g_strdup(id);

    rc = stat(filename, &st);
    if (rc) {
        error_setg_errno(errp, errno, "Could not stat file '%s'", filename);
        goto done;
    }

    file_layer = qdict_new();
    driver_layer = qdict_new();

    if (S_ISBLK(st.st_mode)) {
        qdict_put_str(file_layer, "driver", "host_device");
    } else {
        qdict_put_str(file_layer, "driver", "file");
    }

    qdict_put_str(file_layer, "filename", filename);

    if (mode && *mode != 'w') {
        qdict_put_bool(file_layer, "read-only", true);
    }

    if (direct_io_safe) {
        unsigned long value;

        if (!qemu_strtoul(direct_io_safe, NULL, 2, &value) && !!value) {
            QDict *cache_qdict = qdict_new();

            qdict_put_bool(cache_qdict, "direct", true);
            qdict_put(file_layer, "cache", cache_qdict);

            qdict_put_str(file_layer, "aio", "native");
        }
    }

    if (discard_enable) {
        unsigned long value;

        if (!qemu_strtoul(discard_enable, NULL, 2, &value) && !!value) {
            qdict_put_str(file_layer, "discard", "unmap");
            qdict_put_str(driver_layer, "discard", "unmap");
        }
    }

    /*
     * It is necessary to turn file locking off as an emulated device
     * may have already opened the same image file.
     */
    qdict_put_str(file_layer, "locking", "off");

    qdict_put_str(driver_layer, "driver", driver);

    qdict_put(driver_layer, "file", file_layer);

    g_assert(!drive->node_name);
    drive->node_name = xen_block_blockdev_add(drive->id, driver_layer,
                                              errp);

    qobject_unref(driver_layer);

done:
    g_free(filename);
    g_free(driver);
    if (*errp) {
        xen_block_drive_destroy(drive, NULL);
        return NULL;
    }

    return drive;
}

static const char *xen_block_drive_get_node_name(XenBlockDrive *drive)
{
    return drive->node_name ? drive->node_name : "";
}

static void xen_block_iothread_destroy(XenBlockIOThread *iothread,
                                       Error **errp)
{
    qmp_object_del(iothread->id, errp);

    g_free(iothread->id);
    g_free(iothread);
}

static XenBlockIOThread *xen_block_iothread_create(const char *id,
                                                   Error **errp)
{
    ERRP_GUARD();
    XenBlockIOThread *iothread = g_new(XenBlockIOThread, 1);
    ObjectOptions *opts;

    iothread->id = g_strdup(id);

    opts = g_new(ObjectOptions, 1);
    *opts = (ObjectOptions) {
        .qom_type = OBJECT_TYPE_IOTHREAD,
        .id = g_strdup(id),
    };
    qmp_object_add(opts, errp);
    qapi_free_ObjectOptions(opts);

    if (*errp) {
        g_free(iothread->id);
        g_free(iothread);
        return NULL;
    }

    return iothread;
}

static void xen_block_device_create(XenBackendInstance *backend,
                                    QDict *opts, Error **errp)
{
    ERRP_GUARD();
    XenBus *xenbus = xen_backend_get_bus(backend);
    const char *name = xen_backend_get_name(backend);
    unsigned long number;
    const char *vdev, *device_type;
    XenBlockDrive *drive = NULL;
    XenBlockIOThread *iothread = NULL;
    XenDevice *xendev = NULL;
    const char *type;
    XenBlockDevice *blockdev;

    if (qemu_strtoul(name, NULL, 10, &number)) {
        error_setg(errp, "failed to parse name '%s'", name);
        goto fail;
    }

    trace_xen_block_device_create(number);

    vdev = qdict_get_try_str(opts, "dev");
    if (!vdev) {
        error_setg(errp, "no dev parameter");
        goto fail;
    }

    device_type = qdict_get_try_str(opts, "device-type");
    if (!device_type) {
        error_setg(errp, "no device-type parameter");
        goto fail;
    }

    if (!strcmp(device_type, "disk")) {
        type = TYPE_XEN_DISK_DEVICE;
    } else if (!strcmp(device_type, "cdrom")) {
        type = TYPE_XEN_CDROM_DEVICE;
    } else {
        error_setg(errp, "invalid device-type parameter '%s'", device_type);
        goto fail;
    }

    drive = xen_block_drive_create(vdev, device_type, opts, errp);
    if (!drive) {
        error_prepend(errp, "failed to create drive: ");
        goto fail;
    }

    iothread = xen_block_iothread_create(vdev, errp);
    if (*errp) {
        error_prepend(errp, "failed to create iothread: ");
        goto fail;
    }

    xendev = XEN_DEVICE(qdev_new(type));
    blockdev = XEN_BLOCK_DEVICE(xendev);

    if (!object_property_set_str(OBJECT(xendev), "vdev", vdev,
                                 errp)) {
        error_prepend(errp, "failed to set 'vdev': ");
        goto fail;
    }

    if (!object_property_set_str(OBJECT(xendev), "drive",
                                 xen_block_drive_get_node_name(drive),
                                 errp)) {
        error_prepend(errp, "failed to set 'drive': ");
        goto fail;
    }

    if (!object_property_set_str(OBJECT(xendev), "iothread", iothread->id,
                                 errp)) {
        error_prepend(errp, "failed to set 'iothread': ");
        goto fail;
    }

    blockdev->iothread = iothread;
    blockdev->drive = drive;

    if (!qdev_realize_and_unref(DEVICE(xendev), BUS(xenbus), errp)) {
        error_prepend(errp, "realization of device %s failed: ", type);
        goto fail;
    }

    xen_backend_set_device(backend, xendev);
    return;

fail:
    if (xendev) {
        object_unparent(OBJECT(xendev));
    }

    if (iothread) {
        xen_block_iothread_destroy(iothread, NULL);
    }

    if (drive) {
        xen_block_drive_destroy(drive, NULL);
    }
}

static void xen_block_device_destroy(XenBackendInstance *backend,
                                     Error **errp)
{
    ERRP_GUARD();
    XenDevice *xendev = xen_backend_get_device(backend);
    XenBlockDevice *blockdev = XEN_BLOCK_DEVICE(xendev);
    XenBlockVdev *vdev = &blockdev->props.vdev;
    XenBlockDrive *drive = blockdev->drive;
    XenBlockIOThread *iothread = blockdev->iothread;

    trace_xen_block_device_destroy(vdev->number);

    object_unparent(OBJECT(xendev));

    /*
     * Drain all pending RCU callbacks as object_unparent() frees `xendev'
     * in a RCU callback.
     * And due to the property "drive" still existing in `xendev', we
     * can't destroy the XenBlockDrive associated with `xendev' with
     * xen_block_drive_destroy() below.
     */
    drain_call_rcu();

    if (iothread) {
        xen_block_iothread_destroy(iothread, errp);
        if (*errp) {
            error_prepend(errp, "failed to destroy iothread: ");
            return;
        }
    }

    if (drive) {
        xen_block_drive_destroy(drive, errp);
        if (*errp) {
            error_prepend(errp, "failed to destroy drive: ");
            return;
        }
    }
}

static const XenBackendInfo xen_block_backend_info = {
    .type = "qdisk",
    .create = xen_block_device_create,
    .destroy = xen_block_device_destroy,
};

static void xen_block_register_backend(void)
{
    xen_backend_register(&xen_block_backend_info);
}

xen_backend_init(xen_block_register_backend);
