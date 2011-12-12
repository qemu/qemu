#ifndef QDEV_H
#define QDEV_H

#include "hw.h"
#include "qemu-queue.h"
#include "qemu-char.h"
#include "qemu-option.h"
#include "qapi/qapi-visit-core.h"

typedef struct Property Property;

typedef struct PropertyInfo PropertyInfo;

typedef struct CompatProperty CompatProperty;

typedef struct DeviceInfo DeviceInfo;

typedef struct BusState BusState;

typedef struct BusInfo BusInfo;

enum DevState {
    DEV_STATE_CREATED = 1,
    DEV_STATE_INITIALIZED,
};

enum {
    DEV_NVECTORS_UNSPECIFIED = -1,
};

/**
 * @DevicePropertyAccessor - called when trying to get/set a property
 *
 * @dev the device that owns the property
 * @v the visitor that contains the property data
 * @opaque the device property opaque
 * @name the name of the property
 * @errp a pointer to an Error that is filled if getting/setting fails.
 */
typedef void (DevicePropertyAccessor)(DeviceState *dev,
                                      Visitor *v,
                                      void *opaque,
                                      const char *name,
                                      Error **errp);

/**
 * @DevicePropertyRelease - called when a property is removed from a device
 *
 * @dev the device that owns the property
 * @name the name of the property
 * @opaque the opaque registered with the property
 */
typedef void (DevicePropertyRelease)(DeviceState *dev,
                                     const char *name,
                                     void *opaque);

typedef struct DeviceProperty
{
    gchar *name;
    gchar *type;
    DevicePropertyAccessor *get;
    DevicePropertyAccessor *set;
    DevicePropertyRelease *release;
    void *opaque;

    QTAILQ_ENTRY(DeviceProperty) node;
} DeviceProperty;

/* This structure should not be accessed directly.  We declare it here
   so that it can be embedded in individual device state structures.  */
struct DeviceState {
    const char *id;
    enum DevState state;
    QemuOpts *opts;
    int hotplugged;
    DeviceInfo *info;
    BusState *parent_bus;
    int num_gpio_out;
    qemu_irq *gpio_out;
    int num_gpio_in;
    qemu_irq *gpio_in;
    QLIST_HEAD(, BusState) child_bus;
    int num_child_bus;
    QTAILQ_ENTRY(DeviceState) sibling;
    int instance_id_alias;
    int alias_required_for_version;

    /**
     * This tracks the number of references between devices.  See @qdev_ref for
     * more information.
     */
    uint32_t ref;

    QTAILQ_HEAD(, DeviceProperty) properties;

    /* Do not, under any circumstance, use this parent link below anywhere
     * outside of qdev.c.  You have been warned. */
    DeviceState *parent;
};

typedef void (*bus_dev_printfn)(Monitor *mon, DeviceState *dev, int indent);
typedef char *(*bus_get_dev_path)(DeviceState *dev);
/*
 * This callback is used to create Open Firmware device path in accordance with
 * OF spec http://forthworks.com/standards/of1275.pdf. Indicidual bus bindings
 * can be found here http://playground.sun.com/1275/bindings/.
 */
typedef char *(*bus_get_fw_dev_path)(DeviceState *dev);
typedef int (qbus_resetfn)(BusState *bus);

struct BusInfo {
    const char *name;
    size_t size;
    bus_dev_printfn print_dev;
    bus_get_dev_path get_dev_path;
    bus_get_fw_dev_path get_fw_dev_path;
    qbus_resetfn *reset;
    Property *props;
};

struct BusState {
    DeviceState *parent;
    BusInfo *info;
    const char *name;
    int allow_hotplug;
    int qdev_allocated;
    QTAILQ_HEAD(ChildrenHead, DeviceState) children;
    QLIST_ENTRY(BusState) sibling;
};

struct Property {
    const char   *name;
    PropertyInfo *info;
    int          offset;
    int          bitnr;
    void         *defval;
};

enum PropertyType {
    PROP_TYPE_UNSPEC = 0,
    PROP_TYPE_UINT8,
    PROP_TYPE_UINT16,
    PROP_TYPE_UINT32,
    PROP_TYPE_INT32,
    PROP_TYPE_UINT64,
    PROP_TYPE_TADDR,
    PROP_TYPE_MACADDR,
    PROP_TYPE_DRIVE,
    PROP_TYPE_CHR,
    PROP_TYPE_STRING,
    PROP_TYPE_NETDEV,
    PROP_TYPE_VLAN,
    PROP_TYPE_PTR,
    PROP_TYPE_BIT,
};

