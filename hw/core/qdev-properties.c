#include "qemu/osdep.h"
#include "hw/qdev-properties.h"
#include "qapi/error.h"
#include "qapi/qapi-types-misc.h"
#include "qobject/qlist.h"
#include "qemu/ctype.h"
#include "qemu/error-report.h"
#include "qapi/visitor.h"
#include "qemu/units.h"
#include "qemu/cutils.h"
#include "qdev-prop-internal.h"
#include "qom/qom-qobject.h"

void qdev_prop_set_after_realize(DeviceState *dev, const char *name,
                                  Error **errp)
{
    if (dev->id) {
        error_setg(errp, "Attempt to set property '%s' on device '%s' "
                   "(type '%s') after it was realized", name, dev->id,
                   object_get_typename(OBJECT(dev)));
    } else {
        error_setg(errp, "Attempt to set property '%s' on anonymous device "
                   "(type '%s') after it was realized", name,
                   object_get_typename(OBJECT(dev)));
    }
}

/* returns: true if property is allowed to be set, false otherwise */
static bool qdev_prop_allow_set(Object *obj, const char *name,
                                const PropertyInfo *info, Error **errp)
{
    DeviceState *dev = DEVICE(obj);

    if (dev->realized && !info->realized_set_allowed) {
        qdev_prop_set_after_realize(dev, name, errp);
        return false;
    }
    return true;
}

void qdev_prop_allow_set_link_before_realize(const Object *obj,
                                             const char *name,
                                             Object *val, Error **errp)
{
    DeviceState *dev = DEVICE(obj);

    if (dev->realized) {
        error_setg(errp, "Attempt to set link property '%s' on device '%s' "
                   "(type '%s') after it was realized",
                   name, dev->id, object_get_typename(obj));
    }
}

void *object_field_prop_ptr(Object *obj, const Property *prop)
{
    void *ptr = obj;
    ptr += prop->offset;
    return ptr;
}

static void field_prop_get(Object *obj, Visitor *v, const char *name,
                           void *opaque, Error **errp)
{
    const Property *prop = opaque;
    return prop->info->get(obj, v, name, opaque, errp);
}

/**
 * field_prop_getter: Return getter function to be used for property
 *
 * Return value can be NULL if @info has no getter function.
 */
static ObjectPropertyAccessor *field_prop_getter(const PropertyInfo *info)
{
    return info->get ? field_prop_get : NULL;
}

static void field_prop_set(Object *obj, Visitor *v, const char *name,
                           void *opaque, Error **errp)
{
    const Property *prop = opaque;

    if (!qdev_prop_allow_set(obj, name, prop->info, errp)) {
        return;
    }

    return prop->info->set(obj, v, name, opaque, errp);
}

/**
 * field_prop_setter: Return setter function to be used for property
 *
 * Return value can be NULL if @info has not setter function.
 */
static ObjectPropertyAccessor *field_prop_setter(const PropertyInfo *info)
{
    return info->set ? field_prop_set : NULL;
}

void qdev_propinfo_get_enum(Object *obj, Visitor *v, const char *name,
                            void *opaque, Error **errp)
{
    const Property *prop = opaque;
    int *ptr = object_field_prop_ptr(obj, prop);

    visit_type_enum(v, name, ptr, prop->info->enum_table, errp);
}

void qdev_propinfo_set_enum(Object *obj, Visitor *v, const char *name,
                            void *opaque, Error **errp)
{
    const Property *prop = opaque;
    int *ptr = object_field_prop_ptr(obj, prop);

    visit_type_enum(v, name, ptr, prop->info->enum_table, errp);
}

void qdev_propinfo_set_default_value_enum(ObjectProperty *op,
                                          const Property *prop)
{
    object_property_set_default_str(op,
        qapi_enum_lookup(prop->info->enum_table, prop->defval.i));
}

/* Bit */

static uint32_t qdev_get_prop_mask(const Property *prop)
{
    assert(prop->info == &qdev_prop_bit);
    return 0x1 << prop->bitnr;
}

