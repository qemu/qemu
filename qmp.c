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

#include "qemu-common.h"
#include "sysemu/sysemu.h"
#include "qmp-commands.h"
#include "sysemu/char.h"
#include "ui/qemu-spice.h"
#include "ui/vnc.h"
#include "sysemu/kvm.h"
#include "sysemu/arch_init.h"
#include "hw/qdev.h"
#include "sysemu/blockdev.h"
#include "qom/qom-qobject.h"
#include "qapi/qmp/qobject.h"
#include "qapi/qmp-input-visitor.h"
#include "hw/boards.h"
#include "qom/object_interfaces.h"

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

void qmp_quit(Error **errp)
{
    no_shutdown = 0;
    qemu_system_shutdown_request();
}

void qmp_stop(Error **errp)
{
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

void qmp_cont(Error **errp)
{
    BlockDriverState *bs;

    if (runstate_needs_reset()) {
        error_setg(errp, "Resetting the Virtual Machine is required");
        return;
    } else if (runstate_check(RUN_STATE_SUSPENDED)) {
        return;
    }

    for (bs = bdrv_next(NULL); bs; bs = bdrv_next(bs)) {
        bdrv_iostatus_reset(bs);
    }
    for (bs = bdrv_next(NULL); bs; bs = bdrv_next(bs)) {
        if (bdrv_key_required(bs)) {
            error_set(errp, QERR_DEVICE_ENCRYPTED,
                      bdrv_get_device_name(bs),
                      bdrv_get_encrypted_filename(bs));
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

    obj = object_resolve_path(path, &ambiguous);
    if (obj == NULL) {
        if (ambiguous) {
            error_setg(errp, "Path '%s' is ambiguous", path);
        } else {
            error_set(errp, QERR_DEVICE_NOT_FOUND, path);
        }
        return NULL;
    }

    QTAILQ_FOREACH(prop, &obj->properties, node) {
        ObjectPropertyInfoList *entry = g_malloc0(sizeof(*entry));

        entry->value = g_malloc0(sizeof(ObjectPropertyInfo));
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
    Object *obj;

    obj = object_resolve_path(path, NULL);
    if (!obj) {
        error_set(&local_err, QERR_DEVICE_NOT_FOUND, path);
        goto out;
    }

    object_property_set_qobject(obj, value, property, &local_err);

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
    Object *obj;

    obj = object_resolve_path(path, NULL);
    if (!obj) {
        error_set(&local_err, QERR_DEVICE_NOT_FOUND, path);
        goto out;
    }

    *ret = object_property_get_qobject(obj, property, &local_err);

out:
    if (local_err) {
        qerror_report_err(local_err);
        error_free(local_err);
        return -1;
    }

    return 0;
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
            error_set(errp, QERR_INVALID_PARAMETER, "connected");
            return;
        }
    }

    if (strcmp(protocol, "spice") == 0) {
        if (!using_spice) {
            /* correct one? spice isn't a device ,,, */
            error_set(errp, QERR_DEVICE_NOT_ACTIVE, "spice");
            return;
        }
        rc = qemu_spice_set_passwd(password, fail_if_connected,
                                   disconnect_if_connected);
        if (rc != 0) {
            error_set(errp, QERR_SET_PASSWD_FAILED);
        }
        return;
    }

    if (strcmp(protocol, "vnc") == 0) {
        if (fail_if_connected || disconnect_if_connected) {
            /* vnc supports "connected=keep" only */
            error_set(errp, QERR_INVALID_PARAMETER, "connected");
            return;
        }
        /* Note that setting an empty password will not disable login through
         * this interface. */
        rc = vnc_display_password(NULL, password);
        if (rc < 0) {
            error_set(errp, QERR_SET_PASSWD_FAILED);
        }
        return;
    }

    error_set(errp, QERR_INVALID_PARAMETER, "protocol");
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
        if (!using_spice) {
            /* correct one? spice isn't a device ,,, */
            error_set(errp, QERR_DEVICE_NOT_ACTIVE, "spice");
            return;
        }
        rc = qemu_spice_set_pw_expire(when);
        if (rc != 0) {
            error_set(errp, QERR_SET_PASSWD_FAILED);
        }
        return;
    }

    if (strcmp(protocol, "vnc") == 0) {
        rc = vnc_display_pw_expire(NULL, when);
        if (rc != 0) {
            error_set(errp, QERR_SET_PASSWD_FAILED);
        }
        return;
    }

    error_set(errp, QERR_INVALID_PARAMETER, "protocol");
}

#ifdef CONFIG_VNC
void qmp_change_vnc_password(const char *password, Error **errp)
{
    if (vnc_display_password(NULL, password) < 0) {
        error_set(errp, QERR_SET_PASSWD_FAILED);
    }
}

static void qmp_change_vnc_listen(const char *target, Error **errp)
{
    vnc_display_open(NULL, target, errp);
}

static void qmp_change_vnc(const char *target, bool has_arg, const char *arg,
                           Error **errp)
{
    if (strcmp(target, "passwd") == 0 || strcmp(target, "password") == 0) {
        if (!has_arg) {
            error_set(errp, QERR_MISSING_PARAMETER, "password");
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
    error_set(errp, QERR_FEATURE_DISABLED, "vnc");
}
static void qmp_change_vnc(const char *target, bool has_arg, const char *arg,
                           Error **errp)
{
    error_set(errp, QERR_FEATURE_DISABLED, "vnc");
}
#endif /* !CONFIG_VNC */

void qmp_change(const char *device, const char *target,
                bool has_arg, const char *arg, Error **errp)
{
    if (strcmp(device, "vnc") == 0) {
        qmp_change_vnc(target, has_arg, arg, errp);
    } else {
        qmp_change_blockdev(device, target, arg, errp);
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

DevicePropertyInfoList *qmp_device_list_properties(const char *typename,
                                                   Error **errp)
{
    ObjectClass *klass;
    Property *prop;
    DevicePropertyInfoList *prop_list = NULL;

    klass = object_class_by_name(typename);
    if (klass == NULL) {
        error_set(errp, QERR_DEVICE_NOT_FOUND, typename);
        return NULL;
    }

    klass = object_class_dynamic_cast(klass, TYPE_DEVICE);
    if (klass == NULL) {
        error_set(errp, QERR_INVALID_PARAMETER_VALUE,
                  "name", TYPE_DEVICE);
        return NULL;
    }

    do {
        for (prop = DEVICE_CLASS(klass)->props; prop && prop->name; prop++) {
            DevicePropertyInfoList *entry;
            DevicePropertyInfo *info;

            /*
             * TODO Properties without a parser are just for dirty hacks.
             * qdev_prop_ptr is the only such PropertyInfo.  It's marked
             * for removal.  This conditional should be removed along with
             * it.
             */
            if (!prop->info->set) {
                continue;           /* no way to set it, don't show */
            }

            info = g_malloc0(sizeof(*info));
            info->name = g_strdup(prop->name);
            info->type = g_strdup(prop->info->legacy_name ?: prop->info->name);

            entry = g_malloc0(sizeof(*entry));
            entry->value = info;
            entry->next = prop_list;
            prop_list = entry;
        }
        klass = object_class_get_parent(klass);
    } while (klass != object_class_by_name(TYPE_DEVICE));

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
        if (!using_spice) {
            error_set(errp, QERR_DEVICE_NOT_ACTIVE, "spice");
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

void object_add(const char *type, const char *id, const QDict *qdict,
                Visitor *v, Error **errp)
{
    Object *obj;
    ObjectClass *klass;
    const QDictEntry *e;
    Error *local_err = NULL;

    klass = object_class_by_name(type);
    if (!klass) {
        error_setg(errp, "invalid class name");
        return;
    }

    if (!object_class_dynamic_cast(klass, TYPE_USER_CREATABLE)) {
        error_setg(errp, "object type '%s' isn't supported by object-add",
                   type);
        return;
    }

    if (object_class_is_abstract(klass)) {
        error_setg(errp, "object type '%s' is abstract", type);
        return;
    }

    obj = object_new(type);
    if (qdict) {
        for (e = qdict_first(qdict); e; e = qdict_next(qdict, e)) {
            object_property_set(obj, v, e->key, &local_err);
            if (local_err) {
                goto out;
            }
        }
    }

    object_property_add_child(container_get(object_get_root(), "/objects"),
                              id, obj, &local_err);
    if (local_err) {
        goto out;
    }

    user_creatable_complete(obj, &local_err);
    if (local_err) {
        object_property_del(container_get(object_get_root(), "/objects"),
                            id, &error_abort);
        goto out;
    }
out:
    if (local_err) {
        error_propagate(errp, local_err);
    }
    object_unref(obj);
}

int qmp_object_add(Monitor *mon, const QDict *qdict, QObject **ret)
{
    const char *type = qdict_get_str(qdict, "qom-type");
    const char *id = qdict_get_str(qdict, "id");
    QObject *props = qdict_get(qdict, "props");
    const QDict *pdict = NULL;
    Error *local_err = NULL;
    QmpInputVisitor *qiv;

    if (props) {
        pdict = qobject_to_qdict(props);
        if (!pdict) {
            error_set(&local_err, QERR_INVALID_PARAMETER_TYPE, "props", "dict");
            goto out;
        }
    }

    qiv = qmp_input_visitor_new(props);
    object_add(type, id, pdict, qmp_input_get_visitor(qiv), &local_err);
    qmp_input_visitor_cleanup(qiv);

out:
    if (local_err) {
        qerror_report_err(local_err);
        error_free(local_err);
        return -1;
    }

    return 0;
}

void qmp_object_del(const char *id, Error **errp)
{
    Object *container;
    Object *obj;

    container = container_get(object_get_root(), "/objects");
    obj = object_resolve_path_component(container, id);
    if (!obj) {
        error_setg(errp, "object id not found");
        return;
    }
    object_unparent(obj);
}