struct PropertyInfo {
    const char *name;
    size_t size;
    enum PropertyType type;
    int (*parse)(DeviceState *dev, Property *prop, const char *str);
    int (*print)(DeviceState *dev, Property *prop, char *dest, size_t len);
    void (*free)(DeviceState *dev, Property *prop);
};

typedef struct GlobalProperty {
    const char *driver;
    const char *property;
    const char *value;
    QTAILQ_ENTRY(GlobalProperty) next;
} GlobalProperty;

/*** Board API.  This should go away once we have a machine config file.  ***/

DeviceState *qdev_create(BusState *bus, const char *name);
DeviceState *qdev_try_create(BusState *bus, const char *name);
int qdev_device_help(QemuOpts *opts);
DeviceState *qdev_device_add(QemuOpts *opts);
int qdev_init(DeviceState *dev) QEMU_WARN_UNUSED_RESULT;
void qdev_init_nofail(DeviceState *dev);
void qdev_set_legacy_instance_id(DeviceState *dev, int alias_id,
                                 int required_for_version);
int qdev_unplug(DeviceState *dev);
void qdev_free(DeviceState *dev);
int qdev_simple_unplug_cb(DeviceState *dev);
void qdev_machine_creation_done(void);
bool qdev_machine_modified(void);

qemu_irq qdev_get_gpio_in(DeviceState *dev, int n);
void qdev_connect_gpio_out(DeviceState *dev, int n, qemu_irq pin);

BusState *qdev_get_child_bus(DeviceState *dev, const char *name);

/*** Device API.  ***/

typedef int (*qdev_initfn)(DeviceState *dev, DeviceInfo *info);
typedef int (*qdev_event)(DeviceState *dev);
typedef void (*qdev_resetfn)(DeviceState *dev);

struct DeviceInfo {
    const char *name;
    const char *fw_name;
    const char *alias;
    const char *desc;
    size_t size;
    Property *props;
    int no_user;

    /* callbacks */
    qdev_resetfn reset;

    /* device state */
    const VMStateDescription *vmsd;

    /* Private to qdev / bus.  */
    qdev_initfn init;
    qdev_event unplug;
    qdev_event exit;
    BusInfo *bus_info;
    struct DeviceInfo *next;
};
extern DeviceInfo *device_info_list;

void qdev_register(DeviceInfo *info);

/* Register device properties.  */
/* GPIO inputs also double as IRQ sinks.  */
void qdev_init_gpio_in(DeviceState *dev, qemu_irq_handler handler, int n);
void qdev_init_gpio_out(DeviceState *dev, qemu_irq *pins, int n);

CharDriverState *qdev_init_chardev(DeviceState *dev);

BusState *qdev_get_parent_bus(DeviceState *dev);

/*** BUS API. ***/

DeviceState *qdev_find_recursive(BusState *bus, const char *id);

/* Returns 0 to walk children, > 0 to skip walk, < 0 to terminate walk. */
typedef int (qbus_walkerfn)(BusState *bus, void *opaque);
typedef int (qdev_walkerfn)(DeviceState *dev, void *opaque);

void qbus_create_inplace(BusState *bus, BusInfo *info,
                         DeviceState *parent, const char *name);
BusState *qbus_create(BusInfo *info, DeviceState *parent, const char *name);
/* Returns > 0 if either devfn or busfn skip walk somewhere in cursion,
 *         < 0 if either devfn or busfn terminate walk somewhere in cursion,
 *           0 otherwise. */
int qbus_walk_children(BusState *bus, qdev_walkerfn *devfn,
                       qbus_walkerfn *busfn, void *opaque);
int qdev_walk_children(DeviceState *dev, qdev_walkerfn *devfn,
                       qbus_walkerfn *busfn, void *opaque);
void qdev_reset_all(DeviceState *dev);
void qbus_reset_all_fn(void *opaque);

void qbus_free(BusState *bus);

#define FROM_QBUS(type, dev) DO_UPCAST(type, qbus, dev)

/* This should go away once we get rid of the NULL bus hack */
BusState *sysbus_get_default(void);

/*** monitor commands ***/

void do_info_qtree(Monitor *mon);
void do_info_qdm(Monitor *mon);
int do_device_add(Monitor *mon, const QDict *qdict, QObject **ret_data);
int do_device_del(Monitor *mon, const QDict *qdict, QObject **ret_data);