static void bit_prop_set(Object *obj, const Property *props, bool val)
{
    uint32_t *p = object_field_prop_ptr(obj, props);
    uint32_t mask = qdev_get_prop_mask(props);
    if (val) {
        *p |= mask;
    } else {
        *p &= ~mask;
    }
}

static void prop_get_bit(Object *obj, Visitor *v, const char *name,
                         void *opaque, Error **errp)
{
    const Property *prop = opaque;
    uint32_t *p = object_field_prop_ptr(obj, prop);
    bool value = (*p & qdev_get_prop_mask(prop)) != 0;

    visit_type_bool(v, name, &value, errp);
}

static void prop_set_bit(Object *obj, Visitor *v, const char *name,
                         void *opaque, Error **errp)
{
    const Property *prop = opaque;
    bool value;

    if (!visit_type_bool(v, name, &value, errp)) {
        return;
    }
    bit_prop_set(obj, prop, value);
}

static void set_default_value_bool(ObjectProperty *op, const Property *prop)
{
    object_property_set_default_bool(op, prop->defval.u);
}

const PropertyInfo qdev_prop_bit = {
    .type  = "bool",
    .description = "on/off",
    .get   = prop_get_bit,
    .set   = prop_set_bit,
    .set_default_value = set_default_value_bool,
};

/* Bit64 */

static uint64_t qdev_get_prop_mask64(const Property *prop)
{
    assert(prop->info == &qdev_prop_bit64);
    return 0x1ull << prop->bitnr;
}

static void bit64_prop_set(Object *obj, const Property *props, bool val)
{
    uint64_t *p = object_field_prop_ptr(obj, props);
    uint64_t mask = qdev_get_prop_mask64(props);
    if (val) {
        *p |= mask;
    } else {
        *p &= ~mask;
    }
}

static void prop_get_bit64(Object *obj, Visitor *v, const char *name,
                           void *opaque, Error **errp)
{
    const Property *prop = opaque;
    uint64_t *p = object_field_prop_ptr(obj, prop);
    bool value = (*p & qdev_get_prop_mask64(prop)) != 0;

    visit_type_bool(v, name, &value, errp);
}

static void prop_set_bit64(Object *obj, Visitor *v, const char *name,
                           void *opaque, Error **errp)
{
    const Property *prop = opaque;
    bool value;

    if (!visit_type_bool(v, name, &value, errp)) {
        return;
    }
    bit64_prop_set(obj, prop, value);
}

const PropertyInfo qdev_prop_bit64 = {
    .type  = "bool",
    .description = "on/off",
    .get   = prop_get_bit64,
    .set   = prop_set_bit64,
    .set_default_value = set_default_value_bool,
};

/* --- bool --- */

static void get_bool(Object *obj, Visitor *v, const char *name, void *opaque,
                     Error **errp)
{
    const Property *prop = opaque;
    bool *ptr = object_field_prop_ptr(obj, prop);

    visit_type_bool(v, name, ptr, errp);
}

static void set_bool(Object *obj, Visitor *v, const char *name, void *opaque,
                     Error **errp)
{
    const Property *prop = opaque;
    bool *ptr = object_field_prop_ptr(obj, prop);

    visit_type_bool(v, name, ptr, errp);
}

const PropertyInfo qdev_prop_bool = {
    .type  = "bool",
    .description = "on/off",
    .get   = get_bool,
    .set   = set_bool,
    .set_default_value = set_default_value_bool,
};

/* --- 8bit integer --- */

static void get_uint8(Object *obj, Visitor *v, const char *name, void *opaque,
                      Error **errp)
{
    const Property *prop = opaque;
    uint8_t *ptr = object_field_prop_ptr(obj, prop);

    visit_type_uint8(v, name, ptr, errp);
}

static void set_uint8(Object *obj, Visitor *v, const char *name, void *opaque,
                      Error **errp)
{
    const Property *prop = opaque;
    uint8_t *ptr = object_field_prop_ptr(obj, prop);

    visit_type_uint8(v, name, ptr, errp);
}

void qdev_propinfo_set_default_value_int(ObjectProperty *op,
                                         const Property *prop)
{
    object_property_set_default_int(op, prop->defval.i);
}

