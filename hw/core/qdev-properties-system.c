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
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "qapi/qapi-types-block.h"
#include "qapi/qapi-types-machine.h"
#include "qapi/qapi-types-migration.h"
#include "qapi/qmp/qerror.h"
#include "qemu/ctype.h"
#include "qemu/cutils.h"
#include "qemu/units.h"
#include "qemu/uuid.h"
#include "qemu/error-report.h"
#include "qdev-prop-internal.h"

#include "audio/audio.h"
#include "chardev/char-fe.h"
#include "sysemu/block-backend.h"
#include "sysemu/blockdev.h"
#include "net/net.h"
#include "hw/pci/pci.h"
#include "hw/pci/pcie.h"
#include "hw/i386/x86.h"
#include "util/block-helpers.h"

static bool check_prop_still_unset(Object *obj, const char *name,
                                   const void *old_val, const char *new_val,
                                   bool allow_override, Error **errp)
{
    const GlobalProperty *prop = qdev_find_global_prop(obj, name);

    if (!old_val || (!prop && allow_override)) {
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
    Property *prop = opaque;
    void **ptr = object_field_prop_ptr(obj, prop);
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
    void **ptr = object_field_prop_ptr(obj, prop);
    char *str;
    BlockBackend *blk;
    bool blk_created = false;
    int ret;
    BlockDriverState *bs;
    AioContext *ctx;

    if (!visit_type_str(v, name, &str, errp)) {
        return;
    }

    if (!check_prop_still_unset(obj, name, *ptr, str, true, errp)) {
        return;
    }

    if (*ptr) {
        /* BlockBackend alread exists. So, we want to change attached node */
        blk = *ptr;
        ctx = blk_get_aio_context(blk);
        bs = bdrv_lookup_bs(NULL, str, errp);
        if (!bs) {
            return;
        }

        if (ctx != bdrv_get_aio_context(bs)) {
            error_setg(errp, "Different aio context is not supported for new "
                       "node");
        }

        aio_context_acquire(ctx);
        blk_replace_bs(blk, bs, errp);
        aio_context_release(ctx);
        return;
    }

    if (!*str) {
        g_free(str);
        *ptr = NULL;
        return;
    }

    blk = blk_by_name(str);
    if (!blk) {
        bs = bdrv_lookup_bs(NULL, str, NULL);
        if (bs) {
            /*
             * If the device supports iothreads, it will make sure to move the
             * block node to the right AioContext if necessary (or fail if this
             * isn't possible because of other users). Devices that are not
             * aware of iothreads require their BlockBackends to be in the main
             * AioContext.
             */
            ctx = iothread ? bdrv_get_aio_context(bs) : qemu_get_aio_context();
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
                   object_get_typename(OBJECT(dev)), name, str);
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
    BlockBackend **ptr = object_field_prop_ptr(obj, prop);

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
    .realized_set_allowed = true,
    .get   = get_drive,
    .set   = set_drive,
    .release = release_drive,
};

const PropertyInfo qdev_prop_drive_iothread = {
    .name  = "str",
    .description = "Node name or ID of a block device to use as a backend",
    .realized_set_allowed = true,
    .get   = get_drive,
    .set   = set_drive_iothread,
    .release = release_drive,
};

/* --- character device --- */

static void get_chr(Object *obj, Visitor *v, const char *name, void *opaque,
                    Error **errp)
{
    CharBackend *be = object_field_prop_ptr(obj, opaque);
    char *p;

    p = g_strdup(be->chr && be->chr->label ? be->chr->label : "");
    visit_type_str(v, name, &p, errp);
    g_free(p);
}

static void set_chr(Object *obj, Visitor *v, const char *name, void *opaque,
                    Error **errp)
{
    Property *prop = opaque;
    CharBackend *be = object_field_prop_ptr(obj, prop);
    Chardev *s;
    char *str;

    if (!visit_type_str(v, name, &str, errp)) {
        return;
    }

    /*
     * TODO Should this really be an error?  If no, the old value
     * needs to be released before we store the new one.
     */
    if (!check_prop_still_unset(obj, name, be->chr, str, false, errp)) {
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
                   object_get_typename(obj), name, str);
    } else if (!qemu_chr_fe_init(be, s, errp)) {
        error_prepend(errp, "Property '%s.%s' can't take value '%s': ",
                      object_get_typename(obj), name, str);
    }
    g_free(str);
}

static void release_chr(Object *obj, const char *name, void *opaque)
{
    Property *prop = opaque;
    CharBackend *be = object_field_prop_ptr(obj, prop);

    qemu_chr_fe_deinit(be, false);
}

const PropertyInfo qdev_prop_chr = {
    .name  = "str",
    .description = "ID of a chardev to use as a backend",
    .get   = get_chr,
    .set   = set_chr,
    .release = release_chr,
};

/* --- mac address --- */

/*
 * accepted syntax versions:
 *   01:02:03:04:05:06
 *   01-02-03-04-05-06
 */
static void get_mac(Object *obj, Visitor *v, const char *name, void *opaque,
                    Error **errp)
{
    Property *prop = opaque;
    MACAddr *mac = object_field_prop_ptr(obj, prop);
    char buffer[2 * 6 + 5 + 1];
    char *p = buffer;

    snprintf(buffer, sizeof(buffer), "%02x:%02x:%02x:%02x:%02x:%02x",
             mac->a[0], mac->a[1], mac->a[2],
             mac->a[3], mac->a[4], mac->a[5]);

    visit_type_str(v, name, &p, errp);
}

static void set_mac(Object *obj, Visitor *v, const char *name, void *opaque,
                    Error **errp)
{
    Property *prop = opaque;
    MACAddr *mac = object_field_prop_ptr(obj, prop);
    int i, pos;
    char *str;
    const char *p;

    if (!visit_type_str(v, name, &str, errp)) {
        return;
    }

    for (i = 0, pos = 0; i < 6; i++, pos += 3) {
        long val;

        if (!qemu_isxdigit(str[pos])) {
            goto inval;
        }
        if (!qemu_isxdigit(str[pos + 1])) {
            goto inval;
        }
        if (i == 5) {
            if (str[pos + 2] != '\0') {
                goto inval;
            }
        } else {
            if (str[pos + 2] != ':' && str[pos + 2] != '-') {
                goto inval;
            }
        }
        if (qemu_strtol(str + pos, &p, 16, &val) < 0 || val > 0xff) {
            goto inval;
        }
        mac->a[i] = val;
    }
    g_free(str);
    return;

inval:
    error_set_from_qdev_prop_error(errp, EINVAL, obj, name, str);
    g_free(str);
}

const PropertyInfo qdev_prop_macaddr = {
    .name  = "str",
    .description = "Ethernet 6-byte MAC Address, example: 52:54:00:12:34:56",
    .get   = get_mac,
    .set   = set_mac,
};

void qdev_prop_set_macaddr(DeviceState *dev, const char *name,
                           const uint8_t *value)
{
    char str[2 * 6 + 5 + 1];
    snprintf(str, sizeof(str), "%02x:%02x:%02x:%02x:%02x:%02x",
             value[0], value[1], value[2], value[3], value[4], value[5]);

    object_property_set_str(OBJECT(dev), name, str, &error_abort);
}

/* --- netdev device --- */
static void get_netdev(Object *obj, Visitor *v, const char *name,
                       void *opaque, Error **errp)
{
    Property *prop = opaque;
    NICPeers *peers_ptr = object_field_prop_ptr(obj, prop);
    char *p = g_strdup(peers_ptr->ncs[0] ? peers_ptr->ncs[0]->name : "");

    visit_type_str(v, name, &p, errp);
    g_free(p);
}

static void set_netdev(Object *obj, Visitor *v, const char *name,
                       void *opaque, Error **errp)
{
    Property *prop = opaque;
    NICPeers *peers_ptr = object_field_prop_ptr(obj, prop);
    NetClientState **ncs = peers_ptr->ncs;
    NetClientState *peers[MAX_QUEUE_NUM];
    int queues, err = 0, i = 0;
    char *str;

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
        if (!check_prop_still_unset(obj, name, ncs[i], str, false, errp)) {
            goto out;
        }

        if (peers[i]->info->check_peer_type) {
            if (!peers[i]->info->check_peer_type(peers[i], obj->class, errp)) {
                goto out;
            }
        }

        ncs[i] = peers[i];
        ncs[i]->queue_index = i;
    }

    peers_ptr->queues = queues;

out:
    error_set_from_qdev_prop_error(errp, err, obj, name, str);
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
    Property *prop = opaque;
    QEMUSoundCard *card = object_field_prop_ptr(obj, prop);
    char *p = g_strdup(audio_get_id(card));

    visit_type_str(v, name, &p, errp);
    g_free(p);
}

