/*
 * QEMU access control list file authorization driver
 *
 * Copyright (c) 2018 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "qemu/osdep.h"
#include "authz/listfile.h"
#include "trace.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "qemu/module.h"
#include "qemu/sockets.h"
#include "qemu/filemonitor.h"
#include "qom/object_interfaces.h"
#include "qapi/qapi-visit-authz.h"
#include "qapi/qmp/qjson.h"
#include "qapi/qmp/qobject.h"
#include "qapi/qmp/qerror.h"
#include "qapi/qobject-input-visitor.h"


static bool
qauthz_list_file_is_allowed(QAuthZ *authz,
                            const char *identity,
                            Error **errp)
{
    QAuthZListFile *fauthz = QAUTHZ_LIST_FILE(authz);
    if (fauthz->list) {
        return qauthz_is_allowed(fauthz->list, identity, errp);
    }

    return false;
}


static QAuthZ *
qauthz_list_file_load(QAuthZListFile *fauthz, Error **errp)
{
    GError *err = NULL;
    gchar *content = NULL;
    gsize len;
    QObject *obj = NULL;
    QDict *pdict;
    Visitor *v = NULL;
    QAuthZ *ret = NULL;

    trace_qauthz_list_file_load(fauthz, fauthz->filename);
    if (!g_file_get_contents(fauthz->filename, &content, &len, &err)) {
        error_setg(errp, "Unable to read '%s': %s",
                   fauthz->filename, err->message);
        goto cleanup;
    }

    obj = qobject_from_json(content, errp);
    if (!obj) {
        goto cleanup;
    }

    pdict = qobject_to(QDict, obj);
    if (!pdict) {
        error_setg(errp, QERR_INVALID_PARAMETER_TYPE, "obj", "dict");
        goto cleanup;
    }

    v = qobject_input_visitor_new(obj);

    ret = (QAuthZ *)user_creatable_add_type(TYPE_QAUTHZ_LIST,
                                            NULL, pdict, v, errp);

 cleanup:
    visit_free(v);
    qobject_unref(obj);
    if (err) {
        g_error_free(err);
    }
    g_free(content);
    return ret;
}


static void
qauthz_list_file_event(int64_t wd G_GNUC_UNUSED,
                       QFileMonitorEvent ev G_GNUC_UNUSED,
                       const char *name G_GNUC_UNUSED,
                       void *opaque)
{
    QAuthZListFile *fauthz = opaque;
    Error *err = NULL;

    if (ev != QFILE_MONITOR_EVENT_MODIFIED &&
        ev != QFILE_MONITOR_EVENT_CREATED) {
        return;
    }

    object_unref(OBJECT(fauthz->list));
    fauthz->list = qauthz_list_file_load(fauthz, &err);
    trace_qauthz_list_file_refresh(fauthz,
                                   fauthz->filename, fauthz->list ? 1 : 0);
    if (!fauthz->list) {
        error_report_err(err);
    }
}

static void
qauthz_list_file_complete(UserCreatable *uc, Error **errp)
{
    QAuthZListFile *fauthz = QAUTHZ_LIST_FILE(uc);
    gchar *dir = NULL, *file = NULL;

    fauthz->list = qauthz_list_file_load(fauthz, errp);

    if (!fauthz->refresh) {
        return;
    }

    fauthz->file_monitor = qemu_file_monitor_new(errp);
    if (!fauthz->file_monitor) {
        return;
    }

    dir = g_path_get_dirname(fauthz->filename);
    if (g_str_equal(dir, ".")) {
        error_setg(errp, "Filename must be an absolute path");
        goto cleanup;
    }
    file = g_path_get_basename(fauthz->filename);
    if (g_str_equal(file, ".")) {
        error_setg(errp, "Path has no trailing filename component");
        goto cleanup;
    }

    fauthz->file_watch = qemu_file_monitor_add_watch(
        fauthz->file_monitor, dir, file,
        qauthz_list_file_event, fauthz, errp);
    if (fauthz->file_watch < 0) {
        goto cleanup;
    }

 cleanup:
    g_free(file);
    g_free(dir);
}


static void
qauthz_list_file_prop_set_filename(Object *obj,
                                   const char *value,
                                   Error **errp G_GNUC_UNUSED)
{
    QAuthZListFile *fauthz = QAUTHZ_LIST_FILE(obj);

    g_free(fauthz->filename);
    fauthz->filename = g_strdup(value);
}


static char *
qauthz_list_file_prop_get_filename(Object *obj,
                                   Error **errp G_GNUC_UNUSED)
{
    QAuthZListFile *fauthz = QAUTHZ_LIST_FILE(obj);

    return g_strdup(fauthz->filename);
}


static void
qauthz_list_file_prop_set_refresh(Object *obj,
                                  bool value,
                                  Error **errp G_GNUC_UNUSED)
{
    QAuthZListFile *fauthz = QAUTHZ_LIST_FILE(obj);

    fauthz->refresh = value;
}


static bool
qauthz_list_file_prop_get_refresh(Object *obj,
                                  Error **errp G_GNUC_UNUSED)
{
    QAuthZListFile *fauthz = QAUTHZ_LIST_FILE(obj);

    return fauthz->refresh;
}


static void
qauthz_list_file_finalize(Object *obj)
{
    QAuthZListFile *fauthz = QAUTHZ_LIST_FILE(obj);

    object_unref(OBJECT(fauthz->list));
    g_free(fauthz->filename);
    qemu_file_monitor_free(fauthz->file_monitor);
}


static void
qauthz_list_file_class_init(ObjectClass *oc, void *data)
{
    UserCreatableClass *ucc = USER_CREATABLE_CLASS(oc);
    QAuthZClass *authz = QAUTHZ_CLASS(oc);

    ucc->complete = qauthz_list_file_complete;

    object_class_property_add_str(oc, "filename",
                                  qauthz_list_file_prop_get_filename,
                                  qauthz_list_file_prop_set_filename,
                                  NULL);
    object_class_property_add_bool(oc, "refresh",
                                   qauthz_list_file_prop_get_refresh,
                                   qauthz_list_file_prop_set_refresh,
                                   NULL);

    authz->is_allowed = qauthz_list_file_is_allowed;
}


static void
qauthz_list_file_init(Object *obj)
{
    QAuthZListFile *authz = QAUTHZ_LIST_FILE(obj);

    authz->file_watch = -1;
#ifdef CONFIG_INOTIFY1
    authz->refresh = TRUE;
#endif
}


QAuthZListFile *qauthz_list_file_new(const char *id,
                                     const char *filename,
                                     bool refresh,
                                     Error **errp)
{
    return QAUTHZ_LIST_FILE(
        object_new_with_props(TYPE_QAUTHZ_LIST_FILE,
                              object_get_objects_root(),
                              id, errp,
                              "filename", filename,
                              "refresh", refresh ? "yes" : "no",
                              NULL));
}


static const TypeInfo qauthz_list_file_info = {
    .parent = TYPE_QAUTHZ,
    .name = TYPE_QAUTHZ_LIST_FILE,
    .instance_init = qauthz_list_file_init,
    .instance_size = sizeof(QAuthZListFile),
    .instance_finalize = qauthz_list_file_finalize,
    .class_size = sizeof(QAuthZListFileClass),
    .class_init = qauthz_list_file_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_USER_CREATABLE },
        { }
    }
};


static void
qauthz_list_file_register_types(void)
{
    type_register_static(&qauthz_list_file_info);
}


type_init(qauthz_list_file_register_types);