void qdev_propinfo_set_default_value_uint(ObjectProperty *op,
                                          const Property *prop)
{
    object_property_set_default_uint(op, prop->defval.u);
}

const PropertyInfo qdev_prop_uint8 = {
    .type  = "uint8",
    .get   = get_uint8,
    .set   = set_uint8,
    .set_default_value = qdev_propinfo_set_default_value_uint,
};

/* --- 16bit integer --- */

static void get_uint16(Object *obj, Visitor *v, const char *name,
                       void *opaque, Error **errp)
{
    const Property *prop = opaque;
    uint16_t *ptr = object_field_prop_ptr(obj, prop);

    visit_type_uint16(v, name, ptr, errp);
}

static void set_uint16(Object *obj, Visitor *v, const char *name,
                       void *opaque, Error **errp)
{
    const Property *prop = opaque;
    uint16_t *ptr = object_field_prop_ptr(obj, prop);

    visit_type_uint16(v, name, ptr, errp);
}

const PropertyInfo qdev_prop_uint16 = {
    .type  = "uint16",
    .get   = get_uint16,
    .set   = set_uint16,
    .set_default_value = qdev_propinfo_set_default_value_uint,
};

/* --- 32bit integer --- */

static void get_uint32(Object *obj, Visitor *v, const char *name,
                       void *opaque, Error **errp)
{
    const Property *prop = opaque;
    uint32_t *ptr = object_field_prop_ptr(obj, prop);

    visit_type_uint32(v, name, ptr, errp);
}

static void set_uint32(Object *obj, Visitor *v, const char *name,
                       void *opaque, Error **errp)
{
    const Property *prop = opaque;
    uint32_t *ptr = object_field_prop_ptr(obj, prop);

    visit_type_uint32(v, name, ptr, errp);
}

void qdev_propinfo_get_int32(Object *obj, Visitor *v, const char *name,
                             void *opaque, Error **errp)
{
    const Property *prop = opaque;
    int32_t *ptr = object_field_prop_ptr(obj, prop);

    visit_type_int32(v, name, ptr, errp);
}

static void set_int32(Object *obj, Visitor *v, const char *name, void *opaque,
                      Error **errp)
{
    const Property *prop = opaque;
    int32_t *ptr = object_field_prop_ptr(obj, prop);

    visit_type_int32(v, name, ptr, errp);
}

const PropertyInfo qdev_prop_uint32 = {
    .type  = "uint32",
    .get   = get_uint32,
    .set   = set_uint32,
    .set_default_value = qdev_propinfo_set_default_value_uint,
};

const PropertyInfo qdev_prop_int32 = {
    .type  = "int32",
    .get   = qdev_propinfo_get_int32,
    .set   = set_int32,
    .set_default_value = qdev_propinfo_set_default_value_int,
};

/* --- 64bit integer --- */

static void get_uint64(Object *obj, Visitor *v, const char *name,
                       void *opaque, Error **errp)
{
    const Property *prop = opaque;
    uint64_t *ptr = object_field_prop_ptr(obj, prop);

    visit_type_uint64(v, name, ptr, errp);
}

static void set_uint64(Object *obj, Visitor *v, const char *name,
                       void *opaque, Error **errp)
{
    const Property *prop = opaque;
    uint64_t *ptr = object_field_prop_ptr(obj, prop);

    visit_type_uint64(v, name, ptr, errp);
}

static void get_int64(Object *obj, Visitor *v, const char *name,
                      void *opaque, Error **errp)
{
    const Property *prop = opaque;
    int64_t *ptr = object_field_prop_ptr(obj, prop);

    visit_type_int64(v, name, ptr, errp);
}

static void set_int64(Object *obj, Visitor *v, const char *name,
                      void *opaque, Error **errp)
{
    const Property *prop = opaque;
    int64_t *ptr = object_field_prop_ptr(obj, prop);

    visit_type_int64(v, name, ptr, errp);
}

const PropertyInfo qdev_prop_uint64 = {
    .type  = "uint64",
    .get   = get_uint64,
    .set   = set_uint64,
    .set_default_value = qdev_propinfo_set_default_value_uint,
};