static void set_audiodev(Object *obj, Visitor *v, const char* name,
                         void *opaque, Error **errp)
{
    Property *prop = opaque;
    QEMUSoundCard *card = object_field_prop_ptr(obj, prop);
    AudioState *state;
    int err = 0;
    char *str;

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
    error_set_from_qdev_prop_error(errp, err, obj, name, str);
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
        object_property_find(OBJECT(dev), "vectors")) {
        qdev_prop_set_uint32(dev, "vectors", nd->nvectors);
    }
    nd->instantiated = 1;
}

/* --- lost tick policy --- */

static void qdev_propinfo_set_losttickpolicy(Object *obj, Visitor *v,
                                             const char *name, void *opaque,
                                             Error **errp)
{
    Property *prop = opaque;
    int *ptr = object_field_prop_ptr(obj, prop);
    int value;

    if (!visit_type_enum(v, name, &value, prop->info->enum_table, errp)) {
        return;
    }

    if (value == LOST_TICK_POLICY_SLEW) {
        MachineState *ms = MACHINE(qdev_get_machine());

        if (!object_dynamic_cast(OBJECT(ms), TYPE_X86_MACHINE)) {
            error_setg(errp,
                       "the 'slew' policy is only available for x86 machines");
            return;
        }
    }

    *ptr = value;
}

