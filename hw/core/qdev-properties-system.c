/*
 * qdev property parsing
 * (parts specific for qemu-system-*)
 *
 * This file is based on code from hw/qdev-properties.c from
 * commit 074a86fccd185616469dfcdc0e157f438aebba18,
 * Copyright (c) Gerd Hoffmann <kraxel@redhat.com> and other contributors.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "net/net.h"
#include "hw/qdev.h"
#include "qapi/error.h"
#include "qapi/qmp/qerror.h"
#include "sysemu/block-backend.h"
#include "sysemu/blockdev.h"
#include "hw/block/block.h"
#include "net/hub.h"
#include "qapi/visitor.h"
#include "chardev/char-fe.h"
#include "sysemu/iothread.h"

static void get_pointer(Object *obj, Visitor *v, Property *prop,
                        char *(*print)(void *ptr),
                        const char *name, Error **errp)
{
    DeviceState *dev = DEVICE(obj);
    void **ptr = qdev_get_prop_ptr(dev, prop);
    char *p;

    p = *ptr ? print(*ptr) : g_strdup("");
    visit_type_str(v, name, &p, errp);
    g_free(p);
}

static void set_pointer(Object *obj, Visitor *v, Property *prop,
                        void (*parse)(DeviceState *dev, const char *str,
                                      void **ptr, const char *propname,
                                      Error **errp),
                        const char *name, Error **errp)
{
    DeviceState *dev = DEVICE(obj);
    Error *local_err = NULL;
    void **ptr = qdev_get_prop_ptr(dev, prop);
    char *str;

    if (dev->realized) {
        qdev_prop_set_after_realize(dev, name, errp);
        return;
    }

    visit_type_str(v, name, &str, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }
    if (!*str) {
        g_free(str);
        *ptr = NULL;
        return;
    }
    parse(dev, str, ptr, prop->name, errp);
    g_free(str);
}

/* --- drive --- */

static void parse_drive(DeviceState *dev, const char *str, void **ptr,
                        const char *propname, Error **errp)
{
    BlockBackend *blk;
    bool blk_created = false;
    int ret;

    blk = blk_by_name(str);
    if (!blk) {
        BlockDriverState *bs = bdrv_lookup_bs(NULL, str, NULL);
        if (bs) {
            blk = blk_new(0, BLK_PERM_ALL);
            blk_created = true;

            ret = blk_insert_bs(blk, bs, errp);
            if (ret < 0) {
                goto fail;
            }
        }
    }
    if (!blk) {
        error_setg(errp, "Property '%s.%s' can't find value '%s'",
                   object_get_typename(OBJECT(dev)), propname, str);
        goto fail;
    }
    if (blk_attach_dev(blk, dev) < 0) {
        DriveInfo *dinfo = blk_legacy_dinfo(blk);

        if (dinfo && dinfo->type != IF_NONE) {
            error_setg(errp, "Drive '%s' is already in use because "
                       "it has been automatically connected to another "
                       "device (did you need 'if=none' in the drive options?)",
                       str);
        } else {
            error_setg(errp, "Drive '%s' is already in use by another device",
                       str);
        }
        goto fail;
    }

    *ptr = blk;

fail:
    if (blk_created) {
        /* If we need to keep a reference, blk_attach_dev() took it */
        blk_unref(blk);
    }
}

static void release_drive(Object *obj, const char *name, void *opaque)
{
    DeviceState *dev = DEVICE(obj);
    Property *prop = opaque;
    BlockBackend **ptr = qdev_get_prop_ptr(dev, prop);

    if (*ptr) {
        AioContext *ctx = blk_get_aio_context(*ptr);

        aio_context_acquire(ctx);
        blockdev_auto_del(*ptr);
        blk_detach_dev(*ptr, dev);
        aio_context_release(ctx);
    }
}

static char *print_drive(void *ptr)
{
    const char *name;

    name = blk_name(ptr);
    if (!*name) {
        BlockDriverState *bs = blk_bs(ptr);
        if (bs) {
            name = bdrv_get_node_name(bs);
        }
    }
    return g_strdup(name);
}

static void get_drive(Object *obj, Visitor *v, const char *name, void *opaque,
                      Error **errp)
{
    get_pointer(obj, v, opaque, print_drive, name, errp);
}

static void set_drive(Object *obj, Visitor *v, const char *name, void *opaque,
                      Error **errp)
{
    set_pointer(obj, v, opaque, parse_drive, name, errp);
}

const PropertyInfo qdev_prop_drive = {
    .name  = "str",
    .description = "Node name or ID of a block device to use as a backend",
    .get   = get_drive,
    .set   = set_drive,
    .release = release_drive,
};

/* --- character device --- */

static void get_chr(Object *obj, Visitor *v, const char *name, void *opaque,
                    Error **errp)
{
    DeviceState *dev = DEVICE(obj);
    CharBackend *be = qdev_get_prop_ptr(dev, opaque);
    char *p;

    p = g_strdup(be->chr && be->chr->label ? be->chr->label : "");
    visit_type_str(v, name, &p, errp);
    g_free(p);
}