const PropertyInfo qdev_prop_int64 = {
    .type  = "int64",
    .get   = get_int64,
    .set   = set_int64,
    .set_default_value = qdev_propinfo_set_default_value_int,
};

static void set_uint64_checkmask(Object *obj, Visitor *v, const char *name,
                      void *opaque, Error **errp)
{
    const Property *prop = opaque;
    uint64_t *ptr = object_field_prop_ptr(obj, prop);

    visit_type_uint64(v, name, ptr, errp);
    if (*ptr & ~prop->bitmask) {
        error_setg(errp, "Property value for '%s' has bits outside mask '0x%" PRIx64 "'",
                   name, prop->bitmask);
    }
}

const PropertyInfo qdev_prop_uint64_checkmask = {
    .type  = "uint64",
    .get   = get_uint64,
    .set   = set_uint64_checkmask,
};

/* --- pointer-size integer --- */

static void get_usize(Object *obj, Visitor *v, const char *name, void *opaque,
                      Error **errp)
{
    const Property *prop = opaque;

#if HOST_LONG_BITS == 32
    uint32_t *ptr = object_field_prop_ptr(obj, prop);
    visit_type_uint32(v, name, ptr, errp);
#else
    uint64_t *ptr = object_field_prop_ptr(obj, prop);
    visit_type_uint64(v, name, ptr, errp);
#endif
}

static void set_usize(Object *obj, Visitor *v, const char *name, void *opaque,
                      Error **errp)
{
    const Property *prop = opaque;

#if HOST_LONG_BITS == 32
    uint32_t *ptr = object_field_prop_ptr(obj, prop);
    visit_type_uint32(v, name, ptr, errp);
#else
    uint64_t *ptr = object_field_prop_ptr(obj, prop);
    visit_type_uint64(v, name, ptr, errp);
#endif
}

const PropertyInfo qdev_prop_usize = {
    .type  = "usize",
    .get   = get_usize,
    .set   = set_usize,
    .set_default_value = qdev_propinfo_set_default_value_uint,
};

/* --- string --- */

static void release_string(Object *obj, const char *name, void *opaque)
{
    const Property *prop = opaque;
    g_free(*(char **)object_field_prop_ptr(obj, prop));
}

static void get_string(Object *obj, Visitor *v, const char *name,
                       void *opaque, Error **errp)
{
    const Property *prop = opaque;
    char **ptr = object_field_prop_ptr(obj, prop);

    if (!*ptr) {
        char *str = (char *)"";
        visit_type_str(v, name, &str, errp);
    } else {
        visit_type_str(v, name, ptr, errp);
    }
}

static void set_string(Object *obj, Visitor *v, const char *name,
                       void *opaque, Error **errp)
{
    const Property *prop = opaque;
    char **ptr = object_field_prop_ptr(obj, prop);
    char *str;

    if (!visit_type_str(v, name, &str, errp)) {
        return;
    }
    g_free(*ptr);
    *ptr = str;
}

const PropertyInfo qdev_prop_string = {
    .type  = "str",
    .release = release_string,
    .get   = get_string,
    .set   = set_string,
};

/* --- on/off/auto --- */

const PropertyInfo qdev_prop_on_off_auto = {
    .type = "OnOffAuto",
    .description = "on/off/auto",
    .enum_table = &OnOffAuto_lookup,
    .get = qdev_propinfo_get_enum,
    .set = qdev_propinfo_set_enum,
    .set_default_value = qdev_propinfo_set_default_value_enum,
};

/* --- 32bit unsigned int 'size' type --- */

void qdev_propinfo_get_size32(Object *obj, Visitor *v, const char *name,
                              void *opaque, Error **errp)
{
    const Property *prop = opaque;
    uint32_t *ptr = object_field_prop_ptr(obj, prop);
    uint64_t value = *ptr;

    visit_type_size(v, name, &value, errp);
}

