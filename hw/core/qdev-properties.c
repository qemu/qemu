#include "net/net.h"
#include "hw/qdev.h"
#include "qapi/qmp/qerror.h"
#include "sysemu/blockdev.h"
#include "hw/block/block.h"
#include "net/hub.h"
#include "qapi/visitor.h"
#include "sysemu/char.h"

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

void *qdev_get_prop_ptr(DeviceState *dev, Property *prop)
{
    void *ptr = dev;
    ptr += prop->offset;
    return ptr;
}

static void get_enum(Object *obj, Visitor *v, void *opaque,
                     const char *name, Error **errp)
{
    DeviceState *dev = DEVICE(obj);
    Property *prop = opaque;
    int *ptr = qdev_get_prop_ptr(dev, prop);

    visit_type_enum(v, ptr, prop->info->enum_table,
                    prop->info->name, prop->name, errp);
}

static void set_enum(Object *obj, Visitor *v, void *opaque,
                     const char *name, Error **errp)
{
    DeviceState *dev = DEVICE(obj);
    Property *prop = opaque;
    int *ptr = qdev_get_prop_ptr(dev, prop);

    if (dev->realized) {
        qdev_prop_set_after_realize(dev, name, errp);
        return;
    }

    visit_type_enum(v, ptr, prop->info->enum_table,
                    prop->info->name, prop->name, errp);
}

/* Bit */

static uint32_t qdev_get_prop_mask(Property *prop)
{
    assert(prop->info == &qdev_prop_bit);
    return 0x1 << prop->bitnr;
}

static void bit_prop_set(DeviceState *dev, Property *props, bool val)
{
    uint32_t *p = qdev_get_prop_ptr(dev, props);
    uint32_t mask = qdev_get_prop_mask(props);
    if (val) {
        *p |= mask;
    } else {
        *p &= ~mask;
    }
}

static int prop_print_bit(DeviceState *dev, Property *prop, char *dest,
                          size_t len)
{
    uint32_t *p = qdev_get_prop_ptr(dev, prop);
    return snprintf(dest, len, (*p & qdev_get_prop_mask(prop)) ? "on" : "off");
}

static void prop_get_bit(Object *obj, Visitor *v, void *opaque,
                    const char *name, Error **errp)
{
    DeviceState *dev = DEVICE(obj);
    Property *prop = opaque;
    uint32_t *p = qdev_get_prop_ptr(dev, prop);
    bool value = (*p & qdev_get_prop_mask(prop)) != 0;

    visit_type_bool(v, &value, name, errp);
}

static void prop_set_bit(Object *obj, Visitor *v, void *opaque,
                    const char *name, Error **errp)
{
    DeviceState *dev = DEVICE(obj);
    Property *prop = opaque;
    Error *local_err = NULL;
    bool value;

    if (dev->realized) {
        qdev_prop_set_after_realize(dev, name, errp);
        return;
    }

    visit_type_bool(v, &value, name, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }
    bit_prop_set(dev, prop, value);
}

PropertyInfo qdev_prop_bit = {
    .name  = "boolean",
    .legacy_name  = "on/off",
    .print = prop_print_bit,
    .get   = prop_get_bit,
    .set   = prop_set_bit,
};

/* --- bool --- */

static void get_bool(Object *obj, Visitor *v, void *opaque,
                     const char *name, Error **errp)
{
    DeviceState *dev = DEVICE(obj);
    Property *prop = opaque;
    bool *ptr = qdev_get_prop_ptr(dev, prop);

    visit_type_bool(v, ptr, name, errp);
}

static void set_bool(Object *obj, Visitor *v, void *opaque,
                     const char *name, Error **errp)
{
    DeviceState *dev = DEVICE(obj);
    Property *prop = opaque;
    bool *ptr = qdev_get_prop_ptr(dev, prop);

    if (dev->realized) {
        qdev_prop_set_after_realize(dev, name, errp);
        return;
    }

    visit_type_bool(v, ptr, name, errp);
}

PropertyInfo qdev_prop_bool = {
    .name  = "boolean",
    .get   = get_bool,
    .set   = set_bool,
};

/* --- 8bit integer --- */