/*** qdev-properties.c ***/

extern PropertyInfo qdev_prop_bit;
extern PropertyInfo qdev_prop_uint8;
extern PropertyInfo qdev_prop_uint16;
extern PropertyInfo qdev_prop_uint32;
extern PropertyInfo qdev_prop_int32;
extern PropertyInfo qdev_prop_uint64;
extern PropertyInfo qdev_prop_hex8;
extern PropertyInfo qdev_prop_hex32;
extern PropertyInfo qdev_prop_hex64;
extern PropertyInfo qdev_prop_string;
extern PropertyInfo qdev_prop_chr;
extern PropertyInfo qdev_prop_ptr;
extern PropertyInfo qdev_prop_macaddr;
extern PropertyInfo qdev_prop_drive;
extern PropertyInfo qdev_prop_netdev;
extern PropertyInfo qdev_prop_vlan;
extern PropertyInfo qdev_prop_pci_devfn;

#define DEFINE_PROP(_name, _state, _field, _prop, _type) { \
        .name      = (_name),                                    \
        .info      = &(_prop),                                   \
        .offset    = offsetof(_state, _field)                    \
            + type_check(_type,typeof_field(_state, _field)),    \
        }
#define DEFINE_PROP_DEFAULT(_name, _state, _field, _defval, _prop, _type) { \
        .name      = (_name),                                           \
        .info      = &(_prop),                                          \
        .offset    = offsetof(_state, _field)                           \
            + type_check(_type,typeof_field(_state, _field)),           \
        .defval    = (_type[]) { _defval },                             \
        }
#define DEFINE_PROP_BIT(_name, _state, _field, _bit, _defval) {  \
        .name      = (_name),                                    \
        .info      = &(qdev_prop_bit),                           \
        .bitnr    = (_bit),                                      \
        .offset    = offsetof(_state, _field)                    \
            + type_check(uint32_t,typeof_field(_state, _field)), \
        .defval    = (bool[]) { (_defval) },                     \
        }

#define DEFINE_PROP_UINT8(_n, _s, _f, _d)                       \
    DEFINE_PROP_DEFAULT(_n, _s, _f, _d, qdev_prop_uint8, uint8_t)
#define DEFINE_PROP_UINT16(_n, _s, _f, _d)                      \
    DEFINE_PROP_DEFAULT(_n, _s, _f, _d, qdev_prop_uint16, uint16_t)
#define DEFINE_PROP_UINT32(_n, _s, _f, _d)                      \
    DEFINE_PROP_DEFAULT(_n, _s, _f, _d, qdev_prop_uint32, uint32_t)
#define DEFINE_PROP_INT32(_n, _s, _f, _d)                      \
    DEFINE_PROP_DEFAULT(_n, _s, _f, _d, qdev_prop_int32, int32_t)
#define DEFINE_PROP_UINT64(_n, _s, _f, _d)                      \
    DEFINE_PROP_DEFAULT(_n, _s, _f, _d, qdev_prop_uint64, uint64_t)
#define DEFINE_PROP_HEX8(_n, _s, _f, _d)                       \
    DEFINE_PROP_DEFAULT(_n, _s, _f, _d, qdev_prop_hex8, uint8_t)
#define DEFINE_PROP_HEX32(_n, _s, _f, _d)                       \
    DEFINE_PROP_DEFAULT(_n, _s, _f, _d, qdev_prop_hex32, uint32_t)
#define DEFINE_PROP_HEX64(_n, _s, _f, _d)                       \
    DEFINE_PROP_DEFAULT(_n, _s, _f, _d, qdev_prop_hex64, uint64_t)
#define DEFINE_PROP_PCI_DEVFN(_n, _s, _f, _d)                   \
    DEFINE_PROP_DEFAULT(_n, _s, _f, _d, qdev_prop_pci_devfn, uint32_t)

#define DEFINE_PROP_PTR(_n, _s, _f)             \
    DEFINE_PROP(_n, _s, _f, qdev_prop_ptr, void*)
#define DEFINE_PROP_CHR(_n, _s, _f)             \
    DEFINE_PROP(_n, _s, _f, qdev_prop_chr, CharDriverState*)
#define DEFINE_PROP_STRING(_n, _s, _f)             \
    DEFINE_PROP(_n, _s, _f, qdev_prop_string, char*)
