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
    object_init_with_type(obj, type);
}

void object_initialize(void *data, const char *typename)
{
    TypeImpl *type = type_get_by_name(typename);

    object_initialize_with_type(data, type);
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
}

void object_finalize(void *data)
{
    Object *obj = data;
    TypeImpl *ti = obj->class->type;

    object_deinit(obj, ti);
}

Object *object_new_with_type(Type type)
{
    Object *obj;

    g_assert(type != NULL);

    obj = g_malloc(type->instance_size);
    object_initialize_with_type(obj, type);

    return obj;
}

Object *object_new(const char *typename)
{
    TypeImpl *ti = type_get_by_name(typename);

    return object_new_with_type(ti);
}

void object_delete(Object *obj)
{
    object_finalize(obj);
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
    void *opaque;
} OCFData;

static void object_class_foreach_tramp(gpointer key, gpointer value,
                                       gpointer opaque)
{
    OCFData *data = opaque;
    TypeImpl *type = value;

    type_class_init(type);

    data->fn(value, type->class);
}

void object_class_foreach(void (*fn)(ObjectClass *klass, void *opaque),
                          void *opaque)
{
    OCFData data = { fn, opaque };

    g_hash_table_foreach(type_table_get(), object_class_foreach_tramp, &data);
}