QEMU_BUILD_BUG_ON(sizeof(LostTickPolicy) != sizeof(int));

const PropertyInfo qdev_prop_losttickpolicy = {
    .name  = "LostTickPolicy",
    .enum_table  = &LostTickPolicy_lookup,
    .get   = qdev_propinfo_get_enum,
    .set   = qdev_propinfo_set_losttickpolicy,
    .set_default_value = qdev_propinfo_set_default_value_enum,
};

/* --- blocksize --- */

static void set_blocksize(Object *obj, Visitor *v, const char *name,
                          void *opaque, Error **errp)
{
    DeviceState *dev = DEVICE(obj);
    Property *prop = opaque;
    uint32_t *ptr = object_field_prop_ptr(obj, prop);
    uint64_t value;
    Error *local_err = NULL;

    if (!visit_type_size(v, name, &value, errp)) {
        return;
    }
    check_block_size(dev->id ? : "", name, value, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }
    *ptr = value;
}

const PropertyInfo qdev_prop_blocksize = {
    .name  = "size",
    .description = "A power of two between " MIN_BLOCK_SIZE_STR
                   " and " MAX_BLOCK_SIZE_STR,
    .get   = qdev_propinfo_get_size32,
    .set   = set_blocksize,
    .set_default_value = qdev_propinfo_set_default_value_uint,
};

/* --- Block device error handling policy --- */

QEMU_BUILD_BUG_ON(sizeof(BlockdevOnError) != sizeof(int));

const PropertyInfo qdev_prop_blockdev_on_error = {
    .name = "BlockdevOnError",
    .description = "Error handling policy, "
                   "report/ignore/enospc/stop/auto",
    .enum_table = &BlockdevOnError_lookup,
    .get = qdev_propinfo_get_enum,
    .set = qdev_propinfo_set_enum,
    .set_default_value = qdev_propinfo_set_default_value_enum,
};