static void get_uint8(Object *obj, Visitor *v, void *opaque,
                      const char *name, Error **errp)
{
    DeviceState *dev = DEVICE(obj);
    Property *prop = opaque;
    uint8_t *ptr = qdev_get_prop_ptr(dev, prop);

    visit_type_uint8(v, ptr, name, errp);
}

static void set_uint8(Object *obj, Visitor *v, void *opaque,
                      const char *name, Error **errp)
{
    DeviceState *dev = DEVICE(obj);
    Property *prop = opaque;
    uint8_t *ptr = qdev_get_prop_ptr(dev, prop);

    if (dev->realized) {
        qdev_prop_set_after_realize(dev, name, errp);
        return;
    }

    visit_type_uint8(v, ptr, name, errp);
}

PropertyInfo qdev_prop_uint8 = {
    .name  = "uint8",
    .get   = get_uint8,
    .set   = set_uint8,
};

/* --- 8bit hex value --- */

static int parse_hex8(DeviceState *dev, Property *prop, const char *str)
{
    uint8_t *ptr = qdev_get_prop_ptr(dev, prop);
    char *end;

    if (str[0] != '0' || str[1] != 'x') {
        return -EINVAL;
    }

    *ptr = strtoul(str, &end, 16);
    if ((*end != '\0') || (end == str)) {
        return -EINVAL;
    }

    return 0;
}

static int print_hex8(DeviceState *dev, Property *prop, char *dest, size_t len)
{
    uint8_t *ptr = qdev_get_prop_ptr(dev, prop);
    return snprintf(dest, len, "0x%" PRIx8, *ptr);
}

PropertyInfo qdev_prop_hex8 = {
    .name  = "uint8",
    .legacy_name  = "hex8",
    .parse = parse_hex8,
    .print = print_hex8,
    .get   = get_uint8,
    .set   = set_uint8,
};

/* --- 16bit integer --- */

static void get_uint16(Object *obj, Visitor *v, void *opaque,
                       const char *name, Error **errp)
{
    DeviceState *dev = DEVICE(obj);
    Property *prop = opaque;
    uint16_t *ptr = qdev_get_prop_ptr(dev, prop);

    visit_type_uint16(v, ptr, name, errp);
}

static void set_uint16(Object *obj, Visitor *v, void *opaque,
                       const char *name, Error **errp)
{
    DeviceState *dev = DEVICE(obj);
    Property *prop = opaque;
    uint16_t *ptr = qdev_get_prop_ptr(dev, prop);

    if (dev->realized) {
        qdev_prop_set_after_realize(dev, name, errp);
        return;
    }

    visit_type_uint16(v, ptr, name, errp);
}

PropertyInfo qdev_prop_uint16 = {
    .name  = "uint16",
    .get   = get_uint16,
    .set   = set_uint16,
};

/* --- 32bit integer --- */

static void get_uint32(Object *obj, Visitor *v, void *opaque,
                       const char *name, Error **errp)
{
    DeviceState *dev = DEVICE(obj);
    Property *prop = opaque;
    uint32_t *ptr = qdev_get_prop_ptr(dev, prop);

    visit_type_uint32(v, ptr, name, errp);
}

static void set_uint32(Object *obj, Visitor *v, void *opaque,
                       const char *name, Error **errp)
{
    DeviceState *dev = DEVICE(obj);
    Property *prop = opaque;
    uint32_t *ptr = qdev_get_prop_ptr(dev, prop);

    if (dev->realized) {
        qdev_prop_set_after_realize(dev, name, errp);
        return;
    }

    visit_type_uint32(v, ptr, name, errp);
}

static void get_int32(Object *obj, Visitor *v, void *opaque,
                      const char *name, Error **errp)
{
    DeviceState *dev = DEVICE(obj);
    Property *prop = opaque;
    int32_t *ptr = qdev_get_prop_ptr(dev, prop);

    visit_type_int32(v, ptr, name, errp);
}

static void set_int32(Object *obj, Visitor *v, void *opaque,
                      const char *name, Error **errp)
{
    DeviceState *dev = DEVICE(obj);
    Property *prop = opaque;
    int32_t *ptr = qdev_get_prop_ptr(dev, prop);

    if (dev->realized) {
        qdev_prop_set_after_realize(dev, name, errp);
        return;
    }

    visit_type_int32(v, ptr, name, errp);
}

PropertyInfo qdev_prop_uint32 = {
    .name  = "uint32",
    .get   = get_uint32,
    .set   = set_uint32,
};

