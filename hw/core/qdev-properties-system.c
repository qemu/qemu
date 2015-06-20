/*
 * qdev property parsing and global properties
 * (parts specific for qemu-system-*)
 *
 * This file is based on code from hw/qdev-properties.c from
 * commit 074a86fccd185616469dfcdc0e157f438aebba18,
 * Copyright (c) Gerd Hoffmann <kraxel@redhat.com> and other contributors.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "net/net.h"
#include "hw/qdev.h"
#include "qapi/qmp/qerror.h"
#include "sysemu/blockdev.h"
#include "hw/block/block.h"
#include "net/hub.h"
#include "qapi/visitor.h"
#include "sysemu/char.h"

static void get_pointer(Object *obj, Visitor *v, Property *prop,
                        const char *(*print)(void *ptr),
                        const char *name, Error **errp)
{
    DeviceState *dev = DEVICE(obj);
    void **ptr = qdev_get_prop_ptr(dev, prop);
    char *p;

    p = (char *) (*ptr ? print(*ptr) : "");
    visit_type_str(v, &p, name, errp);
}

static void set_pointer(Object *obj, Visitor *v, Property *prop,
                        int (*parse)(DeviceState *dev, const char *str,
                                     void **ptr),
                        const char *name, Error **errp)
{
    DeviceState *dev = DEVICE(obj);
    Error *local_err = NULL;
    void **ptr = qdev_get_prop_ptr(dev, prop);
    char *str;
    int ret;

    if (dev->realized) {
        qdev_prop_set_after_realize(dev, name, errp);
        return;
    }

    visit_type_str(v, &str, name, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }
    if (!*str) {
        g_free(str);
        *ptr = NULL;
        return;
    }
    ret = parse(dev, str, ptr);
    error_set_from_qdev_prop_error(errp, ret, dev, prop, str);
    g_free(str);
}

/* --- drive --- */

static int parse_drive(DeviceState *dev, const char *str, void **ptr)
{
    BlockDriverState *bs;

    bs = bdrv_find(str);
    if (bs == NULL) {
        return -ENOENT;
    }
    if (bdrv_attach_dev(bs, dev) < 0) {
        return -EEXIST;
    }
    *ptr = bs;
    return 0;
}

static void release_drive(Object *obj, const char *name, void *opaque)
{
    DeviceState *dev = DEVICE(obj);
    Property *prop = opaque;
    BlockDriverState **ptr = qdev_get_prop_ptr(dev, prop);

    if (*ptr) {
        bdrv_detach_dev(*ptr, dev);
        blockdev_auto_del(*ptr);
    }
}

static const char *print_drive(void *ptr)
{
    return bdrv_get_device_name(ptr);
}

static void get_drive(Object *obj, Visitor *v, void *opaque,
                      const char *name, Error **errp)
{
    get_pointer(obj, v, opaque, print_drive, name, errp);
}

static void set_drive(Object *obj, Visitor *v, void *opaque,
                      const char *name, Error **errp)
{
    set_pointer(obj, v, opaque, parse_drive, name, errp);
}

PropertyInfo qdev_prop_drive = {
    .name  = "drive",
    .get   = get_drive,
    .set   = set_drive,
    .release = release_drive,
};

/* --- character device --- */

static int parse_chr(DeviceState *dev, const char *str, void **ptr)
{
    CharDriverState *chr = qemu_chr_find(str);
    if (chr == NULL) {
        return -ENOENT;
    }
    if (qemu_chr_fe_claim(chr) != 0) {
        return -EEXIST;
    }
    *ptr = chr;
    return 0;
}

static void release_chr(Object *obj, const char *name, void *opaque)
{
    DeviceState *dev = DEVICE(obj);
    Property *prop = opaque;
    CharDriverState **ptr = qdev_get_prop_ptr(dev, prop);
    CharDriverState *chr = *ptr;

    if (chr) {
        qemu_chr_add_handlers(chr, NULL, NULL, NULL, NULL);
        qemu_chr_fe_release(chr);
    }
}


static const char *print_chr(void *ptr)
{
    CharDriverState *chr = ptr;

    return chr->label ? chr->label : "";
}

static void get_chr(Object *obj, Visitor *v, void *opaque,
                    const char *name, Error **errp)
{
    get_pointer(obj, v, opaque, print_chr, name, errp);
}

static void set_chr(Object *obj, Visitor *v, void *opaque,
                    const char *name, Error **errp)
{
    set_pointer(obj, v, opaque, parse_chr, name, errp);
}

PropertyInfo qdev_prop_chr = {
    .name  = "chr",
    .get   = get_chr,
    .set   = set_chr,
    .release = release_chr,
};

/* --- netdev device --- */

static int parse_netdev(DeviceState *dev, const char *str, void **ptr)
{
    NICPeers *peers_ptr = (NICPeers *)ptr;
    NICConf *conf = container_of(peers_ptr, NICConf, peers);
    NetClientState **ncs = peers_ptr->ncs;
    NetClientState *peers[MAX_QUEUE_NUM];
    int queues, i = 0;
    int ret;

    queues = qemu_find_net_clients_except(str, peers,
                                          NET_CLIENT_OPTIONS_KIND_NIC,
                                          MAX_QUEUE_NUM);
    if (queues == 0) {
        ret = -ENOENT;
        goto err;
    }

    if (queues > MAX_QUEUE_NUM) {
        ret = -E2BIG;
        goto err;
    }

    for (i = 0; i < queues; i++) {
        if (peers[i] == NULL) {
            ret = -ENOENT;
            goto err;
        }

        if (peers[i]->peer) {
            ret = -EEXIST;
            goto err;
        }

        if (ncs[i]) {
            ret = -EINVAL;
            goto err;
        }

        ncs[i] = peers[i];
        ncs[i]->queue_index = i;
    }

    conf->queues = queues;

    return 0;

err:
    return ret;
}