static void set_chr(Object *obj, Visitor *v, const char *name, void *opaque,
                    Error **errp)
{
    DeviceState *dev = DEVICE(obj);
    Error *local_err = NULL;
    Property *prop = opaque;
    CharBackend *be = qdev_get_prop_ptr(dev, prop);
    Chardev *s;
    char *str;

    if (dev->realized) {
        qdev_prop_set_after_realize(dev, name, errp);
        return;
    }

    visit_type_str(v, name, &str, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    if (!*str) {
        g_free(str);
        be->chr = NULL;
        return;
    }

    s = qemu_chr_find(str);
    if (s == NULL) {
        error_setg(errp, "Property '%s.%s' can't find value '%s'",
                   object_get_typename(obj), prop->name, str);
    } else if (!qemu_chr_fe_init(be, s, errp)) {
        error_prepend(errp, "Property '%s.%s' can't take value '%s': ",
                      object_get_typename(obj), prop->name, str);
    }
    g_free(str);
}

static void release_chr(Object *obj, const char *name, void *opaque)
{
    DeviceState *dev = DEVICE(obj);
    Property *prop = opaque;
    CharBackend *be = qdev_get_prop_ptr(dev, prop);

    qemu_chr_fe_deinit(be, false);
}

const PropertyInfo qdev_prop_chr = {
    .name  = "str",
    .description = "ID of a chardev to use as a backend",
    .get   = get_chr,
    .set   = set_chr,
    .release = release_chr,
};

/* --- netdev device --- */
static void get_netdev(Object *obj, Visitor *v, const char *name,
                       void *opaque, Error **errp)
{
    DeviceState *dev = DEVICE(obj);
    Property *prop = opaque;
    NICPeers *peers_ptr = qdev_get_prop_ptr(dev, prop);
    char *p = g_strdup(peers_ptr->ncs[0] ? peers_ptr->ncs[0]->name : "");

    visit_type_str(v, name, &p, errp);
    g_free(p);
}

static void set_netdev(Object *obj, Visitor *v, const char *name,
                       void *opaque, Error **errp)
{
    DeviceState *dev = DEVICE(obj);
    Property *prop = opaque;
    NICPeers *peers_ptr = qdev_get_prop_ptr(dev, prop);
    NetClientState **ncs = peers_ptr->ncs;
    NetClientState *peers[MAX_QUEUE_NUM];
    Error *local_err = NULL;
    int queues, err = 0, i = 0;
    char *str;

    if (dev->realized) {
        qdev_prop_set_after_realize(dev, name, errp);
        return;
    }

    visit_type_str(v, name, &str, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    queues = qemu_find_net_clients_except(str, peers,
                                          NET_CLIENT_DRIVER_NIC,
                                          MAX_QUEUE_NUM);
    if (queues == 0) {
        err = -ENOENT;
        goto out;
    }

    if (queues > MAX_QUEUE_NUM) {
        error_setg(errp, "queues of backend '%s'(%d) exceeds QEMU limitation(%d)",
                   str, queues, MAX_QUEUE_NUM);
        goto out;
    }

    for (i = 0; i < queues; i++) {
        if (peers[i] == NULL) {
            err = -ENOENT;
            goto out;
        }

        if (peers[i]->peer) {
            err = -EEXIST;
            goto out;
        }

        if (ncs[i]) {
            err = -EINVAL;
            goto out;
        }

        ncs[i] = peers[i];
        ncs[i]->queue_index = i;
    }

    peers_ptr->queues = queues;

out:
    error_set_from_qdev_prop_error(errp, err, dev, prop, str);
    g_free(str);
}

const PropertyInfo qdev_prop_netdev = {
    .name  = "str",
    .description = "ID of a netdev to use as a backend",
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

static void get_vlan(Object *obj, Visitor *v, const char *name, void *opaque,
                     Error **errp)
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

    visit_type_int32(v, name, &id, errp);
}

static void set_vlan(Object *obj, Visitor *v, const char *name, void *opaque,
                     Error **errp)
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

    visit_type_int32(v, name, &id, &local_err);
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
        error_setg(errp, QERR_INVALID_PARAMETER_VALUE,
                   name, prop->info->name);
        return;
    }
    *ptr = hubport;
}

const PropertyInfo qdev_prop_vlan = {
    .name  = "int32",
    .description = "Integer VLAN id to connect to",
    .print = print_vlan,
    .get   = get_vlan,
    .set   = set_vlan,
};

void qdev_prop_set_drive(DeviceState *dev, const char *name,
                         BlockBackend *value, Error **errp)
{
    const char *ref = "";

    if (value) {
        ref = blk_name(value);
        if (!*ref) {
            const BlockDriverState *bs = blk_bs(value);
            if (bs) {
                ref = bdrv_get_node_name(bs);
            }
        }
    }

    object_property_set_str(OBJECT(dev), ref, name, errp);
}

void qdev_prop_set_chr(DeviceState *dev, const char *name,
                       Chardev *value)
{
    assert(!value || value->label);
    object_property_set_str(OBJECT(dev),
                            value ? value->label : "", name, &error_abort);
}

void qdev_prop_set_netdev(DeviceState *dev, const char *name,
                          NetClientState *value)
{
    assert(!value || value->name);
    object_property_set_str(OBJECT(dev),
                            value ? value->name : "", name, &error_abort);
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