/* --- BIOS CHS translation */

QEMU_BUILD_BUG_ON(sizeof(BiosAtaTranslation) != sizeof(int));

const PropertyInfo qdev_prop_bios_chs_trans = {
    .name = "BiosAtaTranslation",
    .description = "Logical CHS translation algorithm, "
                   "auto/none/lba/large/rechs",
    .enum_table = &BiosAtaTranslation_lookup,
    .get = qdev_propinfo_get_enum,
    .set = qdev_propinfo_set_enum,
    .set_default_value = qdev_propinfo_set_default_value_enum,
};

/* --- FDC default drive types */

const PropertyInfo qdev_prop_fdc_drive_type = {
    .name = "FdcDriveType",
    .description = "FDC drive type, "
                   "144/288/120/none/auto",
    .enum_table = &FloppyDriveType_lookup,
    .get = qdev_propinfo_get_enum,
    .set = qdev_propinfo_set_enum,
    .set_default_value = qdev_propinfo_set_default_value_enum,
};

/* --- MultiFDCompression --- */

const PropertyInfo qdev_prop_multifd_compression = {
    .name = "MultiFDCompression",
    .description = "multifd_compression values, "
                   "none/zlib/zstd",
    .enum_table = &MultiFDCompression_lookup,
    .get = qdev_propinfo_get_enum,
    .set = qdev_propinfo_set_enum,
    .set_default_value = qdev_propinfo_set_default_value_enum,
};

/* --- Reserved Region --- */

/*
 * Accepted syntax:
 *   <low address>:<high address>:<type>
 *   where low/high addresses are uint64_t in hexadecimal
 *   and type is a non-negative decimal integer
 */
static void get_reserved_region(Object *obj, Visitor *v, const char *name,
                                void *opaque, Error **errp)
{
    Property *prop = opaque;
    ReservedRegion *rr = object_field_prop_ptr(obj, prop);
    char buffer[64];
    char *p = buffer;
    int rc;

    rc = snprintf(buffer, sizeof(buffer), "0x%"PRIx64":0x%"PRIx64":%u",
                  rr->low, rr->high, rr->type);
    assert(rc < sizeof(buffer));

    visit_type_str(v, name, &p, errp);
}

static void set_reserved_region(Object *obj, Visitor *v, const char *name,
                                void *opaque, Error **errp)
{
    Property *prop = opaque;
    ReservedRegion *rr = object_field_prop_ptr(obj, prop);
    const char *endptr;
    char *str;
    int ret;

    if (!visit_type_str(v, name, &str, errp)) {
        return;
    }

    ret = qemu_strtou64(str, &endptr, 16, &rr->low);
    if (ret) {
        error_setg(errp, "start address of '%s'"
                   " must be a hexadecimal integer", name);
        goto out;
    }
    if (*endptr != ':') {
        goto separator_error;
    }

    ret = qemu_strtou64(endptr + 1, &endptr, 16, &rr->high);
    if (ret) {
        error_setg(errp, "end address of '%s'"
                   " must be a hexadecimal integer", name);
        goto out;
    }
    if (*endptr != ':') {
        goto separator_error;
    }

    ret = qemu_strtoui(endptr + 1, &endptr, 10, &rr->type);
    if (ret) {
        error_setg(errp, "type of '%s'"
                   " must be a non-negative decimal integer", name);
    }
    goto out;

separator_error:
    error_setg(errp, "reserved region fields must be separated with ':'");
out:
    g_free(str);
    return;
}

const PropertyInfo qdev_prop_reserved_region = {
    .name  = "reserved_region",
    .description = "Reserved Region, example: 0xFEE00000:0xFEEFFFFF:0",
    .get   = get_reserved_region,
    .set   = set_reserved_region,
};

/* --- pci address --- */

/*
 * bus-local address, i.e. "$slot" or "$slot.$fn"
 */