static void set_size32(Object *obj, Visitor *v, const char *name, void *opaque,
                       Error **errp)
{
    const Property *prop = opaque;
    uint32_t *ptr = object_field_prop_ptr(obj, prop);
    uint64_t value;

    if (!visit_type_size(v, name, &value, errp)) {
        return;
    }

    if (value > UINT32_MAX) {
        error_setg(errp,
                   "Property %s.%s doesn't take value %" PRIu64
                   " (maximum: %u)",
                   object_get_typename(obj), name, value, UINT32_MAX);
        return;
    }

    *ptr = value;
}

const PropertyInfo qdev_prop_size32 = {
    .type  = "size",
    .get = qdev_propinfo_get_size32,
    .set = set_size32,
    .set_default_value = qdev_propinfo_set_default_value_uint,
};

/* --- support for array properties --- */

typedef struct ArrayElementList ArrayElementList;

struct ArrayElementList {
    ArrayElementList *next;
    void *value;
};

/*
 * Given an array property @parent_prop in @obj, return a Property for a
 * specific element of the array. Arrays are backed by an uint32_t length field
 * and an element array. @elem points at an element in this element array.
 */
static Property array_elem_prop(Object *obj, const Property *parent_prop,
                                const char *name, char *elem)
{
    return (Property) {
        .info = parent_prop->arrayinfo,
        .name = name,
        /*
         * This ugly piece of pointer arithmetic sets up the offset so
         * that when the underlying release hook calls qdev_get_prop_ptr
         * they get the right answer despite the array element not actually
         * being inside the device struct.
         */
        .offset = (uintptr_t)elem - (uintptr_t)obj,
    };
}

/*
 * Object property release callback for array properties: We call the
 * underlying element's property release hook for each element.
 *
 * Note that it is the responsibility of the individual device's deinit
 * to free the array proper.
 */
static void release_prop_array(Object *obj, const char *name, void *opaque)
{
    const Property *prop = opaque;
    uint32_t *alenptr = object_field_prop_ptr(obj, prop);
    void **arrayptr = (void *)obj + prop->arrayoffset;
    char *elem = *arrayptr;
    int i;

    if (!prop->arrayinfo->release) {
        return;
    }

    for (i = 0; i < *alenptr; i++) {
        Property elem_prop = array_elem_prop(obj, prop, name, elem);
        prop->arrayinfo->release(obj, NULL, &elem_prop);
        elem += prop->arrayfieldsize;
    }
}

/*
 * Setter for an array property. This sets both the array length (which
 * is technically the property field in the object) and the array itself
 * (a pointer to which is stored in the additional field described by
 * prop->arrayoffset).
 */
static void set_prop_array(Object *obj, Visitor *v, const char *name,
                           void *opaque, Error **errp)
{
    ERRP_GUARD();
    const Property *prop = opaque;
    uint32_t *alenptr = object_field_prop_ptr(obj, prop);
    void **arrayptr = (void *)obj + prop->arrayoffset;
    ArrayElementList *list, *elem, *next;
    const size_t size = sizeof(*list);
    char *elemptr;
    bool ok = true;

    if (*alenptr) {
        error_setg(errp, "array size property %s may not be set more than once",
                   name);
        return;
    }

    if (!visit_start_list(v, name, (GenericList **) &list, size, errp)) {
        return;
    }

    /* Read the whole input into a temporary list */
    elem = list;
    while (elem) {
        Property elem_prop;

        elem->value = g_malloc0(prop->arrayfieldsize);
        elem_prop = array_elem_prop(obj, prop, name, elem->value);
        prop->arrayinfo->set(obj, v, NULL, &elem_prop, errp);
        if (*errp) {
            ok = false;
            goto out_obj;
        }
        if (*alenptr == INT_MAX) {
            error_setg(errp, "array is too big");
            return;
        }
        (*alenptr)++;
        elem = (ArrayElementList *) visit_next_list(v, (GenericList*) elem,
                                                    size);
    }

    ok = visit_check_list(v, errp);
out_obj:
    visit_end_list(v, (void**) &list);

    if (!ok) {
        for (elem = list; elem; elem = next) {
            Property elem_prop = array_elem_prop(obj, prop, name,
                                                 elem->value);
            if (prop->arrayinfo->release) {
                prop->arrayinfo->release(obj, NULL, &elem_prop);
            }
            next = elem->next;
            g_free(elem->value);
            g_free(elem);
        }
        return;
    }

    /*
     * Now that we know how big the array has to be, move the data over to a
     * linear array and free the temporary list.
     */
    *arrayptr = g_malloc_n(*alenptr, prop->arrayfieldsize);
    elemptr = *arrayptr;
    for (elem = list; elem; elem = next) {
        memcpy(elemptr, elem->value, prop->arrayfieldsize);
        elemptr += prop->arrayfieldsize;
        next = elem->next;
        g_free(elem->value);
        g_free(elem);
    }
}

