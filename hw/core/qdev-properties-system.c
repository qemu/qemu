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
#include "audio/audio.h"
#include "net/net.h"
#include "hw/qdev-properties.h"
#include "qapi/error.h"
#include "qapi/qmp/qerror.h"
#include "sysemu/block-backend.h"
#include "sysemu/blockdev.h"
#include "hw/block/block.h"
#include "net/hub.h"
#include "qapi/visitor.h"
#include "chardev/char-fe.h"
#include "sysemu/iothread.h"
#include "sysemu/tpm_backend.h"

static bool check_prop_still_unset(DeviceState *dev, const char *name,
                                   const void *old_val, const char *new_val,
                                   Error **errp)
{
    const GlobalProperty *prop = qdev_find_global_prop(dev, name);

    if (!old_val) {
        return true;
    }

    if (prop) {
        error_setg(errp, "-global %s.%s=... conflicts with %s=%s",
                   prop->driver, prop->property, name, new_val);
    } else {
        /* Error message is vague, but a better one would be hard */
        error_setg(errp, "%s=%s conflicts, and override is not implemented",
                   name, new_val);
    }
    return false;
}


/* --- drive --- */

static void get_drive(Object *obj, Visitor *v, const char *name, void *opaque,
                      Error **errp)
{
    DeviceState *dev = DEVICE(obj);
    Property *prop = opaque;
    void **ptr = qdev_get_prop_ptr(dev, prop);
    const char *value;
    char *p;

    if (*ptr) {
        value = blk_name(*ptr);
        if (!*value) {
            BlockDriverState *bs = blk_bs(*ptr);
            if (bs) {
                value = bdrv_get_node_name(bs);
            }
        }
    } else {
        value = "";
    }

    p = g_strdup(value);
    visit_type_str(v, name, &p, errp);
    g_free(p);
}

static void set_drive_helper(Object *obj, Visitor *v, const char *name,
                             void *opaque, bool iothread, Error **errp)
{
    DeviceState *dev = DEVICE(obj);
    Property *prop = opaque;
    void **ptr = qdev_get_prop_ptr(dev, prop);
    char *str;
    BlockBackend *blk;
    bool blk_created = false;
    int ret;

    if (dev->realized) {
        qdev_prop_set_after_realize(dev, name, errp);
        return;
    }

    if (!visit_type_str(v, name, &str, errp)) {
        return;
    }

    /*
     * TODO Should this really be an error?  If no, the old value
     * needs to be released before we store the new one.
     */
    if (!check_prop_still_unset(dev, name, *ptr, str, errp)) {
        return;
    }

    if (!*str) {
        g_free(str);
        *ptr = NULL;
        return;
    }

    blk = blk_by_name(str);
    if (!blk) {
        BlockDriverState *bs = bdrv_lookup_bs(NULL, str, NULL);
        if (bs) {
            /*
             * If the device supports iothreads, it will make sure to move the
             * block node to the right AioContext if necessary (or fail if this
             * isn't possible because of other users). Devices that are not
             * aware of iothreads require their BlockBackends to be in the main
             * AioContext.
             */
            AioContext *ctx = iothread ? bdrv_get_aio_context(bs) :
                                         qemu_get_aio_context();
            blk = blk_new(ctx, 0, BLK_PERM_ALL);
            blk_created = true;

            ret = blk_insert_bs(blk, bs, errp);
            if (ret < 0) {
                goto fail;
            }
        }
    }
    if (!blk) {
        error_setg(errp, "Property '%s.%s' can't find value '%s'",
                   object_get_typename(OBJECT(dev)), prop->name, str);
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

    g_free(str);
}

static void set_drive(Object *obj, Visitor *v, const char *name, void *opaque,
                      Error **errp)
{
    set_drive_helper(obj, v, name, opaque, false, errp);
}

static void set_drive_iothread(Object *obj, Visitor *v, const char *name,
                               void *opaque, Error **errp)
{
    set_drive_helper(obj, v, name, opaque, true, errp);
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

const PropertyInfo qdev_prop_drive = {
    .name  = "str",
    .description = "Node name or ID of a block device to use as a backend",
    .get   = get_drive,
    .set   = set_drive,
    .release = release_drive,
};

const PropertyInfo qdev_prop_drive_iothread = {
    .name  = "str",
    .description = "Node name or ID of a block device to use as a backend",
    .get   = get_drive,
    .set   = set_drive_iothread,
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
    Property *prop = opaque;
    CharBackend *be = qdev_get_prop_ptr(dev, prop);
    Chardev *s;
    char *str;

    if (dev->realized) {
        qdev_prop_set_after_realize(dev, name, errp);
        return;
    }

    if (!visit_type_str(v, name, &str, errp)) {
        return;
    }

    /*
     * TODO Should this really be an error?  If no, the old value
     * needs to be released before we store the new one.
     */
    if (!check_prop_still_unset(dev, name, be->chr, str, errp)) {
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
    int queues, err = 0, i = 0;
    char *str;

    if (dev->realized) {
        qdev_prop_set_after_realize(dev, name, errp);
        return;
    }

    if (!visit_type_str(v, name, &str, errp)) {
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
        if (peers[i]->peer) {
            err = -EEXIST;
            goto out;
        }

        /*
         * TODO Should this really be an error?  If no, the old value
         * needs to be released before we store the new one.
         */
        if (!check_prop_still_unset(dev, name, ncs[i], str, errp)) {
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


/* --- audiodev --- */
static void get_audiodev(Object *obj, Visitor *v, const char* name,
                         void *opaque, Error **errp)
{
    DeviceState *dev = DEVICE(obj);
    Property *prop = opaque;
    QEMUSoundCard *card = qdev_get_prop_ptr(dev, prop);
    char *p = g_strdup(audio_get_id(card));

    visit_type_str(v, name, &p, errp);
    g_free(p);
}

static void set_audiodev(Object *obj, Visitor *v, const char* name,
                         void *opaque, Error **errp)
{
    DeviceState *dev = DEVICE(obj);
    Property *prop = opaque;
    QEMUSoundCard *card = qdev_get_prop_ptr(dev, prop);
    AudioState *state;
    int err = 0;
    char *str;

    if (dev->realized) {
        qdev_prop_set_after_realize(dev, name, errp);
        return;
    }

    if (!visit_type_str(v, name, &str, errp)) {
        return;
    }

    state = audio_state_by_name(str);

    if (!state) {
        err = -ENOENT;
        goto out;
    }
    card->state = state;

out:
    error_set_from_qdev_prop_error(errp, err, dev, prop, str);
    g_free(str);
}

const PropertyInfo qdev_prop_audiodev = {
    .name = "str",
    .description = "ID of an audiodev to use as a backend",
    /* release done on shutdown */
    .get = get_audiodev,
    .set = set_audiodev,
};

bool qdev_prop_set_drive_err(DeviceState *dev, const char *name,
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

    return object_property_set_str(OBJECT(dev), name, ref, errp);
}

void qdev_prop_set_drive(DeviceState *dev, const char *name,
                         BlockBackend *value)
{
    qdev_prop_set_drive_err(dev, name, value, &error_abort);
}

void qdev_prop_set_chr(DeviceState *dev, const char *name,
                       Chardev *value)
{
    assert(!value || value->label);
    object_property_set_str(OBJECT(dev), name, value ? value->label : "",
                            &error_abort);
}

void qdev_prop_set_netdev(DeviceState *dev, const char *name,
                          NetClientState *value)
{
    assert(!value || value->name);
    object_property_set_str(OBJECT(dev), name, value ? value->name : "",
                            &error_abort);
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