static void set_pci_devfn(Object *obj, Visitor *v, const char *name,
                          void *opaque, Error **errp)
{
    Property *prop = opaque;
    int32_t value, *ptr = object_field_prop_ptr(obj, prop);
    unsigned int slot, fn, n;
    char *str;

    if (!visit_type_str(v, name, &str, NULL)) {
        if (!visit_type_int32(v, name, &value, errp)) {
            return;
        }
        if (value < -1 || value > 255) {
            error_setg(errp, QERR_INVALID_PARAMETER_VALUE,
                       name ? name : "null", "a value between -1 and 255");
            return;
        }
        *ptr = value;
        return;
    }

    if (sscanf(str, "%x.%x%n", &slot, &fn, &n) != 2) {
        fn = 0;
        if (sscanf(str, "%x%n", &slot, &n) != 1) {
            goto invalid;
        }
    }
    if (str[n] != '\0' || fn > 7 || slot > 31) {
        goto invalid;
    }
    *ptr = slot << 3 | fn;
    g_free(str);
    return;

invalid:
    error_set_from_qdev_prop_error(errp, EINVAL, obj, name, str);
    g_free(str);
}

static int print_pci_devfn(Object *obj, Property *prop, char *dest,
                           size_t len)
{
    int32_t *ptr = object_field_prop_ptr(obj, prop);

    if (*ptr == -1) {
        return snprintf(dest, len, "<unset>");
    } else {
        return snprintf(dest, len, "%02x.%x", *ptr >> 3, *ptr & 7);
    }
}

const PropertyInfo qdev_prop_pci_devfn = {
    .name  = "int32",
    .description = "Slot and optional function number, example: 06.0 or 06",
    .print = print_pci_devfn,
    .get   = qdev_propinfo_get_int32,
    .set   = set_pci_devfn,
    .set_default_value = qdev_propinfo_set_default_value_int,
};

/* --- pci host address --- */

static void get_pci_host_devaddr(Object *obj, Visitor *v, const char *name,
                                 void *opaque, Error **errp)
{
    Property *prop = opaque;
    PCIHostDeviceAddress *addr = object_field_prop_ptr(obj, prop);
    char buffer[] = "ffff:ff:ff.f";
    char *p = buffer;
    int rc = 0;

    /*
     * Catch "invalid" device reference from vfio-pci and allow the
     * default buffer representing the non-existent device to be used.
     */
    if (~addr->domain || ~addr->bus || ~addr->slot || ~addr->function) {
        rc = snprintf(buffer, sizeof(buffer), "%04x:%02x:%02x.%0d",
                      addr->domain, addr->bus, addr->slot, addr->function);
        assert(rc == sizeof(buffer) - 1);
    }

    visit_type_str(v, name, &p, errp);
}

/*
 * Parse [<domain>:]<bus>:<slot>.<func>
 *   if <domain> is not supplied, it's assumed to be 0.
 */
static void set_pci_host_devaddr(Object *obj, Visitor *v, const char *name,
                                 void *opaque, Error **errp)
{
    Property *prop = opaque;
    PCIHostDeviceAddress *addr = object_field_prop_ptr(obj, prop);
    char *str, *p;
    char *e;
    unsigned long val;
    unsigned long dom = 0, bus = 0;
    unsigned int slot = 0, func = 0;

    if (!visit_type_str(v, name, &str, errp)) {
        return;
    }

    p = str;
    val = strtoul(p, &e, 16);
    if (e == p || *e != ':') {
        goto inval;
    }
    bus = val;

    p = e + 1;
    val = strtoul(p, &e, 16);
    if (e == p) {
        goto inval;
    }
    if (*e == ':') {
        dom = bus;
        bus = val;
        p = e + 1;
        val = strtoul(p, &e, 16);
        if (e == p) {
            goto inval;
        }
    }
    slot = val;

    if (*e != '.') {
        goto inval;
    }
    p = e + 1;
    val = strtoul(p, &e, 10);
    if (e == p) {
        goto inval;
    }
    func = val;

    if (dom > 0xffff || bus > 0xff || slot > 0x1f || func > 7) {
        goto inval;
    }

    if (*e) {
        goto inval;
    }

    addr->domain = dom;
    addr->bus = bus;
    addr->slot = slot;
    addr->function = func;

    g_free(str);
    return;

inval:
    error_set_from_qdev_prop_error(errp, EINVAL, obj, name, str);
    g_free(str);
}

