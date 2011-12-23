/*
 * QEMU Object Model
 *
 * Copyright IBM, Corp. 2011
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/object.h"
#include "qemu-common.h"
#include "qapi/qapi-visit-core.h"
#include "hw/qdev.h"
// FIXME remove above

#define MAX_INTERFACES 32

typedef struct InterfaceImpl InterfaceImpl;
typedef struct TypeImpl TypeImpl;

struct InterfaceImpl
{
    const char *parent;
    void (*interface_initfn)(ObjectClass *class, void *data);
    TypeImpl *type;
};

struct TypeImpl
{
    const char *name;

    size_t class_size;

    size_t instance_size;

    void (*class_init)(ObjectClass *klass, void *data);
    void (*class_finalize)(ObjectClass *klass, void *data);

    void *class_data;

    void (*instance_init)(Object *obj);
    void (*instance_finalize)(Object *obj);

    bool abstract;

    const char *parent;
    TypeImpl *parent_type;

    ObjectClass *class;

    int num_interfaces;
    InterfaceImpl interfaces[MAX_INTERFACES];
};

typedef struct Interface
{
    Object parent;
    Object *obj;
} Interface;

#define INTERFACE(obj) OBJECT_CHECK(Interface, obj, TYPE_INTERFACE)

static GHashTable *type_table_get(void)
{
    static GHashTable *type_table;

    if (type_table == NULL) {
        type_table = g_hash_table_new(g_str_hash, g_str_equal);
    }

    return type_table;
}

static void type_table_add(TypeImpl *ti)
{
    g_hash_table_insert(type_table_get(), (void *)ti->name, ti);
}

static TypeImpl *type_table_lookup(const char *name)
{
    return g_hash_table_lookup(type_table_get(), name);
}

TypeImpl *type_register(const TypeInfo *info)
{
    TypeImpl *ti = g_malloc0(sizeof(*ti));

    g_assert(info->name != NULL);

    if (type_table_lookup(info->name) != NULL) {
        fprintf(stderr, "Registering `%s' which already exists\n", info->name);
        abort();
    }

    ti->name = g_strdup(info->name);
    ti->parent = g_strdup(info->parent);

    ti->class_size = info->class_size;
    ti->instance_size = info->instance_size;

    ti->class_init = info->class_init;
    ti->class_finalize = info->class_finalize;
    ti->class_data = info->class_data;

    ti->instance_init = info->instance_init;
    ti->instance_finalize = info->instance_finalize;

    ti->abstract = info->abstract;

    if (info->interfaces) {
        int i;

        for (i = 0; info->interfaces[i].type; i++) {
            ti->interfaces[i].parent = info->interfaces[i].type;
            ti->interfaces[i].interface_initfn = info->interfaces[i].interface_initfn;
            ti->num_interfaces++;
        }
    }

    type_table_add(ti);

    return ti;
}

TypeImpl *type_register_static(const TypeInfo *info)
{
    return type_register(info);
}

static TypeImpl *type_get_by_name(const char *name)
{
    if (name == NULL) {
        return NULL;
    }

    return type_table_lookup(name);
}

static TypeImpl *type_get_parent(TypeImpl *type)
{
    if (!type->parent_type && type->parent) {
        type->parent_type = type_get_by_name(type->parent);
        g_assert(type->parent_type != NULL);
    }

    return type->parent_type;
}

static bool type_has_parent(TypeImpl *type)
{
    return (type->parent != NULL);
}

static size_t type_class_get_size(TypeImpl *ti)
{
    if (ti->class_size) {
        return ti->class_size;
    }

    if (type_has_parent(ti)) {
        return type_class_get_size(type_get_parent(ti));
    }

    return sizeof(ObjectClass);
}

static void type_class_interface_init(TypeImpl *ti, InterfaceImpl *iface)
{
    TypeInfo info = {
        .instance_size = sizeof(Interface),
        .parent = iface->parent,
        .class_size = sizeof(InterfaceClass),
        .class_init = iface->interface_initfn,
        .abstract = true,
    };
    char *name = g_strdup_printf("<%s::%s>", ti->name, iface->parent);

    info.name = name;
    iface->type = type_register(&info);
    g_free(name);
}

static void type_class_init(TypeImpl *ti)
{
    size_t class_size = sizeof(ObjectClass);
    int i;

    if (ti->class) {
        return;
    }

    ti->class_size = type_class_get_size(ti);

    ti->class = g_malloc0(ti->class_size);
    ti->class->type = ti;

    if (type_has_parent(ti)) {
        TypeImpl *parent = type_get_parent(ti);

        type_class_init(parent);

        class_size = parent->class_size;
        g_assert(parent->class_size <= ti->class_size);

        memcpy((void *)ti->class + sizeof(ObjectClass),
               (void *)parent->class + sizeof(ObjectClass),
               parent->class_size - sizeof(ObjectClass));
    }

    memset((void *)ti->class + class_size, 0, ti->class_size - class_size);

    for (i = 0; i < ti->num_interfaces; i++) {
        type_class_interface_init(ti, &ti->interfaces[i]);
    }

    if (ti->class_init) {
        ti->class_init(ti->class, ti->class_data);
    }
}

static void object_interface_init(Object *obj, InterfaceImpl *iface)
{
    TypeImpl *ti = iface->type;
    Interface *iface_obj;

    iface_obj = INTERFACE(object_new(ti->name));
    iface_obj->obj = obj;

    obj->interfaces = g_slist_prepend(obj->interfaces, iface_obj);
}

static void object_init_with_type(Object *obj, TypeImpl *ti)
{
    int i;

    if (type_has_parent(ti)) {
        object_init_with_type(obj, type_get_parent(ti));
    }

    for (i = 0; i < ti->num_interfaces; i++) {
        object_interface_init(obj, &ti->interfaces[i]);
    }

    if (ti->instance_init) {
        ti->instance_init(obj);
    }
}

void object_initialize_with_type(void *data, TypeImpl *type)
{
    Object *obj = data;

    g_assert(type != NULL);
    g_assert(type->instance_size >= sizeof(ObjectClass));

    type_class_init(type);
    g_assert(type->abstract == false);

    memset(obj, 0, type->instance_size);
    obj->class = type->class;
    QTAILQ_INIT(&obj->properties);
    object_init_with_type(obj, type);
}

void object_initialize(void *data, const char *typename)
{
    TypeImpl *type = type_get_by_name(typename);

    object_initialize_with_type(data, type);
}

static void object_property_del_all(Object *obj)
{
    while (!QTAILQ_EMPTY(&obj->properties)) {
        ObjectProperty *prop = QTAILQ_FIRST(&obj->properties);

        QTAILQ_REMOVE(&obj->properties, prop, node);

        if (prop->release) {
            prop->release(obj, prop->name, prop->opaque);
        }

        g_free(prop->name);
        g_free(prop->type);
        g_free(prop);
    }
}

static void object_property_del_child(Object *obj, Object *child, Error **errp)
{
    ObjectProperty *prop;

    QTAILQ_FOREACH(prop, &obj->properties, node) {
        if (!strstart(prop->type, "child<", NULL)) {
            continue;
        }

        if (prop->opaque == child) {
            object_property_del(obj, prop->name, errp);
        }
    }
}

void object_unparent(Object *obj)
{
    if (obj->parent) {
        object_property_del_child(obj->parent, obj, NULL);
    }
}

static void object_deinit(Object *obj, TypeImpl *type)
{
    if (type->instance_finalize) {
        type->instance_finalize(obj);
    }

    while (obj->interfaces) {
        Interface *iface_obj = obj->interfaces->data;
        obj->interfaces = g_slist_delete_link(obj->interfaces, obj->interfaces);
        object_delete(OBJECT(iface_obj));
    }

    if (type_has_parent(type)) {
        object_deinit(obj, type_get_parent(type));
    }

    object_unparent(obj);
}

void object_finalize(void *data)
{
    Object *obj = data;
    TypeImpl *ti = obj->class->type;

    object_deinit(obj, ti);
    object_property_del_all(obj);

    g_assert(obj->ref == 0);
}

Object *object_new_with_type(Type type)
{
    Object *obj;

    g_assert(type != NULL);

    obj = g_malloc(type->instance_size);
    object_initialize_with_type(obj, type);
    object_ref(obj);

    return obj;
}

Object *object_new(const char *typename)
{
    TypeImpl *ti = type_get_by_name(typename);

    return object_new_with_type(ti);
}

void object_delete(Object *obj)
{
    object_unref(obj);
    g_assert(obj->ref == 0);
    g_free(obj);
}

static bool object_is_type(Object *obj, const char *typename)
{
    TypeImpl *target_type = type_get_by_name(typename);
    TypeImpl *type = obj->class->type;
    GSList *i;

    /* Check if typename is a direct ancestor of type */
    while (type) {
        if (type == target_type) {
            return true;
        }

        type = type_get_parent(type);
    }

    /* Check if obj has an interface of typename */
    for (i = obj->interfaces; i; i = i->next) {
        Interface *iface = i->data;

        if (object_is_type(OBJECT(iface), typename)) {
            return true;
        }
    }

    return false;
}

