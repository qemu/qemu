#include "net.h"
#include "qdev.h"
#include "qerror.h"
#include "blockdev.h"

void *qdev_get_prop_ptr(DeviceState *dev, Property *prop)
{
    void *ptr = dev;
    ptr += prop->offset;
    return ptr;
}

static uint32_t qdev_get_prop_mask(Property *prop)
{
    assert(prop->info->type == PROP_TYPE_BIT);
    return 0x1 << prop->bitnr;
}

static void bit_prop_set(DeviceState *dev, Property *props, bool val)
{
    uint32_t *p = qdev_get_prop_ptr(dev, props);
    uint32_t mask = qdev_get_prop_mask(props);
    if (val)
        *p |= mask;
    else
        *p &= ~mask;
}

static void qdev_prop_cpy(DeviceState *dev, Property *props, void *src)
{
    if (props->info->type == PROP_TYPE_BIT) {
        bool *defval = src;
        bit_prop_set(dev, props, *defval);
    } else {
        char *dst = qdev_get_prop_ptr(dev, props);
        memcpy(dst, src, props->info->size);
    }
}

/* Bit */
static int parse_bit(DeviceState *dev, Property *prop, const char *str)
{
    if (!strcasecmp(str, "on"))
        bit_prop_set(dev, prop, true);
    else if (!strcasecmp(str, "off"))
        bit_prop_set(dev, prop, false);
    else
        return -EINVAL;
    return 0;
}

static int print_bit(DeviceState *dev, Property *prop, char *dest, size_t len)
{
    uint32_t *p = qdev_get_prop_ptr(dev, prop);
    return snprintf(dest, len, (*p & qdev_get_prop_mask(prop)) ? "on" : "off");
}

static void get_bit(Object *obj, Visitor *v, void *opaque,
                    const char *name, Error **errp)
{
    DeviceState *dev = DEVICE(obj);
    Property *prop = opaque;
    uint32_t *p = qdev_get_prop_ptr(dev, prop);
    bool value = (*p & qdev_get_prop_mask(prop)) != 0;

    visit_type_bool(v, &value, name, errp);
}