PropertyInfo qdev_prop_int32 = {
    .name  = "int32",
    .get   = get_int32,
    .set   = set_int32,
};

/* --- 32bit hex value --- */

static int parse_hex32(DeviceState *dev, Property *prop, const char *str)
{
    uint32_t *ptr = qdev_get_prop_ptr(dev, prop);
    char *end;

    if (str[0] != '0' || str[1] != 'x') {
        return -EINVAL;
    }

    *ptr = strtoul(str, &end, 16);
    if ((*end != '\0') || (end == str)) {
        return -EINVAL;
    }

    return 0;
}

static int print_hex32(DeviceState *dev, Property *prop, char *dest, size_t len)
{
    uint32_t *ptr = qdev_get_prop_ptr(dev, prop);
    return snprintf(dest, len, "0x%" PRIx32, *ptr);
}

PropertyInfo qdev_prop_hex32 = {
    .name  = "uint32",
    .legacy_name  = "hex32",
    .parse = parse_hex32,
    .print = print_hex32,
    .get   = get_uint32,
    .set   = set_uint32,
};

/* --- 64bit integer --- */

static void get_uint64(Object *obj, Visitor *v, void *opaque,
                       const char *name, Error **errp)
{
    DeviceState *dev = DEVICE(obj);
    Property *prop = opaque;
    uint64_t *ptr = qdev_get_prop_ptr(dev, prop);

    visit_type_uint64(v, ptr, name, errp);
}

static void set_uint64(Object *obj, Visitor *v, void *opaque,
                       const char *name, Error **errp)
{
    DeviceState *dev = DEVICE(obj);
    Property *prop = opaque;
    uint64_t *ptr = qdev_get_prop_ptr(dev, prop);

    if (dev->realized) {
        qdev_prop_set_after_realize(dev, name, errp);
        return;
    }

    visit_type_uint64(v, ptr, name, errp);
}

PropertyInfo qdev_prop_uint64 = {
    .name  = "uint64",
    .get   = get_uint64,
    .set   = set_uint64,
};

/* --- 64bit hex value --- */

static int parse_hex64(DeviceState *dev, Property *prop, const char *str)
{
    uint64_t *ptr = qdev_get_prop_ptr(dev, prop);
    char *end;

    if (str[0] != '0' || str[1] != 'x') {
        return -EINVAL;
    }

    *ptr = strtoull(str, &end, 16);
    if ((*end != '\0') || (end == str)) {
        return -EINVAL;
    }

    return 0;
}

static int print_hex64(DeviceState *dev, Property *prop, char *dest, size_t len)
{
    uint64_t *ptr = qdev_get_prop_ptr(dev, prop);
    return snprintf(dest, len, "0x%" PRIx64, *ptr);
}

PropertyInfo qdev_prop_hex64 = {
    .name  = "uint64",
    .legacy_name  = "hex64",
    .parse = parse_hex64,
    .print = print_hex64,
    .get   = get_uint64,
    .set   = set_uint64,
};

/* --- string --- */

static void release_string(Object *obj, const char *name, void *opaque)
{
    Property *prop = opaque;
    g_free(*(char **)qdev_get_prop_ptr(DEVICE(obj), prop));
}

static int print_string(DeviceState *dev, Property *prop, char *dest,
                        size_t len)
{
    char **ptr = qdev_get_prop_ptr(dev, prop);
    if (!*ptr) {
        return snprintf(dest, len, "<null>");
    }
    return snprintf(dest, len, "\"%s\"", *ptr);
}

static void get_string(Object *obj, Visitor *v, void *opaque,
                       const char *name, Error **errp)
{
    DeviceState *dev = DEVICE(obj);
    Property *prop = opaque;
    char **ptr = qdev_get_prop_ptr(dev, prop);

    if (!*ptr) {
        char *str = (char *)"";
        visit_type_str(v, &str, name, errp);
    } else {
        visit_type_str(v, ptr, name, errp);
    }
}

static void set_string(Object *obj, Visitor *v, void *opaque,
                       const char *name, Error **errp)
{
    DeviceState *dev = DEVICE(obj);
    Property *prop = opaque;
    char **ptr = qdev_get_prop_ptr(dev, prop);
    Error *local_err = NULL;
    char *str;

    if (dev->realized) {
        qdev_prop_set_after_realize(dev, name, errp);
        return;
    }

    visit_type_str(v, &str, name, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }
    if (*ptr) {
        g_free(*ptr);
    }
    *ptr = str;
}

