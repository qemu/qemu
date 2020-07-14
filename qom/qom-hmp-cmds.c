/*
 * HMP commands related to QOM
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "hw/qdev-core.h"
#include "monitor/hmp.h"
#include "monitor/monitor.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-qom.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qjson.h"
#include "qapi/qmp/qstring.h"
#include "qom/object.h"

void hmp_qom_list(Monitor *mon, const QDict *qdict)
{
    const char *path = qdict_get_try_str(qdict, "path");
    ObjectPropertyInfoList *list;
    Error *err = NULL;

    if (path == NULL) {
        monitor_printf(mon, "/\n");
        return;
    }

    list = qmp_qom_list(path, &err);
    if (err == NULL) {
        ObjectPropertyInfoList *start = list;
        while (list != NULL) {
            ObjectPropertyInfo *value = list->value;

            monitor_printf(mon, "%s (%s)\n",
                           value->name, value->type);
            list = list->next;
        }
        qapi_free_ObjectPropertyInfoList(start);
    }
    hmp_handle_error(mon, err);
}

void hmp_qom_set(Monitor *mon, const QDict *qdict)
{
    const bool json = qdict_get_try_bool(qdict, "json", false);
    const char *path = qdict_get_str(qdict, "path");
    const char *property = qdict_get_str(qdict, "property");
    const char *value = qdict_get_str(qdict, "value");
    Error *err = NULL;

    if (!json) {
        Object *obj = object_resolve_path(path, NULL);

        if (!obj) {
            error_set(&err, ERROR_CLASS_DEVICE_NOT_FOUND,
                      "Device '%s' not found", path);
        } else {
            object_property_parse(obj, property, value, &err);
        }
    } else {
        QObject *obj = qobject_from_json(value, &err);

        if (!err) {
            qmp_qom_set(path, property, obj, &err);
        }
    }

    hmp_handle_error(mon, err);
}

void hmp_qom_get(Monitor *mon, const QDict *qdict)
{
    const char *path = qdict_get_str(qdict, "path");
    const char *property = qdict_get_str(qdict, "property");
    Error *err = NULL;
    QObject *obj = qmp_qom_get(path, property, &err);

    if (err == NULL) {
        QString *str = qobject_to_json_pretty(obj);
        monitor_printf(mon, "%s\n", qstring_get_str(str));
        qobject_unref(str);
    }

    qobject_unref(obj);
    hmp_handle_error(mon, err);
}

typedef struct QOMCompositionState {
    Monitor *mon;
    int indent;
} QOMCompositionState;

static void print_qom_composition(Monitor *mon, Object *obj, int indent);

static int qom_composition_compare(const void *a, const void *b)
{
    return g_strcmp0(object_get_canonical_path_component(*(Object **)a),
                     object_get_canonical_path_component(*(Object **)b));
}

static int insert_qom_composition_child(Object *obj, void *opaque)
{
    g_array_append_val(opaque, obj);
    return 0;
}

static void print_qom_composition(Monitor *mon, Object *obj, int indent)
{
    GArray *children = g_array_new(false, false, sizeof(Object *));
    const char *name;
    int i;

    if (obj == object_get_root()) {
        name = "";
    } else {
        name = object_get_canonical_path_component(obj);
    }
    monitor_printf(mon, "%*s/%s (%s)\n", indent, "", name,
                   object_get_typename(obj));

    object_child_foreach(obj, insert_qom_composition_child, children);
    g_array_sort(children, qom_composition_compare);

    for (i = 0; i < children->len; i++) {
        print_qom_composition(mon, g_array_index(children, Object *, i),
                              indent + 2);
    }
    g_array_free(children, TRUE);
}

void hmp_info_qom_tree(Monitor *mon, const QDict *dict)
{
    const char *path = qdict_get_try_str(dict, "path");
    Object *obj;
    bool ambiguous = false;

    if (path) {
        obj = object_resolve_path(path, &ambiguous);
        if (!obj) {
            monitor_printf(mon, "Path '%s' could not be resolved.\n", path);
            return;
        }
        if (ambiguous) {
            monitor_printf(mon, "Warning: Path '%s' is ambiguous.\n", path);
            return;
        }
    } else {
        obj = qdev_get_machine();
    }
    print_qom_composition(mon, obj, 0);
}