static void set_bit(Object *obj, Visitor *v, void *opaque,
                    const char *name, Error **errp)
{
    DeviceState *dev = DEVICE(obj);
    Property *prop = opaque;
    Error *local_err = NULL;
    bool value;

    if (dev->state != DEV_STATE_CREATED) {
        error_set(errp, QERR_PERMISSION_DENIED);
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
    .type  = PROP_TYPE_BIT,
    .size  = sizeof(uint32_t),
    .parse = parse_bit,
    .print = print_bit,
    .get   = get_bit,
    .set   = set_bit,
};

/* --- 8bit integer --- */

static int parse_uint8(DeviceState *dev, Property *prop, const char *str)
{
    uint8_t *ptr = qdev_get_prop_ptr(dev, prop);
    char *end;

    /* accept both hex and decimal */
    *ptr = strtoul(str, &end, 0);
    if ((*end != '\0') || (end == str)) {
        return -EINVAL;
    }

    return 0;
}

static int print_uint8(DeviceState *dev, Property *prop, char *dest, size_t len)
{
    uint8_t *ptr = qdev_get_prop_ptr(dev, prop);
    return snprintf(dest, len, "%" PRIu8, *ptr);
}

static void get_int8(Object *obj, Visitor *v, void *opaque,
                     const char *name, Error **errp)
{
    DeviceState *dev = DEVICE(obj);
    Property *prop = opaque;
    int8_t *ptr = qdev_get_prop_ptr(dev, prop);
    int64_t value;

    value = *ptr;
    visit_type_int(v, &value, name, errp);
}

static void set_int8(Object *obj, Visitor *v, void *opaque,
                     const char *name, Error **errp)
{
    DeviceState *dev = DEVICE(obj);
    Property *prop = opaque;
    int8_t *ptr = qdev_get_prop_ptr(dev, prop);
    Error *local_err = NULL;
    int64_t value;

    if (dev->state != DEV_STATE_CREATED) {
        error_set(errp, QERR_PERMISSION_DENIED);
        return;
    }

    visit_type_int(v, &value, name, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }
    if (value > prop->info->min && value <= prop->info->max) {
        *ptr = value;
    } else {
        error_set(errp, QERR_PROPERTY_VALUE_OUT_OF_RANGE,
                  dev->id?:"", name, value, prop->info->min,
                  prop->info->max);
    }
}

PropertyInfo qdev_prop_uint8 = {
    .name  = "uint8",
    .type  = PROP_TYPE_UINT8,
    .size  = sizeof(uint8_t),
    .parse = parse_uint8,
    .print = print_uint8,
    .get   = get_int8,
    .set   = set_int8,
    .min   = 0,
    .max   = 255,
};

/* --- 8bit hex value --- */

static int parse_hex8(DeviceState *dev, Property *prop, const char *str)
{
    uint8_t *ptr = qdev_get_prop_ptr(dev, prop);
    char *end;

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
    .type  = PROP_TYPE_UINT8,
    .size  = sizeof(uint8_t),
    .parse = parse_hex8,
    .print = print_hex8,
    .get   = get_int8,
    .set   = set_int8,
    .min   = 0,
    .max   = 255,
};

/* --- 16bit integer --- */

static int parse_uint16(DeviceState *dev, Property *prop, const char *str)
{
    uint16_t *ptr = qdev_get_prop_ptr(dev, prop);
    char *end;

    /* accept both hex and decimal */
    *ptr = strtoul(str, &end, 0);
    if ((*end != '\0') || (end == str)) {
        return -EINVAL;
    }

    return 0;
}

static int print_uint16(DeviceState *dev, Property *prop, char *dest, size_t len)
{
    uint16_t *ptr = qdev_get_prop_ptr(dev, prop);
    return snprintf(dest, len, "%" PRIu16, *ptr);
}

static void get_int16(Object *obj, Visitor *v, void *opaque,
                      const char *name, Error **errp)
{
    DeviceState *dev = DEVICE(obj);
    Property *prop = opaque;
    int16_t *ptr = qdev_get_prop_ptr(dev, prop);
    int64_t value;

    value = *ptr;
    visit_type_int(v, &value, name, errp);
}

static void set_int16(Object *obj, Visitor *v, void *opaque,
                      const char *name, Error **errp)
{
    DeviceState *dev = DEVICE(obj);
    Property *prop = opaque;
    int16_t *ptr = qdev_get_prop_ptr(dev, prop);
    Error *local_err = NULL;
    int64_t value;

    if (dev->state != DEV_STATE_CREATED) {
        error_set(errp, QERR_PERMISSION_DENIED);
        return;
    }

    visit_type_int(v, &value, name, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }
    if (value > prop->info->min && value <= prop->info->max) {
        *ptr = value;
    } else {
        error_set(errp, QERR_PROPERTY_VALUE_OUT_OF_RANGE,
                  dev->id?:"", name, value, prop->info->min,
                  prop->info->max);
    }
}

PropertyInfo qdev_prop_uint16 = {
    .name  = "uint16",
    .type  = PROP_TYPE_UINT16,
    .size  = sizeof(uint16_t),
    .parse = parse_uint16,
    .print = print_uint16,
    .get   = get_int16,
    .set   = set_int16,
    .min   = 0,
    .max   = 65535,
};

/* --- 32bit integer --- */

static int parse_uint32(DeviceState *dev, Property *prop, const char *str)
{
    uint32_t *ptr = qdev_get_prop_ptr(dev, prop);
    char *end;

    /* accept both hex and decimal */
    *ptr = strtoul(str, &end, 0);
    if ((*end != '\0') || (end == str)) {
        return -EINVAL;
    }

    return 0;
}

static int print_uint32(DeviceState *dev, Property *prop, char *dest, size_t len)
{
    uint32_t *ptr = qdev_get_prop_ptr(dev, prop);
    return snprintf(dest, len, "%" PRIu32, *ptr);
}

static void get_int32(Object *obj, Visitor *v, void *opaque,
                      const char *name, Error **errp)
{
    DeviceState *dev = DEVICE(obj);
    Property *prop = opaque;
    int32_t *ptr = qdev_get_prop_ptr(dev, prop);
    int64_t value;

    value = *ptr;
    visit_type_int(v, &value, name, errp);
}

static void set_int32(Object *obj, Visitor *v, void *opaque,
                      const char *name, Error **errp)
{
    DeviceState *dev = DEVICE(obj);
    Property *prop = opaque;
    int32_t *ptr = qdev_get_prop_ptr(dev, prop);
    Error *local_err = NULL;
    int64_t value;

    if (dev->state != DEV_STATE_CREATED) {
        error_set(errp, QERR_PERMISSION_DENIED);
        return;
    }

    visit_type_int(v, &value, name, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }
    if (value > prop->info->min && value <= prop->info->max) {
        *ptr = value;
    } else {
        error_set(errp, QERR_PROPERTY_VALUE_OUT_OF_RANGE,
                  dev->id?:"", name, value, prop->info->min,
                  prop->info->max);
    }
}

PropertyInfo qdev_prop_uint32 = {
    .name  = "uint32",
    .type  = PROP_TYPE_UINT32,
    .size  = sizeof(uint32_t),
    .parse = parse_uint32,
    .print = print_uint32,
    .get   = get_int32,
    .set   = set_int32,
    .min   = 0,
    .max   = 0xFFFFFFFFULL,
};

static int parse_int32(DeviceState *dev, Property *prop, const char *str)
{
    int32_t *ptr = qdev_get_prop_ptr(dev, prop);
    char *end;

    *ptr = strtol(str, &end, 10);
    if ((*end != '\0') || (end == str)) {
        return -EINVAL;
    }

    return 0;
}

static int print_int32(DeviceState *dev, Property *prop, char *dest, size_t len)
{
    int32_t *ptr = qdev_get_prop_ptr(dev, prop);
    return snprintf(dest, len, "%" PRId32, *ptr);
}

PropertyInfo qdev_prop_int32 = {
    .name  = "int32",
    .type  = PROP_TYPE_INT32,
    .size  = sizeof(int32_t),
    .parse = parse_int32,
    .print = print_int32,
    .get   = get_int32,
    .set   = set_int32,
    .min   = -0x80000000LL,
    .max   = 0x7FFFFFFFLL,
};

/* --- 32bit hex value --- */

static int parse_hex32(DeviceState *dev, Property *prop, const char *str)
{
    uint32_t *ptr = qdev_get_prop_ptr(dev, prop);
    char *end;

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
    .type  = PROP_TYPE_UINT32,
    .size  = sizeof(uint32_t),
    .parse = parse_hex32,
    .print = print_hex32,
    .get   = get_int32,
    .set   = set_int32,
    .min   = 0,
    .max   = 0xFFFFFFFFULL,
};

/* --- 64bit integer --- */

static int parse_uint64(DeviceState *dev, Property *prop, const char *str)
{
    uint64_t *ptr = qdev_get_prop_ptr(dev, prop);
    char *end;

    /* accept both hex and decimal */
    *ptr = strtoull(str, &end, 0);
    if ((*end != '\0') || (end == str)) {
        return -EINVAL;
    }

    return 0;
}

static int print_uint64(DeviceState *dev, Property *prop, char *dest, size_t len)
{
    uint64_t *ptr = qdev_get_prop_ptr(dev, prop);
    return snprintf(dest, len, "%" PRIu64, *ptr);
}

static void get_int64(Object *obj, Visitor *v, void *opaque,
                      const char *name, Error **errp)
{
    DeviceState *dev = DEVICE(obj);
    Property *prop = opaque;
    int64_t *ptr = qdev_get_prop_ptr(dev, prop);

    visit_type_int(v, ptr, name, errp);
}

static void set_int64(Object *obj, Visitor *v, void *opaque,
                      const char *name, Error **errp)
{
    DeviceState *dev = DEVICE(obj);
    Property *prop = opaque;
    int64_t *ptr = qdev_get_prop_ptr(dev, prop);

    if (dev->state != DEV_STATE_CREATED) {
        error_set(errp, QERR_PERMISSION_DENIED);
        return;
    }

    visit_type_int(v, ptr, name, errp);
}

PropertyInfo qdev_prop_uint64 = {
    .name  = "uint64",
    .type  = PROP_TYPE_UINT64,
    .size  = sizeof(uint64_t),
    .parse = parse_uint64,
    .print = print_uint64,
    .get   = get_int64,
    .set   = set_int64,
};

/* --- 64bit hex value --- */

static int parse_hex64(DeviceState *dev, Property *prop, const char *str)
{
    uint64_t *ptr = qdev_get_prop_ptr(dev, prop);
    char *end;

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
    .type  = PROP_TYPE_UINT64,
    .size  = sizeof(uint64_t),
    .parse = parse_hex64,
    .print = print_hex64,
    .get   = get_int64,
    .set   = set_int64,
};

/* --- string --- */

static void free_string(DeviceState *dev, Property *prop)
{
    g_free(*(char **)qdev_get_prop_ptr(dev, prop));
}

static int print_string(DeviceState *dev, Property *prop, char *dest, size_t len)
{
    char **ptr = qdev_get_prop_ptr(dev, prop);
    if (!*ptr)
        return snprintf(dest, len, "<null>");
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

    if (dev->state != DEV_STATE_CREATED) {
        error_set(errp, QERR_PERMISSION_DENIED);
        return;
    }

    visit_type_str(v, &str, name, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }
    if (!*str) {
        g_free(str);
        str = NULL;
    }
    if (*ptr) {
        g_free(*ptr);
    }
    *ptr = str;
}

PropertyInfo qdev_prop_string = {
    .name  = "string",
    .type  = PROP_TYPE_STRING,
    .size  = sizeof(char*),
    .print = print_string,
    .free  = free_string,
    .get   = get_string,
    .set   = set_string,
};

/* --- drive --- */

static int parse_drive(DeviceState *dev, Property *prop, const char *str)
{
    BlockDriverState **ptr = qdev_get_prop_ptr(dev, prop);
    BlockDriverState *bs;

    bs = bdrv_find(str);
    if (bs == NULL)
        return -ENOENT;
    if (bdrv_attach_dev(bs, dev) < 0)
        return -EEXIST;
    *ptr = bs;
    return 0;
}

static void free_drive(DeviceState *dev, Property *prop)
{
    BlockDriverState **ptr = qdev_get_prop_ptr(dev, prop);

    if (*ptr) {
        bdrv_detach_dev(*ptr, dev);
        blockdev_auto_del(*ptr);
    }
}

static int print_drive(DeviceState *dev, Property *prop, char *dest, size_t len)
{
    BlockDriverState **ptr = qdev_get_prop_ptr(dev, prop);
    return snprintf(dest, len, "%s",
                    *ptr ? bdrv_get_device_name(*ptr) : "<null>");
}

static void get_generic(Object *obj, Visitor *v, void *opaque,
                       const char *name, Error **errp)
{
    DeviceState *dev = DEVICE(obj);
    Property *prop = opaque;
    void **ptr = qdev_get_prop_ptr(dev, prop);
    char buffer[1024];
    char *p = buffer;

    buffer[0] = 0;
    if (*ptr) {
        prop->info->print(dev, prop, buffer, sizeof(buffer));
    }
    visit_type_str(v, &p, name, errp);
}

static void set_generic(Object *obj, Visitor *v, void *opaque,
                        const char *name, Error **errp)
{
    DeviceState *dev = DEVICE(obj);
    Property *prop = opaque;
    Error *local_err = NULL;
    char *str;
    int ret;

    if (dev->state != DEV_STATE_CREATED) {
        error_set(errp, QERR_PERMISSION_DENIED);
        return;
    }

    visit_type_str(v, &str, name, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }
    if (!*str) {
        g_free(str);
        error_set_from_qdev_prop_error(errp, EINVAL, dev, prop, str);
        return;
    }
    ret = prop->info->parse(dev, prop, str);
    error_set_from_qdev_prop_error(errp, ret, dev, prop, str);
    g_free(str);
}

PropertyInfo qdev_prop_drive = {
    .name  = "drive",
    .type  = PROP_TYPE_DRIVE,
    .size  = sizeof(BlockDriverState *),
    .parse = parse_drive,
    .print = print_drive,
    .get   = get_generic,
    .set   = set_generic,
    .free  = free_drive,
};

/* --- character device --- */

static int parse_chr(DeviceState *dev, Property *prop, const char *str)
{
    CharDriverState **ptr = qdev_get_prop_ptr(dev, prop);

    *ptr = qemu_chr_find(str);
    if (*ptr == NULL) {
        return -ENOENT;
    }
    if ((*ptr)->avail_connections < 1) {
        return -EEXIST;
    }
    --(*ptr)->avail_connections;
    return 0;
}

static void free_chr(DeviceState *dev, Property *prop)
{
    CharDriverState **ptr = qdev_get_prop_ptr(dev, prop);

    if (*ptr) {
        qemu_chr_add_handlers(*ptr, NULL, NULL, NULL, NULL);
    }
}


static int print_chr(DeviceState *dev, Property *prop, char *dest, size_t len)
{
    CharDriverState **ptr = qdev_get_prop_ptr(dev, prop);

    if (*ptr && (*ptr)->label) {
        return snprintf(dest, len, "%s", (*ptr)->label);
    } else {
        return snprintf(dest, len, "<null>");
    }
}

PropertyInfo qdev_prop_chr = {
    .name  = "chr",
    .type  = PROP_TYPE_CHR,
    .size  = sizeof(CharDriverState*),
    .parse = parse_chr,
    .print = print_chr,
    .get   = get_generic,
    .set   = set_generic,
    .free  = free_chr,
};

/* --- netdev device --- */

static int parse_netdev(DeviceState *dev, Property *prop, const char *str)
{
    VLANClientState **ptr = qdev_get_prop_ptr(dev, prop);

    *ptr = qemu_find_netdev(str);
    if (*ptr == NULL)
        return -ENOENT;
    if ((*ptr)->peer) {
        return -EEXIST;
    }
    return 0;
}

static int print_netdev(DeviceState *dev, Property *prop, char *dest, size_t len)
{
    VLANClientState **ptr = qdev_get_prop_ptr(dev, prop);

    if (*ptr && (*ptr)->name) {
        return snprintf(dest, len, "%s", (*ptr)->name);
    } else {
        return snprintf(dest, len, "<null>");
    }
}

PropertyInfo qdev_prop_netdev = {
    .name  = "netdev",
    .type  = PROP_TYPE_NETDEV,
    .size  = sizeof(VLANClientState*),
    .parse = parse_netdev,
    .print = print_netdev,
    .get   = get_generic,
    .set   = set_generic,
};

/* --- vlan --- */

static int parse_vlan(DeviceState *dev, Property *prop, const char *str)
{
    VLANState **ptr = qdev_get_prop_ptr(dev, prop);
    int id;

    if (sscanf(str, "%d", &id) != 1)
        return -EINVAL;
    *ptr = qemu_find_vlan(id, 1);
    if (*ptr == NULL)
        return -ENOENT;
    return 0;
}

static int print_vlan(DeviceState *dev, Property *prop, char *dest, size_t len)
{
    VLANState **ptr = qdev_get_prop_ptr(dev, prop);

    if (*ptr) {
        return snprintf(dest, len, "%d", (*ptr)->id);
    } else {
        return snprintf(dest, len, "<null>");
    }
}

static void get_vlan(Object *obj, Visitor *v, void *opaque,
                     const char *name, Error **errp)
{
    DeviceState *dev = DEVICE(obj);
    Property *prop = opaque;
    VLANState **ptr = qdev_get_prop_ptr(dev, prop);
    int64_t id;

    id = *ptr ? (*ptr)->id : -1;
    visit_type_int(v, &id, name, errp);
}

static void set_vlan(Object *obj, Visitor *v, void *opaque,
                     const char *name, Error **errp)
{
    DeviceState *dev = DEVICE(obj);
    Property *prop = opaque;
    VLANState **ptr = qdev_get_prop_ptr(dev, prop);
    Error *local_err = NULL;
    int64_t id;
    VLANState *vlan;

    if (dev->state != DEV_STATE_CREATED) {
        error_set(errp, QERR_PERMISSION_DENIED);
        return;
    }

    visit_type_int(v, &id, name, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }
    if (id == -1) {
        *ptr = NULL;
        return;
    }
    vlan = qemu_find_vlan(id, 1);
    if (!vlan) {
        error_set(errp, QERR_INVALID_PARAMETER_VALUE,
                  name, prop->info->name);
        return;
    }
    *ptr = vlan;
}

PropertyInfo qdev_prop_vlan = {
    .name  = "vlan",
    .type  = PROP_TYPE_VLAN,
    .size  = sizeof(VLANClientState*),
    .parse = parse_vlan,
    .print = print_vlan,
    .get   = get_vlan,
    .set   = set_vlan,
};

/* --- pointer --- */

/* Not a proper property, just for dirty hacks.  TODO Remove it!  */
PropertyInfo qdev_prop_ptr = {
    .name  = "ptr",
    .type  = PROP_TYPE_PTR,
    .size  = sizeof(void*),
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

    if (dev->state != DEV_STATE_CREATED) {
        error_set(errp, QERR_PERMISSION_DENIED);
        return;
    }

    visit_type_str(v, &str, name, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    for (i = 0, pos = 0; i < 6; i++, pos += 3) {
        if (!qemu_isxdigit(str[pos]))
            goto inval;
        if (!qemu_isxdigit(str[pos+1]))
            goto inval;
        if (i == 5) {
            if (str[pos+2] != '\0')
                goto inval;
        } else {
            if (str[pos+2] != ':' && str[pos+2] != '-')
                goto inval;
        }
        mac->a[i] = strtol(str+pos, &p, 16);
    }
    return;

inval:
    error_set_from_qdev_prop_error(errp, EINVAL, dev, prop, str);
}

PropertyInfo qdev_prop_macaddr = {
    .name  = "macaddr",
    .type  = PROP_TYPE_MACADDR,
    .size  = sizeof(MACAddr),
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

    if (dev->state != DEV_STATE_CREATED) {
        error_set(errp, QERR_PERMISSION_DENIED);
        return;
    }

    visit_type_enum(v, ptr, prop->info->enum_table,
                    prop->info->name, prop->name, errp);
}

PropertyInfo qdev_prop_losttickpolicy = {
    .name  = "LostTickPolicy",
    .type  = PROP_TYPE_LOSTTICKPOLICY,
    .size  = sizeof(LostTickPolicy),
    .enum_table  = lost_tick_policy_table,
    .get   = get_enum,
    .set   = set_enum,
};

/* --- pci address --- */

/*
 * bus-local address, i.e. "$slot" or "$slot.$fn"
 */
static int parse_pci_devfn(DeviceState *dev, Property *prop, const char *str)
{
    uint32_t *ptr = qdev_get_prop_ptr(dev, prop);
    unsigned int slot, fn, n;

    if (sscanf(str, "%x.%x%n", &slot, &fn, &n) != 2) {
        fn = 0;
        if (sscanf(str, "%x%n", &slot, &n) != 1) {
            return -EINVAL;
        }
    }
    if (str[n] != '\0')
        return -EINVAL;
    if (fn > 7)
        return -EINVAL;
    if (slot > 31)
        return -EINVAL;
    *ptr = slot << 3 | fn;
    return 0;
}

static int print_pci_devfn(DeviceState *dev, Property *prop, char *dest, size_t len)
{
    uint32_t *ptr = qdev_get_prop_ptr(dev, prop);

    if (*ptr == -1) {
        return snprintf(dest, len, "<unset>");
    } else {
        return snprintf(dest, len, "%02x.%x", *ptr >> 3, *ptr & 7);
    }
}

static void get_pci_devfn(Object *obj, Visitor *v, void *opaque,
                          const char *name, Error **errp)
{
    DeviceState *dev = DEVICE(obj);
    Property *prop = opaque;
    uint32_t *ptr = qdev_get_prop_ptr(dev, prop);
    char buffer[32];
    char *p = buffer;

    buffer[0] = 0;
    if (*ptr != -1) {
        snprintf(buffer, sizeof(buffer), "%02x.%x", *ptr >> 3, *ptr & 7);
    }
    visit_type_str(v, &p, name, errp);
}

PropertyInfo qdev_prop_pci_devfn = {
    .name  = "pci-devfn",
    .type  = PROP_TYPE_UINT32,
    .size  = sizeof(uint32_t),
    .parse = parse_pci_devfn,
    .print = print_pci_devfn,
    .get   = get_pci_devfn,
    .set   = set_generic,
};

/* --- public helpers --- */

static Property *qdev_prop_walk(Property *props, const char *name)
{
    if (!props)
        return NULL;
    while (props->name) {
        if (strcmp(props->name, name) == 0)
            return props;
        props++;
    }
    return NULL;
}

static Property *qdev_prop_find(DeviceState *dev, const char *name)
{
    Property *prop;

    /* device properties */
    prop = qdev_prop_walk(qdev_get_props(dev), name);
    if (prop)
        return prop;

    /* bus properties */
    prop = qdev_prop_walk(dev->parent_bus->info->props, name);
    if (prop)
        return prop;

    return NULL;
}

int qdev_prop_exists(DeviceState *dev, const char *name)
{
    return qdev_prop_find(dev, name) ? true : false;
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

int qdev_prop_parse(DeviceState *dev, const char *name, const char *value)
{
    char *legacy_name;
    Error *err = NULL;

    legacy_name = g_strdup_printf("legacy-%s", name);
    if (object_property_get_type(OBJECT(dev), legacy_name, NULL)) {
        object_property_set_str(OBJECT(dev), value, legacy_name, &err);
    } else {
        object_property_set_str(OBJECT(dev), value, name, &err);
    }
    g_free(legacy_name);

    if (err) {
        qerror_report_err(err);
        error_free(err);
        return -1;
    }
    return 0;
}

void qdev_prop_set(DeviceState *dev, const char *name, void *src, enum PropertyType type)
{
    Property *prop;

    prop = qdev_prop_find(dev, name);
    if (!prop) {
        fprintf(stderr, "%s: property \"%s.%s\" not found\n",
                __FUNCTION__, object_get_typename(OBJECT(dev)), name);
        abort();
    }
    if (prop->info->type != type) {
        fprintf(stderr, "%s: property \"%s.%s\" type mismatch\n",
                __FUNCTION__, object_get_typename(OBJECT(dev)), name);
        abort();
    }
    qdev_prop_cpy(dev, prop, src);
}

void qdev_prop_set_bit(DeviceState *dev, const char *name, bool value)
{
    qdev_prop_set(dev, name, &value, PROP_TYPE_BIT);
}

void qdev_prop_set_uint8(DeviceState *dev, const char *name, uint8_t value)
{
    qdev_prop_set(dev, name, &value, PROP_TYPE_UINT8);
}

void qdev_prop_set_uint16(DeviceState *dev, const char *name, uint16_t value)
{
    qdev_prop_set(dev, name, &value, PROP_TYPE_UINT16);
}

void qdev_prop_set_uint32(DeviceState *dev, const char *name, uint32_t value)
{
    qdev_prop_set(dev, name, &value, PROP_TYPE_UINT32);
}

void qdev_prop_set_int32(DeviceState *dev, const char *name, int32_t value)
{
    qdev_prop_set(dev, name, &value, PROP_TYPE_INT32);
}

void qdev_prop_set_uint64(DeviceState *dev, const char *name, uint64_t value)
{
    qdev_prop_set(dev, name, &value, PROP_TYPE_UINT64);
}

void qdev_prop_set_string(DeviceState *dev, const char *name, char *value)
{
    qdev_prop_set(dev, name, &value, PROP_TYPE_STRING);
}

int qdev_prop_set_drive(DeviceState *dev, const char *name, BlockDriverState *value)
{
    int res;

    res = bdrv_attach_dev(value, dev);
    if (res < 0) {
        error_report("Can't attach drive %s to %s.%s: %s",
                     bdrv_get_device_name(value),
                     dev->id ? dev->id : object_get_typename(OBJECT(dev)),
                     name, strerror(-res));
        return -1;
    }
    qdev_prop_set(dev, name, &value, PROP_TYPE_DRIVE);
    return 0;
}

void qdev_prop_set_drive_nofail(DeviceState *dev, const char *name, BlockDriverState *value)
{
    if (qdev_prop_set_drive(dev, name, value) < 0) {
        exit(1);
    }
}
void qdev_prop_set_chr(DeviceState *dev, const char *name, CharDriverState *value)
{
    qdev_prop_set(dev, name, &value, PROP_TYPE_CHR);
}

void qdev_prop_set_netdev(DeviceState *dev, const char *name, VLANClientState *value)
{
    qdev_prop_set(dev, name, &value, PROP_TYPE_NETDEV);
}

void qdev_prop_set_vlan(DeviceState *dev, const char *name, VLANState *value)
{
    qdev_prop_set(dev, name, &value, PROP_TYPE_VLAN);
}

void qdev_prop_set_macaddr(DeviceState *dev, const char *name, uint8_t *value)
{
    qdev_prop_set(dev, name, value, PROP_TYPE_MACADDR);
}

void qdev_prop_set_losttickpolicy(DeviceState *dev, const char *name,
                                  LostTickPolicy *value)
{
    qdev_prop_set(dev, name, value, PROP_TYPE_LOSTTICKPOLICY);
}

void qdev_prop_set_ptr(DeviceState *dev, const char *name, void *value)
{
    qdev_prop_set(dev, name, &value, PROP_TYPE_PTR);
}

void qdev_prop_set_defaults(DeviceState *dev, Property *props)
{
    if (!props)
        return;
    while (props->name) {
        if (props->defval) {
            qdev_prop_cpy(dev, props, props->defval);
        }
        props++;
    }
}

static QTAILQ_HEAD(, GlobalProperty) global_props = QTAILQ_HEAD_INITIALIZER(global_props);

static void qdev_prop_register_global(GlobalProperty *prop)
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

void qdev_prop_set_globals(DeviceState *dev)
{
    GlobalProperty *prop;

    QTAILQ_FOREACH(prop, &global_props, next) {
        if (strcmp(object_get_typename(OBJECT(dev)), prop->driver) != 0 &&
            strcmp(qdev_get_bus_info(dev)->name, prop->driver) != 0) {
            continue;
        }
        if (qdev_prop_parse(dev, prop->property, prop->value) != 0) {
            exit(1);
        }
    }
}

static int qdev_add_one_global(QemuOpts *opts, void *opaque)
{
    GlobalProperty *g;

    g = g_malloc0(sizeof(*g));
    g->driver   = qemu_opt_get(opts, "driver");
    g->property = qemu_opt_get(opts, "property");
    g->value    = qemu_opt_get(opts, "value");
    qdev_prop_register_global(g);
    return 0;
}

void qemu_add_globals(void)
{
    qemu_opts_foreach(qemu_find_opts("global"), qdev_add_one_global, NULL, 0);
}
