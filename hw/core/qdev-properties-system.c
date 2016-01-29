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

#include "qemu/osdep.h"
#include "net/net.h"
#include "hw/qdev.h"
#include "qapi/qmp/qerror.h"
#include "sysemu/block-backend.h"
#include "sysemu/blockdev.h"
#include "hw/block/block.h"
#include "net/hub.h"
#include "qapi/visitor.h"
#include "sysemu/char.h"
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

    blk = blk_by_name(str);
    if (!blk) {
        error_setg(errp, "Property '%s.%s' can't find value '%s'",
                   object_get_typename(OBJECT(dev)), propname, str);
        return;
    }
    if (blk_attach_dev(blk, dev) < 0) {
        DriveInfo *dinfo = blk_legacy_dinfo(blk);

        if (dinfo->type != IF_NONE) {
            error_setg(errp, "Drive '%s' is already in use because "
                       "it has been automatically connected to another "
                       "device (did you need 'if=none' in the drive options?)",
                       str);
        } else {
            error_setg(errp, "Drive '%s' is already in use by another device",
                       str);
        }
        return;
    }
    *ptr = blk;
}

static void release_drive(Object *obj, const char *name, void *opaque)
{
    DeviceState *dev = DEVICE(obj);
    Property *prop = opaque;
    BlockBackend **ptr = qdev_get_prop_ptr(dev, prop);

    if (*ptr) {
        blk_detach_dev(*ptr, dev);
        blockdev_auto_del(*ptr);
    }
}

static char *print_drive(void *ptr)
{
    return g_strdup(blk_name(ptr));
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

PropertyInfo qdev_prop_drive = {
    .name  = "str",
    .description = "ID of a drive to use as a backend",
    .get   = get_drive,
    .set   = set_drive,
    .release = release_drive,
};

/* --- character device --- */

static void parse_chr(DeviceState *dev, const char *str, void **ptr,
                      const char *propname, Error **errp)
{
    CharDriverState *chr = qemu_chr_find(str);
    if (chr == NULL) {
        error_setg(errp, "Property '%s.%s' can't find value '%s'",
                   object_get_typename(OBJECT(dev)), propname, str);
        return;
    }
    if (qemu_chr_fe_claim(chr) != 0) {
        error_setg(errp, "Property '%s.%s' can't take value '%s', it's in use",
                  object_get_typename(OBJECT(dev)), propname, str);
        return;
    }
    *ptr = chr;
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


static char *print_chr(void *ptr)
{
    CharDriverState *chr = ptr;
    const char *val = chr->label ? chr->label : "";

    return g_strdup(val);
}

static void get_chr(Object *obj, Visitor *v, const char *name, void *opaque,
                    Error **errp)
{
    get_pointer(obj, v, opaque, print_chr, name, errp);
}

static void set_chr(Object *obj, Visitor *v, const char *name, void *opaque,
                    Error **errp)
{
    set_pointer(obj, v, opaque, parse_chr, name, errp);
}

PropertyInfo qdev_prop_chr = {
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
                                          NET_CLIENT_OPTIONS_KIND_NIC,
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

PropertyInfo qdev_prop_netdev = {
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

PropertyInfo qdev_prop_vlan = {
    .name  = "int32",
    .description = "Integer VLAN id to connect to",
    .print = print_vlan,
    .get   = get_vlan,
    .set   = set_vlan,
};

void qdev_prop_set_drive(DeviceState *dev, const char *name,
                         BlockBackend *value, Error **errp)
{
    object_property_set_str(OBJECT(dev), value ? blk_name(value) : "",
                            name, errp);
}

void qdev_prop_set_chr(DeviceState *dev, const char *name,
                       CharDriverState *value)
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

static int qdev_add_one_global(void *opaque, QemuOpts *opts, Error **errp)
{
    GlobalProperty *g;

    g = g_malloc0(sizeof(*g));
    g->driver   = qemu_opt_get(opts, "driver");
    g->property = qemu_opt_get(opts, "property");
    g->value    = qemu_opt_get(opts, "value");
    g->user_provided = true;
    qdev_prop_register_global(g);
    return 0;
}

void qemu_add_globals(void)
{
    qemu_opts_foreach(qemu_find_opts("global"),
                      qdev_add_one_global, NULL, NULL);
}