static void get_prop_array(Object *obj, Visitor *v, const char *name,
                           void *opaque, Error **errp)
{
    ERRP_GUARD();
    const Property *prop = opaque;
    uint32_t *alenptr = object_field_prop_ptr(obj, prop);
    void **arrayptr = (void *)obj + prop->arrayoffset;
    char *elemptr = *arrayptr;
    ArrayElementList *list = NULL, *elem;
    ArrayElementList **tail = &list;
    const size_t size = sizeof(*list);
    int i;
    bool ok;

    /* At least the string output visitor needs a real list */
    for (i = 0; i < *alenptr; i++) {
        elem = g_new0(ArrayElementList, 1);
        elem->value = elemptr;
        elemptr += prop->arrayfieldsize;

        *tail = elem;
        tail = &elem->next;
    }

    if (!visit_start_list(v, name, (GenericList **) &list, size, errp)) {
        return;
    }

    elem = list;
    while (elem) {
        Property elem_prop = array_elem_prop(obj, prop, name, elem->value);
        prop->arrayinfo->get(obj, v, NULL, &elem_prop, errp);
        if (*errp) {
            goto out_obj;
        }
        elem = (ArrayElementList *) visit_next_list(v, (GenericList*) elem,
                                                    size);
    }

    /* visit_check_list() can only fail for input visitors */
    ok = visit_check_list(v, errp);
    assert(ok);

out_obj:
    visit_end_list(v, (void**) &list);

    while (list) {
        elem = list;
        list = elem->next;
        g_free(elem);
    }
}

static void default_prop_array(ObjectProperty *op, const Property *prop)
{
    object_property_set_default_list(op);
}

const PropertyInfo qdev_prop_array = {
    .type = "list",
    .get = get_prop_array,
    .set = set_prop_array,
    .release = release_prop_array,
    .set_default_value = default_prop_array,
};

/* --- public helpers --- */

static const Property *qdev_prop_walk(DeviceClass *cls, const char *name)
{
    for (int i = 0, n = cls->props_count_; i < n; ++i) {
        const Property *prop = &cls->props_[i];
        if (strcmp(prop->name, name) == 0) {
            return prop;
        }
    }
    return NULL;
}

static const Property *qdev_prop_find(DeviceState *dev, const char *name)
{
    ObjectClass *class;
    const Property *prop;

    /* device properties */
    class = object_get_class(OBJECT(dev));
    do {
        prop = qdev_prop_walk(DEVICE_CLASS(class), name);
        if (prop) {
            return prop;
        }
        class = object_class_get_parent(class);
    } while (class != object_class_by_name(TYPE_DEVICE));

    return NULL;
}

void error_set_from_qdev_prop_error(Error **errp, int ret, Object *obj,
                                    const char *name, const char *value)
{
    switch (ret) {
    case -EEXIST:
        error_setg(errp, "Property '%s.%s' can't take value '%s', it's in use",
                  object_get_typename(obj), name, value);
        break;
    default:
    case -EINVAL:
        error_setg(errp, "Property '%s.%s' doesn't take value '%s'",
                   object_get_typename(obj), name, value);
        break;
    case -ENOENT:
        error_setg(errp, "Property '%s.%s' can't find value '%s'",
                  object_get_typename(obj), name, value);
        break;
    case 0:
        break;
    }
}

void qdev_prop_set_bit(DeviceState *dev, const char *name, bool value)
{
    object_property_set_bool(OBJECT(dev), name, value, &error_abort);
}