#define DEFINE_PROP_NETDEV(_n, _s, _f)             \
    DEFINE_PROP(_n, _s, _f, qdev_prop_netdev, VLANClientState*)
#define DEFINE_PROP_VLAN(_n, _s, _f)             \
    DEFINE_PROP(_n, _s, _f, qdev_prop_vlan, VLANState*)
#define DEFINE_PROP_DRIVE(_n, _s, _f) \
    DEFINE_PROP(_n, _s, _f, qdev_prop_drive, BlockDriverState *)
#define DEFINE_PROP_MACADDR(_n, _s, _f)         \
    DEFINE_PROP(_n, _s, _f, qdev_prop_macaddr, MACAddr)

#define DEFINE_PROP_END_OF_LIST()               \
    {}

/* Set properties between creation and init.  */
void *qdev_get_prop_ptr(DeviceState *dev, Property *prop);
int qdev_prop_exists(DeviceState *dev, const char *name);
int qdev_prop_parse(DeviceState *dev, const char *name, const char *value);
void qdev_prop_set(DeviceState *dev, const char *name, void *src, enum PropertyType type);
void qdev_prop_set_bit(DeviceState *dev, const char *name, bool value);
void qdev_prop_set_uint8(DeviceState *dev, const char *name, uint8_t value);
void qdev_prop_set_uint16(DeviceState *dev, const char *name, uint16_t value);
void qdev_prop_set_uint32(DeviceState *dev, const char *name, uint32_t value);
void qdev_prop_set_int32(DeviceState *dev, const char *name, int32_t value);
void qdev_prop_set_uint64(DeviceState *dev, const char *name, uint64_t value);
void qdev_prop_set_string(DeviceState *dev, const char *name, char *value);
void qdev_prop_set_chr(DeviceState *dev, const char *name, CharDriverState *value);
void qdev_prop_set_netdev(DeviceState *dev, const char *name, VLANClientState *value);
void qdev_prop_set_vlan(DeviceState *dev, const char *name, VLANState *value);
int qdev_prop_set_drive(DeviceState *dev, const char *name, BlockDriverState *value) QEMU_WARN_UNUSED_RESULT;
void qdev_prop_set_drive_nofail(DeviceState *dev, const char *name, BlockDriverState *value);
void qdev_prop_set_macaddr(DeviceState *dev, const char *name, uint8_t *value);
/* FIXME: Remove opaque pointer properties.  */
void qdev_prop_set_ptr(DeviceState *dev, const char *name, void *value);
void qdev_prop_set_defaults(DeviceState *dev, Property *props);

void qdev_prop_register_global_list(GlobalProperty *props);
void qdev_prop_set_globals(DeviceState *dev);

static inline const char *qdev_fw_name(DeviceState *dev)
{
    return dev->info->fw_name ? : dev->info->alias ? : dev->info->name;
}

char *qdev_get_fw_dev_path(DeviceState *dev);
/* This is a nasty hack to allow passing a NULL bus to qdev_create.  */
extern struct BusInfo system_bus_info;

/**
 * @qdev_ref
 *
 * Increase the reference count of a device.  A device cannot be freed as long
 * as its reference count is greater than zero.
 *
 * @dev - the device
 */
void qdev_ref(DeviceState *dev);

/**
 * @qdef_unref
 *
 * Decrease the reference count of a device.  A device cannot be freed as long
 * as its reference count is greater than zero.
 *
 * @dev - the device
 */
void qdev_unref(DeviceState *dev);

/**
 * @qdev_property_add - add a new property to a device
 *
 * @dev - the device to add a property to
 *
 * @name - the name of the property.  This can contain any character except for
 *         a forward slash.  In general, you should use hyphens '-' instead of
 *         underscores '_' when naming properties.
 *
 * @type - the type name of the property.  This namespace is pretty loosely
 *         defined.  Sub namespaces are constructed by using a prefix and then
 *         to angle brackets.  For instance, the type 'virtio-net-pci' in the
 *         'link' namespace would be 'link<virtio-net-pci>'.
 *
 * @get - the getter to be called to read a property.  If this is NULL, then
 *        the property cannot be read.
 *
 * @set - the setter to be called to write a property.  If this is NULL,
 *        then the property cannot be written.
 *
 * @release - called when the property is removed from the device.  This is
 *            meant to allow a property to free its opaque upon device
 *            destruction.  This may be NULL.
 *
 * @opaque - an opaque pointer to pass to the callbacks for the property
 *
 * @errp - returns an error if this function fails
 */
