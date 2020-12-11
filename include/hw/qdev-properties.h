#ifndef QEMU_QDEV_PROPERTIES_H
#define QEMU_QDEV_PROPERTIES_H

#include "hw/qdev-core.h"

/**
 * Property:
 * @set_default: true if the default value should be set from @defval,
 *    in which case @info->set_default_value must not be NULL
 *    (if false then no default value is set by the property system
 *     and the field retains whatever value it was given by instance_init).
 * @defval: default value for the property. This is used only if @set_default
 *     is true.
 */
struct Property {
    const char   *name;
    const PropertyInfo *info;
    ptrdiff_t    offset;
    uint8_t      bitnr;
    bool         set_default;
    union {
        int64_t i;
        uint64_t u;
    } defval;
    int          arrayoffset;
    const PropertyInfo *arrayinfo;
    int          arrayfieldsize;
    const char   *link_type;
};

struct PropertyInfo {
    const char *name;
    const char *description;
    const QEnumLookup *enum_table;
    int (*print)(Object *obj, Property *prop, char *dest, size_t len);
    void (*set_default_value)(ObjectProperty *op, const Property *prop);
    ObjectProperty *(*create)(ObjectClass *oc, const char *name,
                              Property *prop);
    ObjectPropertyAccessor *get;
    ObjectPropertyAccessor *set;
    ObjectPropertyRelease *release;
};


/*** qdev-properties.c ***/

extern const PropertyInfo qdev_prop_bit;
extern const PropertyInfo qdev_prop_bit64;
extern const PropertyInfo qdev_prop_bool;
extern const PropertyInfo qdev_prop_enum;
extern const PropertyInfo qdev_prop_uint8;
extern const PropertyInfo qdev_prop_uint16;
extern const PropertyInfo qdev_prop_uint32;
extern const PropertyInfo qdev_prop_int32;
extern const PropertyInfo qdev_prop_uint64;
extern const PropertyInfo qdev_prop_int64;
extern const PropertyInfo qdev_prop_size;
extern const PropertyInfo qdev_prop_string;
extern const PropertyInfo qdev_prop_on_off_auto;
extern const PropertyInfo qdev_prop_size32;
extern const PropertyInfo qdev_prop_arraylen;
extern const PropertyInfo qdev_prop_link;

#define DEFINE_PROP(_name, _state, _field, _prop, _type, ...) {  \
        .name      = (_name),                                    \
        .info      = &(_prop),                                   \
        .offset    = offsetof(_state, _field)                    \
            + type_check(_type, typeof_field(_state, _field)),   \
        __VA_ARGS__                                              \
        }

#define DEFINE_PROP_SIGNED(_name, _state, _field, _defval, _prop, _type) \
    DEFINE_PROP(_name, _state, _field, _prop, _type,                     \
                .set_default = true,                                     \
                .defval.i    = (_type)_defval)

#define DEFINE_PROP_SIGNED_NODEFAULT(_name, _state, _field, _prop, _type) \
    DEFINE_PROP(_name, _state, _field, _prop, _type)

#define DEFINE_PROP_BIT(_name, _state, _field, _bit, _defval)   \
    DEFINE_PROP(_name, _state, _field, qdev_prop_bit, uint32_t, \
                .bitnr       = (_bit),                          \
                .set_default = true,                            \
                .defval.u    = (bool)_defval)

#define DEFINE_PROP_UNSIGNED(_name, _state, _field, _defval, _prop, _type) \
    DEFINE_PROP(_name, _state, _field, _prop, _type,                       \
                .set_default = true,                                       \
                .defval.u  = (_type)_defval)

#define DEFINE_PROP_UNSIGNED_NODEFAULT(_name, _state, _field, _prop, _type) \
    DEFINE_PROP(_name, _state, _field, _prop, _type)