PropertyInfo qdev_prop_string = {
    .name  = "string",
    .print = print_string,
    .release = release_string,
    .get   = get_string,
    .set   = set_string,
};

/* --- pointer --- */

/* Not a proper property, just for dirty hacks.  TODO Remove it!  */
PropertyInfo qdev_prop_ptr = {
    .name  = "ptr",
};

/* --- mac address --- */

/*
 * accepted syntax versions:
 *   01:02:03:04:05:06
 *   01-02-03-04-05-06
 */
static void get_mac(Object *obj, Visitor *v, void *opaque,
                    const char *name, Error **errp)
{
    DeviceState *dev = DEVICE(obj);
    Property *prop = opaque;
    MACAddr *mac = qdev_get_prop_ptr(dev, prop);
    char buffer[2 * 6 + 5 + 1];
    char *p = buffer;

    snprintf(buffer, sizeof(buffer), "%02x:%02x:%02x:%02x:%02x:%02x",
             mac->a[0], mac->a[1], mac->a[2],
             mac->a[3], mac->a[4], mac->a[5]);

    visit_type_str(v, &p, name, errp);
}

static void set_mac(Object *obj, Visitor *v, void *opaque,
                    const char *name, Error **errp)
{
    DeviceState *dev = DEVICE(obj);
    Property *prop = opaque;
    MACAddr *mac = qdev_get_prop_ptr(dev, prop);
    Error *local_err = NULL;
    int i, pos;
    char *str, *p;

    if (dev->realized) {
        qdev_prop_set_after_realize(dev, name, errp);
        return;
    }

    visit_type_str(v, &str, name, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    for (i = 0, pos = 0; i < 6; i++, pos += 3) {
        if (!qemu_isxdigit(str[pos])) {
            goto inval;
        }
        if (!qemu_isxdigit(str[pos+1])) {
            goto inval;
        }
        if (i == 5) {
            if (str[pos+2] != '\0') {
                goto inval;
            }
        } else {
            if (str[pos+2] != ':' && str[pos+2] != '-') {
                goto inval;
            }
        }
        mac->a[i] = strtol(str+pos, &p, 16);
    }
    g_free(str);
    return;

inval:
    error_set_from_qdev_prop_error(errp, EINVAL, dev, prop, str);
    g_free(str);
}

PropertyInfo qdev_prop_macaddr = {
    .name  = "macaddr",
    .get   = get_mac,
    .set   = set_mac,
};

/* --- lost tick policy --- */

static const char *lost_tick_policy_table[LOST_TICK_MAX+1] = {
    [LOST_TICK_DISCARD] = "discard",
    [LOST_TICK_DELAY] = "delay",
    [LOST_TICK_MERGE] = "merge",
    [LOST_TICK_SLEW] = "slew",
    [LOST_TICK_MAX] = NULL,
};

QEMU_BUILD_BUG_ON(sizeof(LostTickPolicy) != sizeof(int));

PropertyInfo qdev_prop_losttickpolicy = {
    .name  = "LostTickPolicy",
    .enum_table  = lost_tick_policy_table,
    .get   = get_enum,
    .set   = set_enum,
};

/* --- BIOS CHS translation */

static const char *bios_chs_trans_table[] = {
    [BIOS_ATA_TRANSLATION_AUTO] = "auto",
    [BIOS_ATA_TRANSLATION_NONE] = "none",
    [BIOS_ATA_TRANSLATION_LBA]  = "lba",
};

PropertyInfo qdev_prop_bios_chs_trans = {
    .name = "bios-chs-trans",
    .enum_table = bios_chs_trans_table,
    .get = get_enum,
    .set = set_enum,
};

/* --- pci address --- */

/*
 * bus-local address, i.e. "$slot" or "$slot.$fn"
 */
static void set_pci_devfn(Object *obj, Visitor *v, void *opaque,
                          const char *name, Error **errp)
{
    DeviceState *dev = DEVICE(obj);
    Property *prop = opaque;
    int32_t value, *ptr = qdev_get_prop_ptr(dev, prop);
    unsigned int slot, fn, n;
    Error *local_err = NULL;
    char *str;

    if (dev->realized) {
        qdev_prop_set_after_realize(dev, name, errp);
        return;
    }

    visit_type_str(v, &str, name, &local_err);
    if (local_err) {
        error_free(local_err);
        local_err = NULL;
        visit_type_int32(v, &value, name, &local_err);
        if (local_err) {
            error_propagate(errp, local_err);
        } else if (value < -1 || value > 255) {
            error_set(errp, QERR_INVALID_PARAMETER_VALUE, name ? name : "null",
                      "pci_devfn");
        } else {
            *ptr = value;
        }
        return;
    }

    if (sscanf(str, "%x.%x%n", &slot, &fn, &n) != 2) {
        fn = 0;
        if (sscanf(str, "%x%n", &slot, &n) != 1) {
            goto invalid;
        }
    }
    if (str[n] != '\0' || fn > 7 || slot > 31) {
        goto invalid;
    }
    *ptr = slot << 3 | fn;
    g_free(str);
    return;

invalid:
    error_set_from_qdev_prop_error(errp, EINVAL, dev, prop, str);
    g_free(str);
}

static int print_pci_devfn(DeviceState *dev, Property *prop, char *dest,
                           size_t len)
{
    int32_t *ptr = qdev_get_prop_ptr(dev, prop);

    if (*ptr == -1) {
        return snprintf(dest, len, "<unset>");
    } else {
        return snprintf(dest, len, "%02x.%x", *ptr >> 3, *ptr & 7);
    }
}

PropertyInfo qdev_prop_pci_devfn = {
    .name  = "int32",
    .legacy_name  = "pci-devfn",
    .print = print_pci_devfn,
    .get   = get_int32,
    .set   = set_pci_devfn,
};

/* --- blocksize --- */

static void set_blocksize(Object *obj, Visitor *v, void *opaque,
                          const char *name, Error **errp)
{
    DeviceState *dev = DEVICE(obj);
    Property *prop = opaque;
    uint16_t value, *ptr = qdev_get_prop_ptr(dev, prop);
    Error *local_err = NULL;
    const int64_t min = 512;
    const int64_t max = 32768;

    if (dev->realized) {
        qdev_prop_set_after_realize(dev, name, errp);
        return;
    }

    visit_type_uint16(v, &value, name, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }
    if (value < min || value > max) {
        error_set(errp, QERR_PROPERTY_VALUE_OUT_OF_RANGE,
                  dev->id?:"", name, (int64_t)value, min, max);
        return;
    }

    /* We rely on power-of-2 blocksizes for bitmasks */
    if ((value & (value - 1)) != 0) {
        error_set(errp, QERR_PROPERTY_VALUE_NOT_POWER_OF_2,
                  dev->id?:"", name, (int64_t)value);
        return;
    }

    *ptr = value;
}

PropertyInfo qdev_prop_blocksize = {
    .name  = "blocksize",
    .get   = get_uint16,
    .set   = set_blocksize,
};

/* --- pci host address --- */

static void get_pci_host_devaddr(Object *obj, Visitor *v, void *opaque,
                                 const char *name, Error **errp)
{
    DeviceState *dev = DEVICE(obj);
    Property *prop = opaque;
    PCIHostDeviceAddress *addr = qdev_get_prop_ptr(dev, prop);
    char buffer[] = "xxxx:xx:xx.x";
    char *p = buffer;
    int rc = 0;

    rc = snprintf(buffer, sizeof(buffer), "%04x:%02x:%02x.%d",
                  addr->domain, addr->bus, addr->slot, addr->function);
    assert(rc == sizeof(buffer) - 1);

    visit_type_str(v, &p, name, errp);
}

/*
 * Parse [<domain>:]<bus>:<slot>.<func>
 *   if <domain> is not supplied, it's assumed to be 0.
 */
static void set_pci_host_devaddr(Object *obj, Visitor *v, void *opaque,
                                 const char *name, Error **errp)
{
    DeviceState *dev = DEVICE(obj);
    Property *prop = opaque;
    PCIHostDeviceAddress *addr = qdev_get_prop_ptr(dev, prop);
    Error *local_err = NULL;
    char *str, *p;
    char *e;
    unsigned long val;
    unsigned long dom = 0, bus = 0;
    unsigned int slot = 0, func = 0;

    if (dev->realized) {
        qdev_prop_set_after_realize(dev, name, errp);
        return;
    }

    visit_type_str(v, &str, name, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    p = str;
    val = strtoul(p, &e, 16);
    if (e == p || *e != ':') {
        goto inval;
    }
    bus = val;

    p = e + 1;
    val = strtoul(p, &e, 16);
    if (e == p) {
        goto inval;
    }
    if (*e == ':') {
        dom = bus;
        bus = val;
        p = e + 1;
        val = strtoul(p, &e, 16);
        if (e == p) {
            goto inval;
        }
    }
    slot = val;

    if (*e != '.') {
        goto inval;
    }
    p = e + 1;
    val = strtoul(p, &e, 10);
    if (e == p) {
        goto inval;
    }
    func = val;

    if (dom > 0xffff || bus > 0xff || slot > 0x1f || func > 7) {
        goto inval;
    }

    if (*e) {
        goto inval;
    }

    addr->domain = dom;
    addr->bus = bus;
    addr->slot = slot;
    addr->function = func;

    g_free(str);
    return;

inval:
    error_set_from_qdev_prop_error(errp, EINVAL, dev, prop, str);
    g_free(str);
}

PropertyInfo qdev_prop_pci_host_devaddr = {
    .name = "pci-host-devaddr",
    .get = get_pci_host_devaddr,
    .set = set_pci_host_devaddr,
};

/* --- support for array properties --- */

/* Used as an opaque for the object properties we add for each
 * array element. Note that the struct Property must be first
 * in the struct so that a pointer to this works as the opaque
 * for the underlying element's property hooks as well as for
 * our own release callback.
 */
typedef struct {
    struct Property prop;
    char *propname;
    ObjectPropertyRelease *release;
} ArrayElementProperty;

/* object property release callback for array element properties:
 * we call the underlying element's property release hook, and
 * then free the memory we allocated when we added the property.
 */
static void array_element_release(Object *obj, const char *name, void *opaque)
{
    ArrayElementProperty *p = opaque;
    if (p->release) {
        p->release(obj, name, opaque);
    }
    g_free(p->propname);
    g_free(p);
}

static void set_prop_arraylen(Object *obj, Visitor *v, void *opaque,
                              const char *name, Error **errp)
{
    /* Setter for the property which defines the length of a
     * variable-sized property array. As well as actually setting the
     * array-length field in the device struct, we have to create the
     * array itself and dynamically add the corresponding properties.
     */
    DeviceState *dev = DEVICE(obj);
    Property *prop = opaque;
    uint32_t *alenptr = qdev_get_prop_ptr(dev, prop);
    void **arrayptr = (void *)dev + prop->arrayoffset;
    void *eltptr;
    const char *arrayname;
    int i;

    if (dev->realized) {
        qdev_prop_set_after_realize(dev, name, errp);
        return;
    }
    if (*alenptr) {
        error_setg(errp, "array size property %s may not be set more than once",
                   name);
        return;
    }
    visit_type_uint32(v, alenptr, name, errp);
    if (error_is_set(errp)) {
        return;
    }
    if (!*alenptr) {
        return;
    }

    /* DEFINE_PROP_ARRAY guarantees that name should start with this prefix;
     * strip it off so we can get the name of the array itself.
     */
    assert(strncmp(name, PROP_ARRAY_LEN_PREFIX,
                   strlen(PROP_ARRAY_LEN_PREFIX)) == 0);
    arrayname = name + strlen(PROP_ARRAY_LEN_PREFIX);

    /* Note that it is the responsibility of the individual device's deinit
     * to free the array proper.
     */
    *arrayptr = eltptr = g_malloc0(*alenptr * prop->arrayfieldsize);
    for (i = 0; i < *alenptr; i++, eltptr += prop->arrayfieldsize) {
        char *propname = g_strdup_printf("%s[%d]", arrayname, i);
        ArrayElementProperty *arrayprop = g_new0(ArrayElementProperty, 1);
        arrayprop->release = prop->arrayinfo->release;
        arrayprop->propname = propname;
        arrayprop->prop.info = prop->arrayinfo;
        arrayprop->prop.name = propname;
        /* This ugly piece of pointer arithmetic sets up the offset so
         * that when the underlying get/set hooks call qdev_get_prop_ptr
         * they get the right answer despite the array element not actually
         * being inside the device struct.
         */
        arrayprop->prop.offset = eltptr - (void *)dev;
        assert(qdev_get_prop_ptr(dev, &arrayprop->prop) == eltptr);
        object_property_add(obj, propname,
                            arrayprop->prop.info->name,
                            arrayprop->prop.info->get,
                            arrayprop->prop.info->set,
                            array_element_release,
                            arrayprop, errp);
        if (error_is_set(errp)) {
            return;
        }
    }
}

PropertyInfo qdev_prop_arraylen = {
    .name = "uint32",
    .get = get_uint32,
    .set = set_prop_arraylen,
};

/* --- public helpers --- */

static Property *qdev_prop_walk(Property *props, const char *name)
{
    if (!props) {
        return NULL;
    }
    while (props->name) {
        if (strcmp(props->name, name) == 0) {
            return props;
        }
        props++;
    }
    return NULL;
}

static Property *qdev_prop_find(DeviceState *dev, const char *name)
{
    ObjectClass *class;
    Property *prop;

    /* device properties */
    class = object_get_class(OBJECT(dev));
    do {
        prop = qdev_prop_walk(DEVICE_CLASS(class)->props, name);
        if (prop) {
            return prop;
        }
        class = object_class_get_parent(class);
    } while (class != object_class_by_name(TYPE_DEVICE));

    return NULL;
}

void error_set_from_qdev_prop_error(Error **errp, int ret, DeviceState *dev,
                                    Property *prop, const char *value)
{
    switch (ret) {
    case -EEXIST:
        error_set(errp, QERR_PROPERTY_VALUE_IN_USE,
                  object_get_typename(OBJECT(dev)), prop->name, value);
        break;
    default:
    case -EINVAL:
        error_set(errp, QERR_PROPERTY_VALUE_BAD,
                  object_get_typename(OBJECT(dev)), prop->name, value);
        break;
    case -ENOENT:
        error_set(errp, QERR_PROPERTY_VALUE_NOT_FOUND,
                  object_get_typename(OBJECT(dev)), prop->name, value);
        break;
    case 0:
        break;
    }
}

void qdev_prop_parse(DeviceState *dev, const char *name, const char *value,
                     Error **errp)
{
    char *legacy_name;

    legacy_name = g_strdup_printf("legacy-%s", name);
    if (object_property_get_type(OBJECT(dev), legacy_name, NULL)) {
        object_property_parse(OBJECT(dev), value, legacy_name, errp);
    } else {
        object_property_parse(OBJECT(dev), value, name, errp);
    }
    g_free(legacy_name);
}

void qdev_prop_set_bit(DeviceState *dev, const char *name, bool value)
{
    Error *errp = NULL;
    object_property_set_bool(OBJECT(dev), value, name, &errp);
    assert_no_error(errp);
}

void qdev_prop_set_uint8(DeviceState *dev, const char *name, uint8_t value)
{
    Error *errp = NULL;
    object_property_set_int(OBJECT(dev), value, name, &errp);
    assert_no_error(errp);
}

void qdev_prop_set_uint16(DeviceState *dev, const char *name, uint16_t value)
{
    Error *errp = NULL;
    object_property_set_int(OBJECT(dev), value, name, &errp);
    assert_no_error(errp);
}

void qdev_prop_set_uint32(DeviceState *dev, const char *name, uint32_t value)
{
    Error *errp = NULL;
    object_property_set_int(OBJECT(dev), value, name, &errp);
    assert_no_error(errp);
}

void qdev_prop_set_int32(DeviceState *dev, const char *name, int32_t value)
{
    Error *errp = NULL;
    object_property_set_int(OBJECT(dev), value, name, &errp);
    assert_no_error(errp);
}

void qdev_prop_set_uint64(DeviceState *dev, const char *name, uint64_t value)
{
    Error *errp = NULL;
    object_property_set_int(OBJECT(dev), value, name, &errp);
    assert_no_error(errp);
}

void qdev_prop_set_string(DeviceState *dev, const char *name, const char *value)
{
    Error *errp = NULL;
    object_property_set_str(OBJECT(dev), value, name, &errp);
    assert_no_error(errp);
}

void qdev_prop_set_macaddr(DeviceState *dev, const char *name, uint8_t *value)
{
    Error *errp = NULL;
    char str[2 * 6 + 5 + 1];
    snprintf(str, sizeof(str), "%02x:%02x:%02x:%02x:%02x:%02x",
             value[0], value[1], value[2], value[3], value[4], value[5]);

    object_property_set_str(OBJECT(dev), str, name, &errp);
    assert_no_error(errp);
}

void qdev_prop_set_enum(DeviceState *dev, const char *name, int value)
{
    Property *prop;
    Error *errp = NULL;

    prop = qdev_prop_find(dev, name);
    object_property_set_str(OBJECT(dev), prop->info->enum_table[value],
                            name, &errp);
    assert_no_error(errp);
}

void qdev_prop_set_ptr(DeviceState *dev, const char *name, void *value)
{
    Property *prop;
    void **ptr;

    prop = qdev_prop_find(dev, name);
    assert(prop && prop->info == &qdev_prop_ptr);
    ptr = qdev_get_prop_ptr(dev, prop);
    *ptr = value;
}

static QTAILQ_HEAD(, GlobalProperty) global_props =
        QTAILQ_HEAD_INITIALIZER(global_props);

void qdev_prop_register_global(GlobalProperty *prop)
{
    QTAILQ_INSERT_TAIL(&global_props, prop, next);
}

void qdev_prop_register_global_list(GlobalProperty *props)
{
    int i;

    for (i = 0; props[i].driver != NULL; i++) {
        qdev_prop_register_global(props+i);
    }
}

void qdev_prop_set_globals_for_type(DeviceState *dev, const char *typename,
                                    Error **errp)
{
    GlobalProperty *prop;

    QTAILQ_FOREACH(prop, &global_props, next) {
        Error *err = NULL;

        if (strcmp(typename, prop->driver) != 0) {
            continue;
        }
        qdev_prop_parse(dev, prop->property, prop->value, &err);
        if (err != NULL) {
            error_propagate(errp, err);
            return;
        }
    }
}

void qdev_prop_set_globals(DeviceState *dev, Error **errp)
{
    ObjectClass *class = object_get_class(OBJECT(dev));

    do {
        Error *err = NULL;

        qdev_prop_set_globals_for_type(dev, object_class_get_name(class),
                                       &err);
        if (err != NULL) {
            error_propagate(errp, err);
            return;
        }
        class = object_class_get_parent(class);
    } while (class);
}

/* --- 64bit unsigned int 'size' type --- */

static void get_size(Object *obj, Visitor *v, void *opaque,
                     const char *name, Error **errp)
{
    DeviceState *dev = DEVICE(obj);
    Property *prop = opaque;
    uint64_t *ptr = qdev_get_prop_ptr(dev, prop);

    visit_type_size(v, ptr, name, errp);
}

static void set_size(Object *obj, Visitor *v, void *opaque,
                     const char *name, Error **errp)
{
    DeviceState *dev = DEVICE(obj);
    Property *prop = opaque;
    uint64_t *ptr = qdev_get_prop_ptr(dev, prop);

    visit_type_size(v, ptr, name, errp);
}

static int parse_size(DeviceState *dev, Property *prop, const char *str)
{
    uint64_t *ptr = qdev_get_prop_ptr(dev, prop);
    Error *errp = NULL;

    if (str != NULL) {
        parse_option_size(prop->name, str, ptr, &errp);
    }
    assert_no_error(errp);
    return 0;
}

static int print_size(DeviceState *dev, Property *prop, char *dest, size_t len)
{
    static const char suffixes[] = { 'B', 'K', 'M', 'G', 'T' };
    uint64_t div, val = *(uint64_t *)qdev_get_prop_ptr(dev, prop);
    int i;

    /* Compute floor(log2(val)).  */
    i = 64 - clz64(val);

    /* Find the power of 1024 that we'll display as the units.  */
    i /= 10;
    if (i >= ARRAY_SIZE(suffixes)) {
        i = ARRAY_SIZE(suffixes) - 1;
    }
    div = 1ULL << (i * 10);

    return snprintf(dest, len, "%0.03f%c", (double)val/div, suffixes[i]);
}

PropertyInfo qdev_prop_size = {
    .name  = "size",
    .parse = parse_size,
    .print = print_size,
    .get = get_size,
    .set = set_size,
};