Object *object_dynamic_cast(Object *obj, const char *typename)
{
    GSList *i;

    /* Check if typename is a direct ancestor */
    if (object_is_type(obj, typename)) {
        return obj;
    }

    /* Check if obj has an interface of typename */
    for (i = obj->interfaces; i; i = i->next) {
        Interface *iface = i->data;

        if (object_is_type(OBJECT(iface), typename)) {
            return OBJECT(iface);
        }
    }

    /* Check if obj is an interface and its containing object is a direct
     * ancestor of typename */
    if (object_is_type(obj, TYPE_INTERFACE)) {
        Interface *iface = INTERFACE(obj);

        if (object_is_type(iface->obj, typename)) {
            return iface->obj;
        }
    }

    return NULL;
}


static void register_interface(void)
{
    static TypeInfo interface_info = {
        .name = TYPE_INTERFACE,
        .instance_size = sizeof(Interface),
        .abstract = true,
    };

    type_register_static(&interface_info);
}

device_init(register_interface);

Object *object_dynamic_cast_assert(Object *obj, const char *typename)
{
    Object *inst;

    inst = object_dynamic_cast(obj, typename);

    if (!inst) {
        fprintf(stderr, "Object %p is not an instance of type %s\n",
                obj, typename);
        abort();
    }

    return inst;
}