#define DEFINE_PROP_BIT64(_name, _state, _field, _bit, _defval)   \
    DEFINE_PROP(_name, _state, _field, qdev_prop_bit64, uint64_t, \
                .bitnr    = (_bit),                               \
                .set_default = true,                              \
                .defval.u  = (bool)_defval)

#define DEFINE_PROP_BOOL(_name, _state, _field, _defval)     \
    DEFINE_PROP(_name, _state, _field, qdev_prop_bool, bool, \
                .set_default = true,                         \
                .defval.u    = (bool)_defval)

#define PROP_ARRAY_LEN_PREFIX "len-"

/**
 * DEFINE_PROP_ARRAY:
 * @_name: name of the array
 * @_state: name of the device state structure type
 * @_field: uint32_t field in @_state to hold the array length
 * @_arrayfield: field in @_state (of type '@_arraytype *') which
 *               will point to the array
 * @_arrayprop: PropertyInfo defining what property the array elements have
 * @_arraytype: C type of the array elements
 *
 * Define device properties for a variable-length array _name.  A
 * static property "len-arrayname" is defined. When the device creator
 * sets this property to the desired length of array, further dynamic
 * properties "arrayname[0]", "arrayname[1]", ...  are defined so the
 * device creator can set the array element values. Setting the
 * "len-arrayname" property more than once is an error.
 *
 * When the array length is set, the @_field member of the device
 * struct is set to the array length, and @_arrayfield is set to point
 * to (zero-initialised) memory allocated for the array.  For a zero
 * length array, @_field will be set to 0 and @_arrayfield to NULL.
 * It is the responsibility of the device deinit code to free the
 * @_arrayfield memory.
 */
#define DEFINE_PROP_ARRAY(_name, _state, _field,               \
                          _arrayfield, _arrayprop, _arraytype) \
    DEFINE_PROP((PROP_ARRAY_LEN_PREFIX _name),                 \
                _state, _field, qdev_prop_arraylen, uint32_t,  \
                .set_default = true,                           \
                .defval.u = 0,                                 \
                .arrayinfo = &(_arrayprop),                    \
                .arrayfieldsize = sizeof(_arraytype),          \
                .arrayoffset = offsetof(_state, _arrayfield))

#define DEFINE_PROP_LINK(_name, _state, _field, _type, _ptr_type)     \
    DEFINE_PROP(_name, _state, _field, qdev_prop_link, _ptr_type,     \
                .link_type  = _type)

#define DEFINE_PROP_UINT8(_n, _s, _f, _d)                       \
    DEFINE_PROP_UNSIGNED(_n, _s, _f, _d, qdev_prop_uint8, uint8_t)
#define DEFINE_PROP_UINT16(_n, _s, _f, _d)                      \
    DEFINE_PROP_UNSIGNED(_n, _s, _f, _d, qdev_prop_uint16, uint16_t)
#define DEFINE_PROP_UINT32(_n, _s, _f, _d)                      \
    DEFINE_PROP_UNSIGNED(_n, _s, _f, _d, qdev_prop_uint32, uint32_t)
#define DEFINE_PROP_INT32(_n, _s, _f, _d)                      \
    DEFINE_PROP_SIGNED(_n, _s, _f, _d, qdev_prop_int32, int32_t)
#define DEFINE_PROP_UINT64(_n, _s, _f, _d)                      \
    DEFINE_PROP_UNSIGNED(_n, _s, _f, _d, qdev_prop_uint64, uint64_t)
#define DEFINE_PROP_INT64(_n, _s, _f, _d)                      \
    DEFINE_PROP_SIGNED(_n, _s, _f, _d, qdev_prop_int64, int64_t)
#define DEFINE_PROP_SIZE(_n, _s, _f, _d)                       \
    DEFINE_PROP_UNSIGNED(_n, _s, _f, _d, qdev_prop_size, uint64_t)
#define DEFINE_PROP_STRING(_n, _s, _f)             \
    DEFINE_PROP(_n, _s, _f, qdev_prop_string, char*)