void qdev_prop_set_uint8(DeviceState *dev, const char *name, uint8_t value)
{
    object_property_set_int(OBJECT(dev), name, value, &error_abort);
}

void qdev_prop_set_uint16(DeviceState *dev, const char *name, uint16_t value)
{
    object_property_set_int(OBJECT(dev), name, value, &error_abort);
}

void qdev_prop_set_uint32(DeviceState *dev, const char *name, uint32_t value)
{
    object_property_set_int(OBJECT(dev), name, value, &error_abort);
}

void qdev_prop_set_int32(DeviceState *dev, const char *name, int32_t value)
{
    object_property_set_int(OBJECT(dev), name, value, &error_abort);
}

void qdev_prop_set_uint64(DeviceState *dev, const char *name, uint64_t value)
{
    object_property_set_int(OBJECT(dev), name, value, &error_abort);
}

void qdev_prop_set_string(DeviceState *dev, const char *name, const char *value)
{
    object_property_set_str(OBJECT(dev), name, value, &error_abort);
}

void qdev_prop_set_enum(DeviceState *dev, const char *name, int value)
{
    const Property *prop;

    prop = qdev_prop_find(dev, name);
    object_property_set_str(OBJECT(dev), name,
                            qapi_enum_lookup(prop->info->enum_table, value),
                            &error_abort);
}

void qdev_prop_set_array(DeviceState *dev, const char *name, QList *values)
{
    object_property_set_qobject(OBJECT(dev), name, QOBJECT(values),
                                &error_abort);
    qobject_unref(values);
}

static GPtrArray *global_props(void)
{
    static GPtrArray *gp;

    if (!gp) {
        gp = g_ptr_array_new();
    }

    return gp;
}

void qdev_prop_register_global(GlobalProperty *prop)
{
    g_ptr_array_add(global_props(), prop);
}

const GlobalProperty *qdev_find_global_prop(Object *obj,
                                            const char *name)
{
    GPtrArray *props = global_props();
    const GlobalProperty *p;
    int i;

    for (i = 0; i < props->len; i++) {
        p = g_ptr_array_index(props, i);
        if (object_dynamic_cast(obj, p->driver)
            && !strcmp(p->property, name)) {
            return p;
        }
    }
    return NULL;
}

int qdev_prop_check_globals(void)
{
    int i, ret = 0;

    for (i = 0; i < global_props()->len; i++) {
        GlobalProperty *prop;
        ObjectClass *oc;
        DeviceClass *dc;

        prop = g_ptr_array_index(global_props(), i);
        if (prop->used) {
            continue;
        }
        oc = object_class_by_name(prop->driver);
        oc = object_class_dynamic_cast(oc, TYPE_DEVICE);
        if (!oc) {
            warn_report("global %s.%s has invalid class name",
                        prop->driver, prop->property);
            ret = 1;
            continue;
        }
        dc = DEVICE_CLASS(oc);
        if (!dc->hotpluggable && !prop->used) {
            warn_report("global %s.%s=%s not used",
                        prop->driver, prop->property, prop->value);
            ret = 1;
            continue;
        }
    }
    return ret;
}

void qdev_prop_set_globals(DeviceState *dev)
{
    object_apply_global_props(OBJECT(dev), global_props(),
                              dev->hotplugged ? NULL : &error_fatal);
}

/* --- 64bit unsigned int 'size' type --- */

static void get_size(Object *obj, Visitor *v, const char *name, void *opaque,
                     Error **errp)
{
    const Property *prop = opaque;
    uint64_t *ptr = object_field_prop_ptr(obj, prop);

    visit_type_size(v, name, ptr, errp);
}

static void set_size(Object *obj, Visitor *v, const char *name, void *opaque,
                     Error **errp)
{
    const Property *prop = opaque;
    uint64_t *ptr = object_field_prop_ptr(obj, prop);

    visit_type_size(v, name, ptr, errp);
}

const PropertyInfo qdev_prop_size = {
    .type  = "size",
    .get = get_size,
    .set = set_size,
    .set_default_value = qdev_propinfo_set_default_value_uint,
};