ObjectClass *object_class_dynamic_cast(ObjectClass *class,
                                       const char *typename)
{
    TypeImpl *target_type = type_get_by_name(typename);
    TypeImpl *type = class->type;

    while (type) {
        if (type == target_type) {
            return class;
        }

        type = type_get_parent(type);
    }

    return NULL;
}

ObjectClass *object_class_dynamic_cast_assert(ObjectClass *class,
                                              const char *typename)
{
    ObjectClass *ret = object_class_dynamic_cast(class, typename);

    if (!ret) {
        fprintf(stderr, "Object %p is not an instance of type %s\n",
                class, typename);
        abort();
    }

    return ret;
}

const char *object_get_typename(Object *obj)
{
    return obj->class->type->name;
}

ObjectClass *object_get_class(Object *obj)
{
    return obj->class;
}

const char *object_class_get_name(ObjectClass *klass)
{
    return klass->type->name;
}

ObjectClass *object_class_by_name(const char *typename)
{
    TypeImpl *type = type_get_by_name(typename);

    if (!type) {
        return NULL;
    }

    type_class_init(type);

    return type->class;
}

typedef struct OCFData
{
    void (*fn)(ObjectClass *klass, void *opaque);
    const char *implements_type;
    bool include_abstract;
    void *opaque;
} OCFData;

static void object_class_foreach_tramp(gpointer key, gpointer value,
                                       gpointer opaque)
{
    OCFData *data = opaque;
    TypeImpl *type = value;
    ObjectClass *k;

    type_class_init(type);
    k = type->class;

    if (!data->include_abstract && type->abstract) {
        return;
    }

    if (data->implements_type && 
        !object_class_dynamic_cast(k, data->implements_type)) {
        return;
    }

    data->fn(k, data->opaque);
}

void object_class_foreach(void (*fn)(ObjectClass *klass, void *opaque),
                          const char *implements_type, bool include_abstract,
                          void *opaque)
{
    OCFData data = { fn, implements_type, include_abstract, opaque };

    g_hash_table_foreach(type_table_get(), object_class_foreach_tramp, &data);
}

void object_ref(Object *obj)
{
    obj->ref++;
}

void object_unref(Object *obj)
{
    g_assert(obj->ref > 0);
    obj->ref--;

    /* parent always holds a reference to its children */
    if (obj->ref == 0) {
        object_finalize(obj);
    }
}

void object_property_add(Object *obj, const char *name, const char *type,
                         ObjectPropertyAccessor *get,
                         ObjectPropertyAccessor *set,
                         ObjectPropertyRelease *release,
                         void *opaque, Error **errp)
{
    ObjectProperty *prop = g_malloc0(sizeof(*prop));

    prop->name = g_strdup(name);
    prop->type = g_strdup(type);

    prop->get = get;
    prop->set = set;
    prop->release = release;
    prop->opaque = opaque;

    QTAILQ_INSERT_TAIL(&obj->properties, prop, node);
}