#define DEFINE_PROP_ON_OFF_AUTO(_n, _s, _f, _d) \
    DEFINE_PROP_SIGNED(_n, _s, _f, _d, qdev_prop_on_off_auto, OnOffAuto)
#define DEFINE_PROP_SIZE32(_n, _s, _f, _d)                       \
    DEFINE_PROP_UNSIGNED(_n, _s, _f, _d, qdev_prop_size32, uint32_t)

#define DEFINE_PROP_END_OF_LIST()               \
    {}

/*
 * Set properties between creation and realization.
 *
 * Returns: %true on success, %false on error.
 */
bool qdev_prop_set_drive_err(DeviceState *dev, const char *name,
                             BlockBackend *value, Error **errp);

/*
 * Set properties between creation and realization.
 * @value must be valid.  Each property may be set at most once.
 */
void qdev_prop_set_bit(DeviceState *dev, const char *name, bool value);
void qdev_prop_set_uint8(DeviceState *dev, const char *name, uint8_t value);
void qdev_prop_set_uint16(DeviceState *dev, const char *name, uint16_t value);
void qdev_prop_set_uint32(DeviceState *dev, const char *name, uint32_t value);
void qdev_prop_set_int32(DeviceState *dev, const char *name, int32_t value);
void qdev_prop_set_uint64(DeviceState *dev, const char *name, uint64_t value);
void qdev_prop_set_string(DeviceState *dev, const char *name, const char *value);
void qdev_prop_set_chr(DeviceState *dev, const char *name, Chardev *value);
void qdev_prop_set_netdev(DeviceState *dev, const char *name, NetClientState *value);
void qdev_prop_set_drive(DeviceState *dev, const char *name,
                         BlockBackend *value);
void qdev_prop_set_macaddr(DeviceState *dev, const char *name,
                           const uint8_t *value);
void qdev_prop_set_enum(DeviceState *dev, const char *name, int value);

void *object_field_prop_ptr(Object *obj, Property *prop);

void qdev_prop_register_global(GlobalProperty *prop);
const GlobalProperty *qdev_find_global_prop(Object *obj,
                                            const char *name);
int qdev_prop_check_globals(void);
void qdev_prop_set_globals(DeviceState *dev);
void error_set_from_qdev_prop_error(Error **errp, int ret, Object *obj,
                                    const char *name, const char *value);

/**
 * qdev_property_add_static:
 * @dev: Device to add the property to.
 * @prop: The qdev property definition.
 *
 * Add a static QOM property to @dev for qdev property @prop.
 * On error, store error in @errp.  Static properties access data in a struct.
 * The type of the QOM property is derived from prop->info.
 */
void qdev_property_add_static(DeviceState *dev, Property *prop);

/**
 * qdev_alias_all_properties: Create aliases on source for all target properties
 * @target: Device which has properties to be aliased
 * @source: Object to add alias properties to
 *
 * Add alias properties to the @source object for all qdev properties on
 * the @target DeviceState.
 *
 * This is useful when @target is an internal implementation object
 * owned by @source, and you want to expose all the properties of that
 * implementation object as properties on the @source object so that users
 * of @source can set them.
 */
void qdev_alias_all_properties(DeviceState *target, Object *source);

/**
 * @qdev_prop_set_after_realize:
 * @dev: device
 * @name: name of property
 * @errp: indirect pointer to Error to be set
 * Set the Error object to report that an attempt was made to set a property
 * on a device after it has already been realized. This is a utility function
 * which allows property-setter functions to easily report the error in
 * a friendly format identifying both the device and the property.
 */
void qdev_prop_set_after_realize(DeviceState *dev, const char *name,
                                 Error **errp);

/**
 * qdev_prop_allow_set_link_before_realize:
 *
 * Set the #Error object if an attempt is made to set the link after realize.
 * This function should be used as the check() argument to
 * object_property_add_link().
 */
void qdev_prop_allow_set_link_before_realize(const Object *obj,
                                             const char *name,
                                             Object *val, Error **errp);

#endif