static const char *print_netdev(void *ptr)
{
    NetClientState *netdev = ptr;

    return netdev->name ? netdev->name : "";
}

static void get_netdev(Object *obj, Visitor *v, void *opaque,
                       const char *name, Error **errp)
{
    get_pointer(obj, v, opaque, print_netdev, name, errp);
}

static void set_netdev(Object *obj, Visitor *v, void *opaque,
                       const char *name, Error **errp)
{
    set_pointer(obj, v, opaque, parse_netdev, name, errp);
}

PropertyInfo qdev_prop_netdev = {
    .name  = "netdev",
    .get   = get_netdev,
    .set   = set_netdev,
};

/* --- vlan --- */

static int print_vlan(DeviceState *dev, Property *prop, char *dest, size_t len)
{
    NetClientState **ptr = qdev_get_prop_ptr(dev, prop);

    if (*ptr) {
        int id;
        if (!net_hub_id_for_client(*ptr, &id)) {
            return snprintf(dest, len, "%d", id);
        }
    }

    return snprintf(dest, len, "<null>");
}

static void get_vlan(Object *obj, Visitor *v, void *opaque,
                     const char *name, Error **errp)
{
    DeviceState *dev = DEVICE(obj);
    Property *prop = opaque;
    NetClientState **ptr = qdev_get_prop_ptr(dev, prop);
    int32_t id = -1;

    if (*ptr) {
        int hub_id;
        if (!net_hub_id_for_client(*ptr, &hub_id)) {
            id = hub_id;
        }
    }

    visit_type_int32(v, &id, name, errp);
}

static void set_vlan(Object *obj, Visitor *v, void *opaque,
                     const char *name, Error **errp)
{
    DeviceState *dev = DEVICE(obj);
    Property *prop = opaque;
    NICPeers *peers_ptr = qdev_get_prop_ptr(dev, prop);
    NetClientState **ptr = &peers_ptr->ncs[0];
    Error *local_err = NULL;
    int32_t id;
    NetClientState *hubport;

    if (dev->realized) {
        qdev_prop_set_after_realize(dev, name, errp);
        return;
    }

    visit_type_int32(v, &id, name, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }
    if (id == -1) {
        *ptr = NULL;
        return;
    }
    if (*ptr) {
        error_set_from_qdev_prop_error(errp, -EINVAL, dev, prop, name);
        return;
    }

    hubport = net_hub_port_find(id);
    if (!hubport) {
        error_set(errp, QERR_INVALID_PARAMETER_VALUE,
                  name, prop->info->name);
        return;
    }
    *ptr = hubport;
}

PropertyInfo qdev_prop_vlan = {
    .name  = "vlan",
    .print = print_vlan,
    .get   = get_vlan,
    .set   = set_vlan,
};

int qdev_prop_set_drive(DeviceState *dev, const char *name,
                        BlockDriverState *value)
{
    Error *errp = NULL;
    const char *bdrv_name = value ? bdrv_get_device_name(value) : "";
    object_property_set_str(OBJECT(dev), bdrv_name,
                            name, &errp);
    if (errp) {
        qerror_report_err(errp);
        error_free(errp);
        return -1;
    }
    return 0;
}

void qdev_prop_set_drive_nofail(DeviceState *dev, const char *name,
                                BlockDriverState *value)
{
    if (qdev_prop_set_drive(dev, name, value) < 0) {
        exit(1);
    }
}
void qdev_prop_set_chr(DeviceState *dev, const char *name,
                       CharDriverState *value)
{
    Error *errp = NULL;
    assert(!value || value->label);
    object_property_set_str(OBJECT(dev),
                            value ? value->label : "", name, &errp);
    assert_no_error(errp);
}

void qdev_prop_set_netdev(DeviceState *dev, const char *name,
                          NetClientState *value)
{
    Error *errp = NULL;
    assert(!value || value->name);
    object_property_set_str(OBJECT(dev),
                            value ? value->name : "", name, &errp);
    assert_no_error(errp);
}

void qdev_set_nic_properties(DeviceState *dev, NICInfo *nd)
{
    qdev_prop_set_macaddr(dev, "mac", nd->macaddr.a);
    if (nd->netdev) {
        qdev_prop_set_netdev(dev, "netdev", nd->netdev);
    }
    if (nd->nvectors != DEV_NVECTORS_UNSPECIFIED &&
        object_property_find(OBJECT(dev), "vectors", NULL)) {
        qdev_prop_set_uint32(dev, "vectors", nd->nvectors);
    }
    nd->instantiated = 1;
}

static int qdev_add_one_global(QemuOpts *opts, void *opaque)
{
    GlobalProperty *g;

    g = g_malloc0(sizeof(*g));
    g->driver   = qemu_opt_get(opts, "driver");
    g->property = qemu_opt_get(opts, "property");
    g->value    = qemu_opt_get(opts, "value");
    qdev_prop_register_global(g);
    return 0;
}

void qemu_add_globals(void)
{
    qemu_opts_foreach(qemu_find_opts("global"), qdev_add_one_global, NULL, 0);
}