static ObjectProperty *object_property_find(Object *obj, const char *name)
{
    ObjectProperty *prop;

    QTAILQ_FOREACH(prop, &obj->properties, node) {
        if (strcmp(prop->name, name) == 0) {
            return prop;
        }
    }

    return NULL;
}

void object_property_del(Object *obj, const char *name, Error **errp)
{
    ObjectProperty *prop = object_property_find(obj, name);

    QTAILQ_REMOVE(&obj->properties, prop, node);

    prop->release(obj, prop->name, prop->opaque);

    g_free(prop->name);
    g_free(prop->type);
    g_free(prop);
}

void object_property_get(Object *obj, Visitor *v, const char *name,
                         Error **errp)
{
    ObjectProperty *prop = object_property_find(obj, name);

    if (prop == NULL) {
        error_set(errp, QERR_PROPERTY_NOT_FOUND, "", name);
        return;
    }

    if (!prop->get) {
        error_set(errp, QERR_PERMISSION_DENIED);
    } else {
        prop->get(obj, v, prop->opaque, name, errp);
    }
}

void object_property_set(Object *obj, Visitor *v, const char *name,
                         Error **errp)
{
    ObjectProperty *prop = object_property_find(obj, name);

    if (prop == NULL) {
        error_set(errp, QERR_PROPERTY_NOT_FOUND, "", name);
        return;
    }

    if (!prop->set) {
        error_set(errp, QERR_PERMISSION_DENIED);
    } else {
        prop->set(obj, v, prop->opaque, name, errp);
    }
}

const char *object_property_get_type(Object *obj, const char *name, Error **errp)
{
    ObjectProperty *prop = object_property_find(obj, name);

    if (prop == NULL) {
        error_set(errp, QERR_PROPERTY_NOT_FOUND, "", name);
        return NULL;
    }

    return prop->type;
}

Object *object_get_root(void)
{
    static Object *root;

    if (!root) {
        root = object_new("container");
    }

    return root;
}

static void object_get_child_property(Object *obj, Visitor *v, void *opaque,
                                      const char *name, Error **errp)
{
    Object *child = opaque;
    gchar *path;

    path = object_get_canonical_path(child);
    visit_type_str(v, &path, name, errp);
    g_free(path);
}

static void object_finalize_child_property(Object *obj, const char *name,
                                           void *opaque)
{
    Object *child = opaque;

    object_unref(child);
}

void object_property_add_child(Object *obj, const char *name,
                               Object *child, Error **errp)
{
    gchar *type;

    type = g_strdup_printf("child<%s>", object_get_typename(OBJECT(child)));

    object_property_add(obj, name, type, object_get_child_property,
                        NULL, object_finalize_child_property, child, errp);

    object_ref(child);
    g_assert(child->parent == NULL);
    child->parent = obj;

    g_free(type);
}

static void object_get_link_property(Object *obj, Visitor *v, void *opaque,
                                     const char *name, Error **errp)
{
    Object **child = opaque;
    gchar *path;

    if (*child) {
        path = object_get_canonical_path(*child);
        visit_type_str(v, &path, name, errp);
        g_free(path);
    } else {
        path = (gchar *)"";
        visit_type_str(v, &path, name, errp);
    }
}

static void object_set_link_property(Object *obj, Visitor *v, void *opaque,
                                     const char *name, Error **errp)
{
    Object **child = opaque;
    bool ambiguous = false;
    const char *type;
    char *path;

    type = object_property_get_type(obj, name, NULL);

    visit_type_str(v, &path, name, errp);

    if (*child) {
        object_unref(*child);
    }

    if (strcmp(path, "") != 0) {
        Object *target;

        target = object_resolve_path(path, &ambiguous);
        if (target) {
            gchar *target_type;

            target_type = g_strdup(&type[5]);
            target_type[strlen(target_type) - 2] = 0;

            if (object_dynamic_cast(target, target_type)) {
                object_ref(target);
                *child = target;
            } else {
                error_set(errp, QERR_INVALID_PARAMETER_TYPE, name, type);
            }

            g_free(target_type);
        } else {
            error_set(errp, QERR_DEVICE_NOT_FOUND, path);
        }
    } else {
        *child = NULL;
    }

    g_free(path);
}

void object_property_add_link(Object *obj, const char *name,
                              const char *type, Object **child,
                              Error **errp)
{
    gchar *full_type;

    full_type = g_strdup_printf("link<%s>", type);

    object_property_add(obj, name, full_type,
                        object_get_link_property,
                        object_set_link_property,
                        NULL, child, errp);

    g_free(full_type);
}