const PropertyInfo qdev_prop_pci_host_devaddr = {
    .name = "str",
    .description = "Address (bus/device/function) of "
                   "the host device, example: 04:10.0",
    .get = get_pci_host_devaddr,
    .set = set_pci_host_devaddr,
};

/* --- OffAutoPCIBAR off/auto/bar0/bar1/bar2/bar3/bar4/bar5 --- */

const PropertyInfo qdev_prop_off_auto_pcibar = {
    .name = "OffAutoPCIBAR",
    .description = "off/auto/bar0/bar1/bar2/bar3/bar4/bar5",
    .enum_table = &OffAutoPCIBAR_lookup,
    .get = qdev_propinfo_get_enum,
    .set = qdev_propinfo_set_enum,
    .set_default_value = qdev_propinfo_set_default_value_enum,
};

/* --- PCIELinkSpeed 2_5/5/8/16 -- */

static void get_prop_pcielinkspeed(Object *obj, Visitor *v, const char *name,
                                   void *opaque, Error **errp)
{
    Property *prop = opaque;
    PCIExpLinkSpeed *p = object_field_prop_ptr(obj, prop);
    int speed;

    switch (*p) {
    case QEMU_PCI_EXP_LNK_2_5GT:
        speed = PCIE_LINK_SPEED_2_5;
        break;
    case QEMU_PCI_EXP_LNK_5GT:
        speed = PCIE_LINK_SPEED_5;
        break;
    case QEMU_PCI_EXP_LNK_8GT:
        speed = PCIE_LINK_SPEED_8;
        break;
    case QEMU_PCI_EXP_LNK_16GT:
        speed = PCIE_LINK_SPEED_16;
        break;
    default:
        /* Unreachable */
        abort();
    }

    visit_type_enum(v, name, &speed, prop->info->enum_table, errp);
}

static void set_prop_pcielinkspeed(Object *obj, Visitor *v, const char *name,
                                   void *opaque, Error **errp)
{
    Property *prop = opaque;
    PCIExpLinkSpeed *p = object_field_prop_ptr(obj, prop);
    int speed;

    if (!visit_type_enum(v, name, &speed, prop->info->enum_table,
                         errp)) {
        return;
    }

    switch (speed) {
    case PCIE_LINK_SPEED_2_5:
        *p = QEMU_PCI_EXP_LNK_2_5GT;
        break;
    case PCIE_LINK_SPEED_5:
        *p = QEMU_PCI_EXP_LNK_5GT;
        break;
    case PCIE_LINK_SPEED_8:
        *p = QEMU_PCI_EXP_LNK_8GT;
        break;
    case PCIE_LINK_SPEED_16:
        *p = QEMU_PCI_EXP_LNK_16GT;
        break;
    default:
        /* Unreachable */
        abort();
    }
}

const PropertyInfo qdev_prop_pcie_link_speed = {
    .name = "PCIELinkSpeed",
    .description = "2_5/5/8/16",
    .enum_table = &PCIELinkSpeed_lookup,
    .get = get_prop_pcielinkspeed,
    .set = set_prop_pcielinkspeed,
    .set_default_value = qdev_propinfo_set_default_value_enum,
};

/* --- PCIELinkWidth 1/2/4/8/12/16/32 -- */

