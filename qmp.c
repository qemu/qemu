/*
 * QEMU Management Protocol
 *
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#include "qemu-common.h"
#include "sysemu.h"
#include "qmp-commands.h"
#include "kvm.h"
#include "arch_init.h"
#include "hw/qdev.h"
#include "qapi/qmp-input-visitor.h"
#include "qapi/qmp-output-visitor.h"

NameInfo *qmp_query_name(Error **errp)
{
    NameInfo *info = g_malloc0(sizeof(*info));

    if (qemu_name) {
        info->has_name = true;
        info->name = g_strdup(qemu_name);
    }

    return info;
}

VersionInfo *qmp_query_version(Error **err)
{
    VersionInfo *info = g_malloc0(sizeof(*info));
    const char *version = QEMU_VERSION;
    char *tmp;

    info->qemu.major = strtol(version, &tmp, 10);
    tmp++;
    info->qemu.minor = strtol(tmp, &tmp, 10);
    tmp++;
    info->qemu.micro = strtol(tmp, &tmp, 10);
    info->package = g_strdup(QEMU_PKGVERSION);

    return info;
}

KvmInfo *qmp_query_kvm(Error **errp)
{
    KvmInfo *info = g_malloc0(sizeof(*info));

    info->enabled = kvm_enabled();
    info->present = kvm_available();

    return info;
}

UuidInfo *qmp_query_uuid(Error **errp)
{
    UuidInfo *info = g_malloc0(sizeof(*info));
    char uuid[64];

    snprintf(uuid, sizeof(uuid), UUID_FMT, qemu_uuid[0], qemu_uuid[1],
                   qemu_uuid[2], qemu_uuid[3], qemu_uuid[4], qemu_uuid[5],
                   qemu_uuid[6], qemu_uuid[7], qemu_uuid[8], qemu_uuid[9],
                   qemu_uuid[10], qemu_uuid[11], qemu_uuid[12], qemu_uuid[13],
                   qemu_uuid[14], qemu_uuid[15]);

    info->UUID = g_strdup(uuid);
    return info;
}

void qmp_quit(Error **err)
{
    no_shutdown = 0;
    qemu_system_shutdown_request();
}

void qmp_stop(Error **errp)
{
    vm_stop(RUN_STATE_PAUSED);
}

void qmp_system_reset(Error **errp)
{
    qemu_system_reset_request();
}

void qmp_system_powerdown(Error **erp)
{
    qemu_system_powerdown_request();
}

void qmp_cpu(int64_t index, Error **errp)
{
    /* Just do nothing */
}

#ifndef CONFIG_VNC
/* If VNC support is enabled, the "true" query-vnc command is
   defined in the VNC subsystem */
VncInfo *qmp_query_vnc(Error **errp)
{
    error_set(errp, QERR_FEATURE_DISABLED, "vnc");
    return NULL;
};
#endif

#ifndef CONFIG_SPICE
/* If SPICE support is enabled, the "true" query-spice command is
   defined in the SPICE subsystem. Also note that we use a small
   trick to maintain query-spice's original behavior, which is not
   to be available in the namespace if SPICE is not compiled in */
SpiceInfo *qmp_query_spice(Error **errp)
{
    error_set(errp, QERR_COMMAND_NOT_FOUND, "query-spice");
    return NULL;
};
#endif

static void iostatus_bdrv_it(void *opaque, BlockDriverState *bs)
{
    bdrv_iostatus_reset(bs);
}

static void encrypted_bdrv_it(void *opaque, BlockDriverState *bs)
{
    Error **err = opaque;

    if (!error_is_set(err) && bdrv_key_required(bs)) {
        error_set(err, QERR_DEVICE_ENCRYPTED, bdrv_get_device_name(bs));
    }
}

void qmp_cont(Error **errp)
{
    Error *local_err = NULL;

    if (runstate_check(RUN_STATE_INMIGRATE)) {
        error_set(errp, QERR_MIGRATION_EXPECTED);
        return;
    } else if (runstate_check(RUN_STATE_INTERNAL_ERROR) ||
               runstate_check(RUN_STATE_SHUTDOWN)) {
        error_set(errp, QERR_RESET_REQUIRED);
        return;
    }

    bdrv_iterate(iostatus_bdrv_it, NULL);
    bdrv_iterate(encrypted_bdrv_it, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    vm_start();
}

DevicePropertyInfoList *qmp_qom_list(const char *path, Error **errp)
{
    DeviceState *dev;
    bool ambiguous = false;
    DevicePropertyInfoList *props = NULL;
    DeviceProperty *prop;

    dev = qdev_resolve_path(path, &ambiguous);
    if (dev == NULL) {
        error_set(errp, QERR_DEVICE_NOT_FOUND, path);
        return NULL;
    }

    QTAILQ_FOREACH(prop, &dev->properties, node) {
        DevicePropertyInfoList *entry = g_malloc0(sizeof(*entry));

        entry->value = g_malloc0(sizeof(DevicePropertyInfo));
        entry->next = props;
        props = entry;

        entry->value->name = g_strdup(prop->name);
        entry->value->type = g_strdup(prop->type);
    }

    return props;
}

/* FIXME: teach qapi about how to pass through Visitors */
int qmp_qom_set(Monitor *mon, const QDict *qdict, QObject **ret)
{
    const char *path = qdict_get_str(qdict, "path");
    const char *property = qdict_get_str(qdict, "property");
    QObject *value = qdict_get(qdict, "value");
    Error *local_err = NULL;
    QmpInputVisitor *mi;
    DeviceState *dev;

    dev = qdev_resolve_path(path, NULL);
    if (!dev) {
        error_set(&local_err, QERR_DEVICE_NOT_FOUND, path);
        goto out;
    }

    mi = qmp_input_visitor_new(value);
    qdev_property_set(dev, qmp_input_get_visitor(mi), property, &local_err);

    qmp_input_visitor_cleanup(mi);

out:
    if (local_err) {
        qerror_report_err(local_err);
        error_free(local_err);
        return -1;
    }

    return 0;
}

int qmp_qom_get(Monitor *mon, const QDict *qdict, QObject **ret)
{
    const char *path = qdict_get_str(qdict, "path");
    const char *property = qdict_get_str(qdict, "property");
    Error *local_err = NULL;
    QmpOutputVisitor *mo;
    DeviceState *dev;

    dev = qdev_resolve_path(path, NULL);
    if (!dev) {
        error_set(&local_err, QERR_DEVICE_NOT_FOUND, path);
        goto out;
    }

    mo = qmp_output_visitor_new();
    qdev_property_get(dev, qmp_output_get_visitor(mo), property, &local_err);
    if (!local_err) {
        *ret = qmp_output_get_qobject(mo);
    }

    qmp_output_visitor_cleanup(mo);

out:
    if (local_err) {
        qerror_report_err(local_err);
        error_free(local_err);
        return -1;
    }

    return 0;
}