gchar *object_get_canonical_path(Object *obj)
{
    Object *root = object_get_root();
    char *newpath = NULL, *path = NULL;

    while (obj != root) {
        ObjectProperty *prop = NULL;

        g_assert(obj->parent != NULL);

        QTAILQ_FOREACH(prop, &obj->parent->properties, node) {
            if (!strstart(prop->type, "child<", NULL)) {
                continue;
            }

            if (prop->opaque == obj) {
                if (path) {
                    newpath = g_strdup_printf("%s/%s", prop->name, path);
                    g_free(path);
                    path = newpath;
                } else {
                    path = g_strdup(prop->name);
                }
                break;
            }
        }

        g_assert(prop != NULL);

        obj = obj->parent;
    }

    newpath = g_strdup_printf("/%s", path);
    g_free(path);

    return newpath;
}

static Object *object_resolve_abs_path(Object *parent,
                                          gchar **parts,
                                          int index)
{
    ObjectProperty *prop;
    Object *child;

    if (parts[index] == NULL) {
        return parent;
    }

    if (strcmp(parts[index], "") == 0) {
        return object_resolve_abs_path(parent, parts, index + 1);
    }

    prop = object_property_find(parent, parts[index]);
    if (prop == NULL) {
        return NULL;
    }

    child = NULL;
    if (strstart(prop->type, "link<", NULL)) {
        Object **pchild = prop->opaque;
        if (*pchild) {
            child = *pchild;
        }
    } else if (strstart(prop->type, "child<", NULL)) {
        child = prop->opaque;
    }

    if (!child) {
        return NULL;
    }

    return object_resolve_abs_path(child, parts, index + 1);
}

static Object *object_resolve_partial_path(Object *parent,
                                              gchar **parts,
                                              bool *ambiguous)
{
    Object *obj;
    ObjectProperty *prop;

    obj = object_resolve_abs_path(parent, parts, 0);

    QTAILQ_FOREACH(prop, &parent->properties, node) {
        Object *found;

        if (!strstart(prop->type, "child<", NULL)) {
            continue;
        }

        found = object_resolve_partial_path(prop->opaque, parts, ambiguous);
        if (found) {
            if (obj) {
                if (ambiguous) {
                    *ambiguous = true;
                }
                return NULL;
            }
            obj = found;
        }

        if (ambiguous && *ambiguous) {
            return NULL;
        }
    }

    return obj;
}

Object *object_resolve_path(const char *path, bool *ambiguous)
{
    bool partial_path = true;
    Object *obj;
    gchar **parts;

    parts = g_strsplit(path, "/", 0);
    if (parts == NULL || parts[0] == NULL) {
        g_strfreev(parts);
        return object_get_root();
    }

    if (strcmp(parts[0], "") == 0) {
        partial_path = false;
    }

    if (partial_path) {
        if (ambiguous) {
            *ambiguous = false;
        }
        obj = object_resolve_partial_path(object_get_root(), parts, ambiguous);
    } else {
        obj = object_resolve_abs_path(object_get_root(), parts, 1);
    }

    g_strfreev(parts);

    return obj;
}

typedef struct StringProperty
{
    char *(*get)(Object *, Error **);
    void (*set)(Object *, const char *, Error **);
} StringProperty;

static void object_property_get_str(Object *obj, Visitor *v, void *opaque,
                                    const char *name, Error **errp)
{
    StringProperty *prop = opaque;
    char *value;

    value = prop->get(obj, errp);
    if (value) {
        visit_type_str(v, &value, name, errp);
        g_free(value);
    }
}

static void object_property_set_str(Object *obj, Visitor *v, void *opaque,
                                  const char *name, Error **errp)
{
    StringProperty *prop = opaque;
    char *value;
    Error *local_err = NULL;

    visit_type_str(v, &value, name, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    prop->set(obj, value, errp);
    g_free(value);
}

static void object_property_release_str(Object *obj, const char *name,
                                      void *opaque)
{
    StringProperty *prop = opaque;
    g_free(prop);
}

void object_property_add_str(Object *obj, const char *name,
                           char *(*get)(Object *, Error **),
                           void (*set)(Object *, const char *, Error **),
                           Error **errp)
{
    StringProperty *prop = g_malloc0(sizeof(*prop));

    prop->get = get;
    prop->set = set;

    object_property_add(obj, name, "string",
                        get ? object_property_get_str : NULL,
                        set ? object_property_set_str : NULL,
                        object_property_release_str,
                        prop, errp);
}