static void get_prop_pcielinkwidth(Object *obj, Visitor *v, const char *name,
                                   void *opaque, Error **errp)
{
    Property *prop = opaque;
    PCIExpLinkWidth *p = object_field_prop_ptr(obj, prop);
    int width;

    switch (*p) {
    case QEMU_PCI_EXP_LNK_X1:
        width = PCIE_LINK_WIDTH_1;
        break;
    case QEMU_PCI_EXP_LNK_X2:
        width = PCIE_LINK_WIDTH_2;
        break;
    case QEMU_PCI_EXP_LNK_X4:
        width = PCIE_LINK_WIDTH_4;
        break;
    case QEMU_PCI_EXP_LNK_X8:
        width = PCIE_LINK_WIDTH_8;
        break;
    case QEMU_PCI_EXP_LNK_X12:
        width = PCIE_LINK_WIDTH_12;
        break;
    case QEMU_PCI_EXP_LNK_X16:
        width = PCIE_LINK_WIDTH_16;
        break;
    case QEMU_PCI_EXP_LNK_X32:
        width = PCIE_LINK_WIDTH_32;
        break;
    default:
        /* Unreachable */
        abort();
    }

    visit_type_enum(v, name, &width, prop->info->enum_table, errp);
}

static void set_prop_pcielinkwidth(Object *obj, Visitor *v, const char *name,
                                   void *opaque, Error **errp)
{
    Property *prop = opaque;
    PCIExpLinkWidth *p = object_field_prop_ptr(obj, prop);
    int width;

    if (!visit_type_enum(v, name, &width, prop->info->enum_table,
                         errp)) {
        return;
    }

    switch (width) {
    case PCIE_LINK_WIDTH_1:
        *p = QEMU_PCI_EXP_LNK_X1;
        break;
    case PCIE_LINK_WIDTH_2:
        *p = QEMU_PCI_EXP_LNK_X2;
        break;
    case PCIE_LINK_WIDTH_4:
        *p = QEMU_PCI_EXP_LNK_X4;
        break;
    case PCIE_LINK_WIDTH_8:
        *p = QEMU_PCI_EXP_LNK_X8;
        break;
    case PCIE_LINK_WIDTH_12:
        *p = QEMU_PCI_EXP_LNK_X12;
        break;
    case PCIE_LINK_WIDTH_16:
        *p = QEMU_PCI_EXP_LNK_X16;
        break;
    case PCIE_LINK_WIDTH_32:
        *p = QEMU_PCI_EXP_LNK_X32;
        break;
    default:
        /* Unreachable */
        abort();
    }
}

const PropertyInfo qdev_prop_pcie_link_width = {
    .name = "PCIELinkWidth",
    .description = "1/2/4/8/12/16/32",
    .enum_table = &PCIELinkWidth_lookup,
    .get = get_prop_pcielinkwidth,
    .set = set_prop_pcielinkwidth,
    .set_default_value = qdev_propinfo_set_default_value_enum,
};

/* --- UUID --- */

static void get_uuid(Object *obj, Visitor *v, const char *name, void *opaque,
                     Error **errp)
{
    Property *prop = opaque;
    QemuUUID *uuid = object_field_prop_ptr(obj, prop);
    char buffer[UUID_FMT_LEN + 1];
    char *p = buffer;

    qemu_uuid_unparse(uuid, buffer);

    visit_type_str(v, name, &p, errp);
}

#define UUID_VALUE_AUTO        "auto"

static void set_uuid(Object *obj, Visitor *v, const char *name, void *opaque,
                    Error **errp)
{
    Property *prop = opaque;
    QemuUUID *uuid = object_field_prop_ptr(obj, prop);
    char *str;

    if (!visit_type_str(v, name, &str, errp)) {
        return;
    }

    if (!strcmp(str, UUID_VALUE_AUTO)) {
        qemu_uuid_generate(uuid);
    } else if (qemu_uuid_parse(str, uuid) < 0) {
        error_set_from_qdev_prop_error(errp, EINVAL, obj, name, str);
    }
    g_free(str);
}

static void set_default_uuid_auto(ObjectProperty *op, const Property *prop)
{
    object_property_set_default_str(op, UUID_VALUE_AUTO);
}

const PropertyInfo qdev_prop_uuid = {
    .name  = "str",
    .description = "UUID (aka GUID) or \"" UUID_VALUE_AUTO
        "\" for random value (default)",
    .get   = get_uuid,
    .set   = set_uuid,
    .set_default_value = set_default_uuid_auto,
};
