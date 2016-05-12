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
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "monitor/monitor.h"
#include "sysemu/sysemu.h"
#include "qmp-commands.h"
#include "sysemu/char.h"
#include "ui/qemu-spice.h"
#include "ui/vnc.h"
#include "sysemu/kvm.h"
#include "sysemu/arch_init.h"
#include "hw/qdev.h"
#include "sysemu/blockdev.h"
#include "sysemu/block-backend.h"
#include "qom/qom-qobject.h"
#include "qapi/qmp/qerror.h"
#include "qapi/qmp/qobject.h"
#include "qapi/qmp-input-visitor.h"
#include "hw/boards.h"
#include "qom/object_interfaces.h"
#include "hw/mem/pc-dimm.h"
#include "hw/acpi/acpi_dev_interface.h"

NameInfo *qmp_query_name(Error **errp)
{
    NameInfo *info = g_malloc0(sizeof(*info));

    if (qemu_name) {
        info->has_name = true;
        info->name = g_strdup(qemu_name);
    }

    return info;
}

VersionInfo *qmp_query_version(Error **errp)
{
    VersionInfo *info = g_new0(VersionInfo, 1);
    const char *version = QEMU_VERSION;
    const char *tmp;
    int err;

    info->qemu = g_new0(VersionTriple, 1);
    err = qemu_strtoll(version, &tmp, 10, &info->qemu->major);
    assert(err == 0);
    tmp++;

    err = qemu_strtoll(tmp, &tmp, 10, &info->qemu->minor);
    assert(err == 0);
    tmp++;

    err = qemu_strtoll(tmp, &tmp, 10, &info->qemu->micro);
    assert(err == 0);
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

void qmp_quit(Error **errp)
{
    no_shutdown = 0;
    qemu_system_shutdown_request();
}

void qmp_stop(Error **errp)
{
    /* if there is a dump in background, we should wait until the dump
     * finished */
    if (dump_in_progress()) {
        error_setg(errp, "There is a dump in process, please wait.");
        return;
    }

    if (runstate_check(RUN_STATE_INMIGRATE)) {
        autostart = 0;
    } else {
        vm_stop(RUN_STATE_PAUSED);
    }
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

void qmp_cpu_add(int64_t id, Error **errp)
{
    MachineClass *mc;

    mc = MACHINE_GET_CLASS(current_machine);
    if (mc->hot_add_cpu) {
        mc->hot_add_cpu(id, errp);
    } else {
        error_setg(errp, "Not supported");
    }
}

#ifndef CONFIG_VNC
/* If VNC support is enabled, the "true" query-vnc command is
   defined in the VNC subsystem */
VncInfo *qmp_query_vnc(Error **errp)
{
    error_setg(errp, QERR_FEATURE_DISABLED, "vnc");
    return NULL;
};

VncInfo2List *qmp_query_vnc_servers(Error **errp)
{
    error_setg(errp, QERR_FEATURE_DISABLED, "vnc");
    return NULL;
};
#endif

#ifndef CONFIG_SPICE
/*
 * qmp-commands.hx ensures that QMP command query-spice exists only
 * #ifdef CONFIG_SPICE.  Necessary for an accurate query-commands
 * result.  However, the QAPI schema is blissfully unaware of that,
 * and the QAPI code generator happily generates a dead
 * qmp_marshal_query_spice() that calls qmp_query_spice().  Provide it
 * one, or else linking fails.  FIXME Educate the QAPI schema on
 * CONFIG_SPICE.
 */
SpiceInfo *qmp_query_spice(Error **errp)
{
    abort();
};
#endif

void qmp_cont(Error **errp)
{
    Error *local_err = NULL;
    BlockBackend *blk;
    BlockDriverState *bs;

    /* if there is a dump in background, we should wait until the dump
     * finished */
    if (dump_in_progress()) {
        error_setg(errp, "There is a dump in process, please wait.");
        return;
    }

    if (runstate_needs_reset()) {
        error_setg(errp, "Resetting the Virtual Machine is required");
        return;
    } else if (runstate_check(RUN_STATE_SUSPENDED)) {
        return;
    }

    for (blk = blk_next(NULL); blk; blk = blk_next(blk)) {
        blk_iostatus_reset(blk);
    }
    for (bs = bdrv_next(NULL); bs; bs = bdrv_next(bs)) {
        bdrv_add_key(bs, NULL, &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            return;
        }
    }

    /* Continuing after completed migration. Images have been inactivated to
     * allow the destination to take control. Need to get control back now. */
    if (runstate_check(RUN_STATE_FINISH_MIGRATE) ||
        runstate_check(RUN_STATE_POSTMIGRATE))
    {
        bdrv_invalidate_cache_all(&local_err);
        if (local_err) {
            error_propagate(errp, local_err);
            return;
        }
    }

    if (runstate_check(RUN_STATE_INMIGRATE)) {
        autostart = 1;
    } else {
        vm_start();
    }
}

void qmp_system_wakeup(Error **errp)
{
    qemu_system_wakeup_request(QEMU_WAKEUP_REASON_OTHER);
}

ObjectPropertyInfoList *qmp_qom_list(const char *path, Error **errp)
{
    Object *obj;
    bool ambiguous = false;
    ObjectPropertyInfoList *props = NULL;
    ObjectProperty *prop;
    ObjectPropertyIterator iter;

    obj = object_resolve_path(path, &ambiguous);
    if (obj == NULL) {
        if (ambiguous) {
            error_setg(errp, "Path '%s' is ambiguous", path);
        } else {
            error_set(errp, ERROR_CLASS_DEVICE_NOT_FOUND,
                      "Device '%s' not found", path);
        }
        return NULL;
    }

    object_property_iter_init(&iter, obj);
    while ((prop = object_property_iter_next(&iter))) {
        ObjectPropertyInfoList *entry = g_malloc0(sizeof(*entry));

        entry->value = g_malloc0(sizeof(ObjectPropertyInfo));
        entry->next = props;
        props = entry;

        entry->value->name = g_strdup(prop->name);
        entry->value->type = g_strdup(prop->type);
    }

    return props;
}

void qmp_qom_set(const char *path, const char *property, QObject *value,
                 Error **errp)
{
    Object *obj;

    obj = object_resolve_path(path, NULL);
    if (!obj) {
        error_set(errp, ERROR_CLASS_DEVICE_NOT_FOUND,
                  "Device '%s' not found", path);
        return;
    }

    object_property_set_qobject(obj, value, property, errp);
}

QObject *qmp_qom_get(const char *path, const char *property, Error **errp)
{
    Object *obj;

    obj = object_resolve_path(path, NULL);
    if (!obj) {
        error_set(errp, ERROR_CLASS_DEVICE_NOT_FOUND,
                  "Device '%s' not found", path);
        return NULL;
    }

    return object_property_get_qobject(obj, property, errp);
}

void qmp_set_password(const char *protocol, const char *password,
                      bool has_connected, const char *connected, Error **errp)
{
    int disconnect_if_connected = 0;
    int fail_if_connected = 0;
    int rc;

    if (has_connected) {
        if (strcmp(connected, "fail") == 0) {
            fail_if_connected = 1;
        } else if (strcmp(connected, "disconnect") == 0) {
            disconnect_if_connected = 1;
        } else if (strcmp(connected, "keep") == 0) {
            /* nothing */
        } else {
            error_setg(errp, QERR_INVALID_PARAMETER, "connected");
            return;
        }
    }

    if (strcmp(protocol, "spice") == 0) {
        if (!qemu_using_spice(errp)) {
            return;
        }
        rc = qemu_spice_set_passwd(password, fail_if_connected,
                                   disconnect_if_connected);
        if (rc != 0) {
            error_setg(errp, QERR_SET_PASSWD_FAILED);
        }
        return;
    }

    if (strcmp(protocol, "vnc") == 0) {
        if (fail_if_connected || disconnect_if_connected) {
            /* vnc supports "connected=keep" only */
            error_setg(errp, QERR_INVALID_PARAMETER, "connected");
            return;
        }
        /* Note that setting an empty password will not disable login through
         * this interface. */
        rc = vnc_display_password(NULL, password);
        if (rc < 0) {
            error_setg(errp, QERR_SET_PASSWD_FAILED);
        }
        return;
    }

    error_setg(errp, QERR_INVALID_PARAMETER, "protocol");
}

void qmp_expire_password(const char *protocol, const char *whenstr,
                         Error **errp)
{
    time_t when;
    int rc;

    if (strcmp(whenstr, "now") == 0) {
        when = 0;
    } else if (strcmp(whenstr, "never") == 0) {
        when = TIME_MAX;
    } else if (whenstr[0] == '+') {
        when = time(NULL) + strtoull(whenstr+1, NULL, 10);
    } else {
        when = strtoull(whenstr, NULL, 10);
    }

    if (strcmp(protocol, "spice") == 0) {
        if (!qemu_using_spice(errp)) {
            return;
        }
        rc = qemu_spice_set_pw_expire(when);
        if (rc != 0) {
            error_setg(errp, QERR_SET_PASSWD_FAILED);
        }
        return;
    }

    if (strcmp(protocol, "vnc") == 0) {
        rc = vnc_display_pw_expire(NULL, when);
        if (rc != 0) {
            error_setg(errp, QERR_SET_PASSWD_FAILED);
        }
        return;
    }

    error_setg(errp, QERR_INVALID_PARAMETER, "protocol");
}

#ifdef CONFIG_VNC
void qmp_change_vnc_password(const char *password, Error **errp)
{
    if (vnc_display_password(NULL, password) < 0) {
        error_setg(errp, QERR_SET_PASSWD_FAILED);
    }
}

static void qmp_change_vnc_listen(const char *target, Error **errp)
{
    QemuOptsList *olist = qemu_find_opts("vnc");
    QemuOpts *opts;

    if (strstr(target, "id=")) {
        error_setg(errp, "id not supported");
        return;
    }

    opts = qemu_opts_find(olist, "default");
    if (opts) {
        qemu_opts_del(opts);
    }
    opts = vnc_parse(target, errp);
    if (!opts) {
        return;
    }

    vnc_display_open("default", errp);
}

static void qmp_change_vnc(const char *target, bool has_arg, const char *arg,
                           Error **errp)
{
    if (strcmp(target, "passwd") == 0 || strcmp(target, "password") == 0) {
        if (!has_arg) {
            error_setg(errp, QERR_MISSING_PARAMETER, "password");
        } else {
            qmp_change_vnc_password(arg, errp);
        }
    } else {
        qmp_change_vnc_listen(target, errp);
    }
}
#else
void qmp_change_vnc_password(const char *password, Error **errp)
{
    error_setg(errp, QERR_FEATURE_DISABLED, "vnc");
}
static void qmp_change_vnc(const char *target, bool has_arg, const char *arg,
                           Error **errp)
{
    error_setg(errp, QERR_FEATURE_DISABLED, "vnc");
}
#endif /* !CONFIG_VNC */

void qmp_change(const char *device, const char *target,
                bool has_arg, const char *arg, Error **errp)
{
    if (strcmp(device, "vnc") == 0) {
        qmp_change_vnc(target, has_arg, arg, errp);
    } else {
        qmp_blockdev_change_medium(device, target, has_arg, arg, false, 0,
                                   errp);
    }
}

static void qom_list_types_tramp(ObjectClass *klass, void *data)
{
    ObjectTypeInfoList *e, **pret = data;
    ObjectTypeInfo *info;

    info = g_malloc0(sizeof(*info));
    info->name = g_strdup(object_class_get_name(klass));

    e = g_malloc0(sizeof(*e));
    e->value = info;
    e->next = *pret;
    *pret = e;
}

ObjectTypeInfoList *qmp_qom_list_types(bool has_implements,
                                       const char *implements,
                                       bool has_abstract,
                                       bool abstract,
                                       Error **errp)
{
    ObjectTypeInfoList *ret = NULL;

    object_class_foreach(qom_list_types_tramp, implements, abstract, &ret);

    return ret;
}

/* Return a DevicePropertyInfo for a qdev property.
 *
 * If a qdev property with the given name does not exist, use the given default
 * type.  If the qdev property info should not be shown, return NULL.
 *
 * The caller must free the return value.
 */
static DevicePropertyInfo *make_device_property_info(ObjectClass *klass,
                                                     const char *name,
                                                     const char *default_type,
                                                     const char *description)
{
    DevicePropertyInfo *info;
    Property *prop;

    do {
        for (prop = DEVICE_CLASS(klass)->props; prop && prop->name; prop++) {
            if (strcmp(name, prop->name) != 0) {
                continue;
            }

            /*
             * TODO Properties without a parser are just for dirty hacks.
             * qdev_prop_ptr is the only such PropertyInfo.  It's marked
             * for removal.  This conditional should be removed along with
             * it.
             */
            if (!prop->info->set) {
                return NULL;           /* no way to set it, don't show */
            }

            info = g_malloc0(sizeof(*info));
            info->name = g_strdup(prop->name);
            info->type = g_strdup(prop->info->name);
            info->has_description = !!prop->info->description;
            info->description = g_strdup(prop->info->description);
            return info;
        }
        klass = object_class_get_parent(klass);
    } while (klass != object_class_by_name(TYPE_DEVICE));

    /* Not a qdev property, use the default type */
    info = g_malloc0(sizeof(*info));
    info->name = g_strdup(name);
    info->type = g_strdup(default_type);
    info->has_description = !!description;
    info->description = g_strdup(description);

    return info;
}

DevicePropertyInfoList *qmp_device_list_properties(const char *typename,
                                                   Error **errp)
{
    ObjectClass *klass;
    Object *obj;
    ObjectProperty *prop;
    ObjectPropertyIterator iter;
    DevicePropertyInfoList *prop_list = NULL;

    klass = object_class_by_name(typename);
    if (klass == NULL) {
        error_set(errp, ERROR_CLASS_DEVICE_NOT_FOUND,
                  "Device '%s' not found", typename);
        return NULL;
    }

    klass = object_class_dynamic_cast(klass, TYPE_DEVICE);
    if (klass == NULL) {
        error_setg(errp, QERR_INVALID_PARAMETER_VALUE, "name", TYPE_DEVICE);
        return NULL;
    }

    if (object_class_is_abstract(klass)) {
        error_setg(errp, QERR_INVALID_PARAMETER_VALUE, "name",
                   "non-abstract device type");
        return NULL;
    }

    if (DEVICE_CLASS(klass)->cannot_destroy_with_object_finalize_yet) {
        error_setg(errp, "Can't list properties of device '%s'", typename);
        return NULL;
    }

    obj = object_new(typename);

    object_property_iter_init(&iter, obj);
    while ((prop = object_property_iter_next(&iter))) {
        DevicePropertyInfo *info;
        DevicePropertyInfoList *entry;

        /* Skip Object and DeviceState properties */
        if (strcmp(prop->name, "type") == 0 ||
            strcmp(prop->name, "realized") == 0 ||
            strcmp(prop->name, "hotpluggable") == 0 ||
            strcmp(prop->name, "hotplugged") == 0 ||
            strcmp(prop->name, "parent_bus") == 0) {
            continue;
        }

        /* Skip legacy properties since they are just string versions of
         * properties that we already list.
         */
        if (strstart(prop->name, "legacy-", NULL)) {
            continue;
        }

        info = make_device_property_info(klass, prop->name, prop->type,
                                         prop->description);
        if (!info) {
            continue;
        }

        entry = g_malloc0(sizeof(*entry));
        entry->value = info;
        entry->next = prop_list;
        prop_list = entry;
    }

    object_unref(obj);

    return prop_list;
}

CpuDefinitionInfoList *qmp_query_cpu_definitions(Error **errp)
{
    return arch_query_cpu_definitions(errp);
}

void qmp_add_client(const char *protocol, const char *fdname,
                    bool has_skipauth, bool skipauth, bool has_tls, bool tls,
                    Error **errp)
{
    CharDriverState *s;
    int fd;

    fd = monitor_get_fd(cur_mon, fdname, errp);
    if (fd < 0) {
        return;
    }

    if (strcmp(protocol, "spice") == 0) {
        if (!qemu_using_spice(errp)) {
            close(fd);
            return;
        }
        skipauth = has_skipauth ? skipauth : false;
        tls = has_tls ? tls : false;
        if (qemu_spice_display_add_client(fd, skipauth, tls) < 0) {
            error_setg(errp, "spice failed to add client");
            close(fd);
        }
        return;
#ifdef CONFIG_VNC
    } else if (strcmp(protocol, "vnc") == 0) {
        skipauth = has_skipauth ? skipauth : false;
        vnc_display_add_client(NULL, fd, skipauth);
        return;
#endif
    } else if ((s = qemu_chr_find(protocol)) != NULL) {
        if (qemu_chr_add_client(s, fd) < 0) {
            error_setg(errp, "failed to add client");
            close(fd);
            return;
        }
        return;
    }

    error_setg(errp, "protocol '%s' is invalid", protocol);
    close(fd);
}


void qmp_object_add(const char *type, const char *id,
                    bool has_props, QObject *props, Error **errp)
{
    const QDict *pdict = NULL;
    QmpInputVisitor *qiv;
    Object *obj;

    if (props) {
        pdict = qobject_to_qdict(props);
        if (!pdict) {
            error_setg(errp, QERR_INVALID_PARAMETER_TYPE, "props", "dict");
            return;
        }
    }

    qiv = qmp_input_visitor_new(props, true);
    obj = user_creatable_add_type(type, id, pdict,
                                  qmp_input_get_visitor(qiv), errp);
    qmp_input_visitor_cleanup(qiv);
    if (obj) {
        object_unref(obj);
    }
}

void qmp_object_del(const char *id, Error **errp)
{
    user_creatable_del(id, errp);
}

MemoryDeviceInfoList *qmp_query_memory_devices(Error **errp)
{
    MemoryDeviceInfoList *head = NULL;
    MemoryDeviceInfoList **prev = &head;

    qmp_pc_dimm_device_list(qdev_get_machine(), &prev);

    return head;
}

ACPIOSTInfoList *qmp_query_acpi_ospm_status(Error **errp)
{
    bool ambig;
    ACPIOSTInfoList *head = NULL;
    ACPIOSTInfoList **prev = &head;
    Object *obj = object_resolve_path_type("", TYPE_ACPI_DEVICE_IF, &ambig);

    if (obj) {
        AcpiDeviceIfClass *adevc = ACPI_DEVICE_IF_GET_CLASS(obj);
        AcpiDeviceIf *adev = ACPI_DEVICE_IF(obj);

        adevc->ospm_status(adev, &prev);
    } else {
        error_setg(errp, "command is not supported, missing ACPI device");
    }

    return head;
}
