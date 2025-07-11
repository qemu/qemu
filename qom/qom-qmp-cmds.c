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
#include "block/qdict.h"
#include "hw/qdev-core.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-qdev.h"
#include "qapi/qapi-commands-qom.h"
#include "qapi/qapi-visit-qom.h"
#include "qobject/qdict.h"
#include "qapi/qmp/qerror.h"
#include "qapi/qobject-input-visitor.h"
#include "qapi/qobject-output-visitor.h"
#include "qemu/cutils.h"
#include "qom/object_interfaces.h"
#include "qom/qom-qobject.h"

static Object *qom_resolve_path(const char *path, Error **errp)
{
    bool ambiguous = false;
    Object *obj = object_resolve_path(path, &ambiguous);

    if (obj == NULL) {
        if (ambiguous) {
            error_setg(errp, "Path '%s' is ambiguous", path);
        } else {
            error_set(errp, ERROR_CLASS_DEVICE_NOT_FOUND,
                      "Device '%s' not found", path);
        }
    }
    return obj;
}

ObjectPropertyInfoList *qmp_qom_list(const char *path, Error **errp)
{
    Object *obj;
    ObjectPropertyInfoList *props = NULL;
    ObjectProperty *prop;
    ObjectPropertyIterator iter;

    obj = qom_resolve_path(path, errp);
    if (obj == NULL) {
        return NULL;
    }

    object_property_iter_init(&iter, obj);
    while ((prop = object_property_iter_next(&iter))) {
        ObjectPropertyInfo *value = g_new0(ObjectPropertyInfo, 1);

        QAPI_LIST_PREPEND(props, value);

        value->name = g_strdup(prop->name);
        value->type = g_strdup(prop->type);
    }

    return props;
}

static void qom_list_add_property_value(Object *obj, ObjectProperty *prop,
                                        ObjectPropertyValueList **props)
{
    ObjectPropertyValue *item = g_new0(ObjectPropertyValue, 1);

    QAPI_LIST_PREPEND(*props, item);

    item->name = g_strdup(prop->name);
    item->type = g_strdup(prop->type);
    item->value = object_property_get_qobject(obj, prop->name, NULL);
}

static ObjectPropertyValueList *qom_get_property_value_list(const char *path,
                                                            Error **errp)
{
    Object *obj;
    ObjectProperty *prop;
    ObjectPropertyIterator iter;
    ObjectPropertyValueList *props = NULL;

    obj = qom_resolve_path(path, errp);
    if (obj == NULL) {
        return NULL;
    }

    object_property_iter_init(&iter, obj);
    while ((prop = object_property_iter_next(&iter))) {
        qom_list_add_property_value(obj, prop, &props);
    }

    return props;
}

ObjectPropertiesValuesList *qmp_qom_list_get(strList *paths, Error **errp)
{
    ObjectPropertiesValuesList *head = NULL, **tail = &head;
    strList *path;

    for (path = paths; path; path = path->next) {
        ObjectPropertiesValues *item = g_new0(ObjectPropertiesValues, 1);

        QAPI_LIST_APPEND(tail, item);

        item->properties = qom_get_property_value_list(path->value, errp);
        if (!item->properties) {
            qapi_free_ObjectPropertiesValuesList(head);
            return NULL;
        }
    }

    return head;
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

    object_property_set_qobject(obj, property, value, errp);
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
    ObjectTypeInfoList **pret = data;
    ObjectTypeInfo *info;
    ObjectClass *parent = object_class_get_parent(klass);

    info = g_malloc0(sizeof(*info));
    info->name = g_strdup(object_class_get_name(klass));
    info->has_abstract = info->abstract = object_class_is_abstract(klass);
    if (parent) {
        info->parent = g_strdup(object_class_get_name(parent));
    }

    QAPI_LIST_PREPEND(*pret, info);
}

ObjectTypeInfoList *qmp_qom_list_types(const char *implements,
                                       bool has_abstract,
                                       bool abstract,
                                       Error **errp)
{
    ObjectTypeInfoList *ret = NULL;

    module_load_qom_all();
    object_class_foreach(qom_list_types_tramp, implements, abstract, &ret);

    return ret;
}

ObjectPropertyInfoList *qmp_device_list_properties(const char *typename,
                                                Error **errp)
{
    ObjectClass *klass;
    Object *obj;
    ObjectProperty *prop;
    ObjectPropertyIterator iter;
    ObjectPropertyInfoList *prop_list = NULL;

    klass = module_object_class_by_name(typename);
    if (klass == NULL) {
        error_set(errp, ERROR_CLASS_DEVICE_NOT_FOUND,
                  "Device '%s' not found", typename);
        return NULL;
    }

    if (!object_class_dynamic_cast(klass, TYPE_DEVICE)
        || object_class_is_abstract(klass)) {
        error_setg(errp, QERR_INVALID_PARAMETER_VALUE, "typename",
                   "a non-abstract device type");
        return NULL;
    }

    obj = object_new_with_class(klass);

    object_property_iter_init(&iter, obj);
    while ((prop = object_property_iter_next(&iter))) {
        ObjectPropertyInfo *info;

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

        info = g_new0(ObjectPropertyInfo, 1);
        info->name = g_strdup(prop->name);
        info->type = g_strdup(prop->type);
        info->description = g_strdup(prop->description);
        info->default_value = qobject_ref(prop->defval);

        QAPI_LIST_PREPEND(prop_list, info);
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

    klass = module_object_class_by_name(typename);
    if (klass == NULL) {
        error_set(errp, ERROR_CLASS_DEVICE_NOT_FOUND,
                  "Class '%s' not found", typename);
        return NULL;
    }

    if (!object_class_dynamic_cast(klass, TYPE_OBJECT)) {
        error_setg(errp, QERR_INVALID_PARAMETER_VALUE, "typename",
                   "a QOM type");
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

        info = g_malloc0(sizeof(*info));
        info->name = g_strdup(prop->name);
        info->type = g_strdup(prop->type);
        info->description = g_strdup(prop->description);
        info->default_value = qobject_ref(prop->defval);

        QAPI_LIST_PREPEND(prop_list, info);
    }

    object_unref(obj);

    return prop_list;
}

void qmp_object_add(ObjectOptions *options, Error **errp)
{
    user_creatable_add_qapi(options, errp);
}

void qmp_object_del(const char *id, Error **errp)
{
    user_creatable_del(id, errp);
}