void qdev_property_add(DeviceState *dev, const char *name, const char *type,
                       DevicePropertyAccessor *get, DevicePropertyAccessor *set,
                       DevicePropertyRelease *release,
                       void *opaque, Error **errp);

/**
 * @qdev_property_get - reads a property from a device
 *
 * @dev - the device
 *
 * @v - the visitor that will receive the property value.  This should be an
 *      Output visitor and the data will be written with @name as the name.
 *
 * @name - the name of the property
 *
 * @errp - returns an error if this function fails
 */
void qdev_property_get(DeviceState *dev, Visitor *v, const char *name,
                       Error **errp);

/**
 * @qdev_property_set - writes a property to a device
 *
 * @dev - the device
 *
 * @v - the visitor that will be used to write the property value.  This should
 *      be an Input visitor and the data will be first read with @name as the
 *      name and then written as the property value.
 *
 * @name - the name of the property
 *
 * @errp - returns an error if this function fails
 */
void qdev_property_set(DeviceState *dev, Visitor *v, const char *name,
                       Error **errp);

/**
 * @qdev_property_get_type - returns the type of a property
 *
 * @dev - the device
 *
 * @name - the name of the property
 *
 * @errp - returns an error if this function fails
 *
 * Returns:
 *   The type name of the property.
 */
const char *qdev_property_get_type(DeviceState *dev, const char *name,
                                   Error **errp);

/**
 * @qdev_property_add_legacy - add a legacy @Property to a device
 *
 * DO NOT USE THIS IN NEW CODE!
 */
void qdev_property_add_legacy(DeviceState *dev, Property *prop, Error **errp);

/**
 * @qdev_get_root - returns the root device of the composition tree
 *
 * Returns:
 *   The root of the composition tree.
 */
DeviceState *qdev_get_root(void);

/**
 * @qdev_get_canonical_path - returns the canonical path for a device.  This
 * is the path within the composition tree starting from the root.
 *
 * Returns:
 *   The canonical path in the composition tree.
 */
gchar *qdev_get_canonical_path(DeviceState *dev);

/**
 * @qdev_resolve_path - resolves a path returning a device
 *
 * There are two types of supported paths--absolute paths and partial paths.
 * 
 * Absolute paths are derived from the root device and can follow child<> or
 * link<> properties.  Since they can follow link<> properties, they can be
 * arbitrarily long.  Absolute paths look like absolute filenames and are
 * prefixed with a leading slash.
 * 
 * Partial paths look like relative filenames.  They do not begin with a
 * prefix.  The matching rules for partial paths are subtle but designed to make
 * specifying devices easy.  At each level of the composition tree, the partial
 * path is matched as an absolute path.  The first match is not returned.  At
 * least two matches are searched for.  A successful result is only returned if
 * only one match is founded.  If more than one match is found, a flag is
 * return to indicate that the match was ambiguous.
 *
 * @path - the path to resolve
 *
 * @ambiguous - returns true if the path resolution failed because of an
 *              ambiguous match
 *
 * Returns:
 *   The matched device or NULL on path lookup failure.
 */
DeviceState *qdev_resolve_path(const char *path, bool *ambiguous);

/**
 * @qdev_property_add_child - Add a child property to a device
 *
 * Child properties form the composition tree.  All devices need to be a child
 * of another device.  Devices can only be a child of one device.
 *
 * There is no way for a child to determine what its parent is.  It is not
 * a bidirectional relationship.  This is by design.
 *
 * @dev - the device to add a property to
 *
 * @name - the name of the property
 *
 * @child - the child device
 *
 * @errp - if an error occurs, a pointer to an area to store the area
 */
void qdev_property_add_child(DeviceState *dev, const char *name,
                             DeviceState *child, Error **errp);

/**
 * @qdev_property_add_link - Add a link property to a device
 *
 * Links establish relationships between devices.  Links are unidirectional
 * although two links can be combined to form a bidirectional relationship
 * between devices.
 *
 * Links form the graph in the device model.
 *
 * @dev - the device to add a property to
 *
 * @name - the name of the property
 *
 * @type - the qdev type of the link
 *
 * @child - a pointer to where the link device reference is stored
 *
 * @errp - if an error occurs, a pointer to an area to store the area
 */
void qdev_property_add_link(DeviceState *dev, const char *name,
                            const char *type, DeviceState **child,
                            Error **errp);

#endif