/* --- object link property --- */

static ObjectProperty *create_link_property(ObjectClass *oc, const char *name,
                                            const Property *prop)
{
    return object_class_property_add_link(oc, name, prop->link_type,
                                          prop->offset,
                                          qdev_prop_allow_set_link_before_realize,
                                          OBJ_PROP_LINK_STRONG);
}

const PropertyInfo qdev_prop_link = {
    .type = "link",
    .create = create_link_property,
};

void qdev_property_add_static(DeviceState *dev, const Property *prop)
{
    Object *obj = OBJECT(dev);
    ObjectProperty *op;

    assert(!prop->info->create);

    op = object_property_add(obj, prop->name, prop->info->type,
                             field_prop_getter(prop->info),
                             field_prop_setter(prop->info),
                             prop->info->release,
                             (Property *)prop);

    object_property_set_description(obj, prop->name,
                                    prop->info->description);

    if (prop->set_default) {
        prop->info->set_default_value(op, prop);
        if (op->init) {
            op->init(obj, op);
        }
    }
}

static void qdev_class_add_property(DeviceClass *klass, const char *name,
                                    const Property *prop)
{
    ObjectClass *oc = OBJECT_CLASS(klass);
    ObjectProperty *op;

    if (prop->info->create) {
        op = prop->info->create(oc, name, prop);
    } else {
        op = object_class_property_add(oc,
                                       name, prop->info->type,
                                       field_prop_getter(prop->info),
                                       field_prop_setter(prop->info),
                                       prop->info->release,
                                       (Property *)prop);
    }
    if (prop->set_default) {
        prop->info->set_default_value(op, prop);
    }
    object_class_property_set_description(oc, name, prop->info->description);
}

/**
 * Legacy property handling
 */

static void qdev_get_legacy_property(Object *obj, Visitor *v,
                                     const char *name, void *opaque,
                                     Error **errp)
{
    const Property *prop = opaque;

    char buffer[1024];
    char *ptr = buffer;

    prop->info->print(obj, prop, buffer, sizeof(buffer));
    visit_type_str(v, name, &ptr, errp);
}

/**
 * qdev_class_add_legacy_property:
 * @dev: Device to add the property to.
 * @prop: The qdev property definition.
 *
 * Add a legacy QOM property to @dev for qdev property @prop.
 *
 * Legacy properties are string versions of QOM properties.  The format of
 * the string depends on the property type.  Legacy properties are only
 * needed for "info qtree".
 *
 * Do not use this in new code!  QOM Properties added through this interface
 * will be given names in the "legacy" namespace.
 */
static void qdev_class_add_legacy_property(DeviceClass *dc, const Property *prop)
{
    g_autofree char *name = NULL;

    /* Register pointer properties as legacy properties */
    if (!prop->info->print && prop->info->get) {
        return;
    }

    name = g_strdup_printf("legacy-%s", prop->name);
    object_class_property_add(OBJECT_CLASS(dc), name, "str",
        prop->info->print ? qdev_get_legacy_property : prop->info->get,
        NULL, NULL, (Property *)prop);
}

void device_class_set_props_n(DeviceClass *dc, const Property *props, size_t n)
{
    /* We used a hole in DeviceClass because that's still a lot. */
    assert(n <= UINT16_MAX);
    assert(n != 0);

    dc->props_ = props;
    dc->props_count_ = n;

    for (size_t i = 0; i < n; ++i) {
        const Property *prop = &props[i];
        assert(prop->name);
        qdev_class_add_legacy_property(dc, prop);
        qdev_class_add_property(dc, prop->name, prop);
    }
}

void qdev_alias_all_properties(DeviceState *target, Object *source)
{
    ObjectClass *class;
    ObjectPropertyIterator iter;
    ObjectProperty *prop;

    class = object_get_class(OBJECT(target));

    object_class_property_iter_init(&iter, class);
    while ((prop = object_property_iter_next(&iter))) {
        if (object_property_find(source, prop->name)) {
            continue; /* skip duplicate properties */
        }

        object_property_add_alias(source, prop->name,
                                  OBJECT(target), prop->name);
    }
}
