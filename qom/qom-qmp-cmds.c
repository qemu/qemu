/*
 * QMP commands related to QOM
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
#include "hw/qdev.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-qdev.h"
#include "qapi/qapi-commands-qom.h"
#include "qapi/qmp/qdict.h"
#include "qapi/qmp/qerror.h"
#include "qapi/qobject-input-visitor.h"
#include "qemu/cutils.h"
#include "qom/object_interfaces.h"
#include "qom/qom-qobject.h"

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

static void qom_list_types_tramp(ObjectClass *klass, void *data)
{
    ObjectTypeInfoList *e, **pret = data;
    ObjectTypeInfo *info;
    ObjectClass *parent = object_class_get_parent(klass);

    info = g_malloc0(sizeof(*info));
    info->name = g_strdup(object_class_get_name(klass));
    info->has_abstract = info->abstract = object_class_is_abstract(klass);
    if (parent) {
        info->has_parent = true;
        info->parent = g_strdup(object_class_get_name(parent));
    }

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
static ObjectPropertyInfo *make_device_property_info(ObjectClass *klass,
                                                  const char *name,
                                                  const char *default_type,
                                                  const char *description)
{
    ObjectPropertyInfo *info;
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
            if (!prop->info->set && !prop->info->create) {
                return NULL;           /* no way to set it, don't show */
            }

            info = g_malloc0(sizeof(*info));
            info->name = g_strdup(prop->name);
            info->type = default_type ? g_strdup(default_type)
                                      : g_strdup(prop->info->name);
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

ObjectPropertyInfoList *qmp_device_list_properties(const char *typename,
                                                Error **errp)
{
    ObjectClass *klass;
    Object *obj;
    ObjectProperty *prop;
    ObjectPropertyIterator iter;
    ObjectPropertyInfoList *prop_list = NULL;

    klass = object_class_by_name(typename);
    if (klass == NULL) {
        error_set(errp, ERROR_CLASS_DEVICE_NOT_FOUND,
                  "Device '%s' not found", typename);
        return NULL;
    }

    klass = object_class_dynamic_cast(klass, TYPE_DEVICE);
    if (klass == NULL) {
        error_setg(errp, QERR_INVALID_PARAMETER_VALUE, "typename", TYPE_DEVICE);
        return NULL;
    }

    if (object_class_is_abstract(klass)) {
        error_setg(errp, QERR_INVALID_PARAMETER_VALUE, "typename",
                   "non-abstract device type");
        return NULL;
    }

    obj = object_new(typename);

    object_property_iter_init(&iter, obj);
    while ((prop = object_property_iter_next(&iter))) {
        ObjectPropertyInfo *info;
        ObjectPropertyInfoList *entry;

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

ObjectPropertyInfoList *qmp_qom_list_properties(const char *typename,
                                             Error **errp)
{
    ObjectClass *klass;
    Object *obj = NULL;
    ObjectProperty *prop;
    ObjectPropertyIterator iter;
    ObjectPropertyInfoList *prop_list = NULL;

    klass = object_class_by_name(typename);
    if (klass == NULL) {
        error_set(errp, ERROR_CLASS_DEVICE_NOT_FOUND,
                  "Class '%s' not found", typename);
        return NULL;
    }

    klass = object_class_dynamic_cast(klass, TYPE_OBJECT);
    if (klass == NULL) {
        error_setg(errp, QERR_INVALID_PARAMETER_VALUE, "typename", TYPE_OBJECT);
        return NULL;
    }

    if (object_class_is_abstract(klass)) {
        object_class_property_iter_init(&iter, klass);
    } else {
        obj = object_new(typename);
        object_property_iter_init(&iter, obj);
    }
    while ((prop = object_property_iter_next(&iter))) {
        ObjectPropertyInfo *info;
        ObjectPropertyInfoList *entry;

        info = g_malloc0(sizeof(*info));
        info->name = g_strdup(prop->name);
        info->type = g_strdup(prop->type);
        info->has_description = !!prop->description;
        info->description = g_strdup(prop->description);

        entry = g_malloc0(sizeof(*entry));
        entry->value = info;
        entry->next = prop_list;
        prop_list = entry;
    }

    object_unref(obj);

    return prop_list;
}

void qmp_object_add(const char *type, const char *id,
                    bool has_props, QObject *props, Error **errp)
{
    QDict *pdict;
    Visitor *v;
    Object *obj;

    if (props) {
        pdict = qobject_to(QDict, props);
        if (!pdict) {
            error_setg(errp, QERR_INVALID_PARAMETER_TYPE, "props", "dict");
            return;
        }
        qobject_ref(pdict);
    } else {
        pdict = qdict_new();
    }

    v = qobject_input_visitor_new(QOBJECT(pdict));
    obj = user_creatable_add_type(type, id, pdict, v, errp);
    visit_free(v);
    if (obj) {
        object_unref(obj);
    }
    qobject_unref(pdict);
}

void qmp_object_del(const char *id, Error **errp)
{
    user_creatable_del(id, errp);
}
