#include "sysemu.h"
#include "qdev.h"

void *qdev_get_prop_ptr(DeviceState *dev, Property *prop)
{
    void *ptr = dev;
    ptr += prop->offset;
    return ptr;
}

/* --- 16bit integer --- */

static int parse_uint16(DeviceState *dev, Property *prop, const char *str)
{
    uint16_t *ptr = qdev_get_prop_ptr(dev, prop);
    const char *fmt;

    /* accept both hex and decimal */
    fmt = strncasecmp(str, "0x",2) == 0 ? "%" PRIx16 : "%" PRIu16;
    if (sscanf(str, fmt, ptr) != 1)
        return -1;
    return 0;
}

static int print_uint16(DeviceState *dev, Property *prop, char *dest, size_t len)
{
    uint16_t *ptr = qdev_get_prop_ptr(dev, prop);
    return snprintf(dest, len, "%" PRIu16, *ptr);
}

PropertyInfo qdev_prop_uint16 = {
    .name  = "uint16",
    .type  = PROP_TYPE_UINT16,
    .size  = sizeof(uint16_t),
    .parse = parse_uint16,
    .print = print_uint16,
};

/* --- 32bit integer --- */

static int parse_uint32(DeviceState *dev, Property *prop, const char *str)
{
    uint32_t *ptr = qdev_get_prop_ptr(dev, prop);
    const char *fmt;

    /* accept both hex and decimal */
    fmt = strncasecmp(str, "0x",2) == 0 ? "%" PRIx32 : "%" PRIu32;
    if (sscanf(str, fmt, ptr) != 1)
        return -1;
    return 0;
}

static int print_uint32(DeviceState *dev, Property *prop, char *dest, size_t len)
{
    uint32_t *ptr = qdev_get_prop_ptr(dev, prop);
    return snprintf(dest, len, "%" PRIu32, *ptr);
}

PropertyInfo qdev_prop_uint32 = {
    .name  = "uint32",
    .type  = PROP_TYPE_UINT32,
    .size  = sizeof(uint32_t),
    .parse = parse_uint32,
    .print = print_uint32,
};

/* --- 32bit hex value --- */

static int parse_hex32(DeviceState *dev, Property *prop, const char *str)
{
    uint32_t *ptr = qdev_get_prop_ptr(dev, prop);

    if (sscanf(str, "%" PRIx32, ptr) != 1)
        return -1;
    return 0;
}

static int print_hex32(DeviceState *dev, Property *prop, char *dest, size_t len)
{
    uint32_t *ptr = qdev_get_prop_ptr(dev, prop);
    return snprintf(dest, len, "0x%" PRIx32, *ptr);
}

PropertyInfo qdev_prop_hex32 = {
    .name  = "hex32",
    .type  = PROP_TYPE_UINT32,
    .size  = sizeof(uint32_t),
    .parse = parse_hex32,
    .print = print_hex32,
};

/* --- 64bit integer --- */

static int parse_uint64(DeviceState *dev, Property *prop, const char *str)
{
    uint64_t *ptr = qdev_get_prop_ptr(dev, prop);
    const char *fmt;

    /* accept both hex and decimal */
    fmt = strncasecmp(str, "0x",2) == 0 ? "%" PRIx64 : "%" PRIu64;
    if (sscanf(str, fmt, ptr) != 1)
        return -1;
    return 0;
}

static int print_uint64(DeviceState *dev, Property *prop, char *dest, size_t len)
{
    uint64_t *ptr = qdev_get_prop_ptr(dev, prop);
    return snprintf(dest, len, "%" PRIu64, *ptr);
}

PropertyInfo qdev_prop_uint64 = {
    .name  = "uint64",
    .type  = PROP_TYPE_UINT64,
    .size  = sizeof(uint64_t),
    .parse = parse_uint64,
    .print = print_uint64,
};

/* --- 64bit hex value --- */

static int parse_hex64(DeviceState *dev, Property *prop, const char *str)
{
    uint64_t *ptr = qdev_get_prop_ptr(dev, prop);

    if (sscanf(str, "%" PRIx64, ptr) != 1)
        return -1;
    return 0;
}

static int print_hex64(DeviceState *dev, Property *prop, char *dest, size_t len)
{
    uint64_t *ptr = qdev_get_prop_ptr(dev, prop);
    return snprintf(dest, len, "0x%" PRIx64, *ptr);
}

PropertyInfo qdev_prop_hex64 = {
    .name  = "hex64",
    .type  = PROP_TYPE_UINT64,
    .size  = sizeof(uint64_t),
    .parse = parse_hex64,
    .print = print_hex64,
};

/* --- drive --- */

static int parse_drive(DeviceState *dev, Property *prop, const char *str)
{
    DriveInfo **ptr = qdev_get_prop_ptr(dev, prop);

    *ptr = drive_get_by_id(str);
    if (*ptr == NULL)
        return -1;
    return 0;
}

static int print_drive(DeviceState *dev, Property *prop, char *dest, size_t len)
{
    DriveInfo **ptr = qdev_get_prop_ptr(dev, prop);
    return snprintf(dest, len, "%s", (*ptr)->id);
}

PropertyInfo qdev_prop_drive = {
    .name  = "drive",
    .type  = PROP_TYPE_DRIVE,
    .size  = sizeof(DriveInfo*),
    .parse = parse_drive,
    .print = print_drive,
};

/* --- character device --- */

static int print_chr(DeviceState *dev, Property *prop, char *dest, size_t len)
{
    CharDriverState **ptr = qdev_get_prop_ptr(dev, prop);
    return snprintf(dest, len, "%s", (*ptr)->label);
}

PropertyInfo qdev_prop_chr = {
    .name  = "chr",
    .type  = PROP_TYPE_CHR,
    .size  = sizeof(CharDriverState*),
    .print = print_chr,
};

/* --- pointer --- */

static int print_ptr(DeviceState *dev, Property *prop, char *dest, size_t len)
{
    void **ptr = qdev_get_prop_ptr(dev, prop);
    return snprintf(dest, len, "<%p>", *ptr);
}

PropertyInfo qdev_prop_ptr = {
    .name  = "ptr",
    .type  = PROP_TYPE_PTR,
    .size  = sizeof(void*),
    .print = print_ptr,
};

/* --- mac address --- */

/*
 * accepted syntax versions:
 *   01:02:03:04:05:06
 *   01-02-03-04-05-06
 */
static int parse_mac(DeviceState *dev, Property *prop, const char *str)
{
    uint8_t *mac = qdev_get_prop_ptr(dev, prop);
    int i, pos;
    char *p;

    for (i = 0, pos = 0; i < 6; i++, pos += 3) {
        if (!qemu_isxdigit(str[pos]))
            return -1;
        if (!qemu_isxdigit(str[pos+1]))
            return -1;
        if (i == 5 && str[pos+2] != '\0')
            return -1;
        if (str[pos+2] != ':' && str[pos+2] != '-')
            return -1;
        mac[i] = strtol(str+pos, &p, 16);
    }
    return 0;
}

static int print_mac(DeviceState *dev, Property *prop, char *dest, size_t len)
{
    uint8_t *mac = qdev_get_prop_ptr(dev, prop);
    return snprintf(dest, len, "%02x:%02x:%02x:%02x:%02x:%02x",
                    mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

PropertyInfo qdev_prop_macaddr = {
    .name  = "mac-addr",
    .type  = PROP_TYPE_MACADDR,
    .size  = 6,
    .parse = parse_mac,
    .print = print_mac,
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
            return -1;
        }
    }
    if (str[n] != '\0')
        return -1;
    if (fn > 7)
        return -1;
    *ptr = slot << 3 | fn;
    return 0;
}

static int print_pci_devfn(DeviceState *dev, Property *prop, char *dest, size_t len)
{
    uint32_t *ptr = qdev_get_prop_ptr(dev, prop);

    if (-1 == *ptr) {
        return snprintf(dest, len, "<unset>");
    } else {
        return snprintf(dest, len, "%02x.%x", *ptr >> 3, *ptr & 7);
    }
}

PropertyInfo qdev_prop_pci_devfn = {
    .name  = "pci-devfn",
    .type  = PROP_TYPE_UINT32,
    .size  = sizeof(uint32_t),
    .parse = parse_pci_devfn,
    .print = print_pci_devfn,
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
    prop = qdev_prop_walk(dev->info->props, name);
    if (prop)
        return prop;

    /* bus properties */
    prop = qdev_prop_walk(dev->parent_bus->info->props, name);
    if (prop)
        return prop;

    return NULL;
}

int qdev_prop_parse(DeviceState *dev, const char *name, const char *value)
{
    Property *prop;

    prop = qdev_prop_find(dev, name);
    if (!prop) {
        fprintf(stderr, "property \"%s.%s\" not found\n",
                dev->info->name, name);
        return -1;
    }
    if (!prop->info->parse) {
        fprintf(stderr, "property \"%s.%s\" has no parser\n",
                dev->info->name, name);
        return -1;
    }
    return prop->info->parse(dev, prop, value);
}

void qdev_prop_set(DeviceState *dev, const char *name, void *src, enum PropertyType type)
{
    Property *prop;
    void *dst;

    prop = qdev_prop_find(dev, name);
    if (!prop) {
        fprintf(stderr, "%s: property \"%s.%s\" not found\n",
                __FUNCTION__, dev->info->name, name);
        abort();
    }
    if (prop->info->type != type) {
        fprintf(stderr, "%s: property \"%s.%s\" type mismatch\n",
                __FUNCTION__, dev->info->name, name);
        abort();
    }
    dst = qdev_get_prop_ptr(dev, prop);
    memcpy(dst, src, prop->info->size);
}

void qdev_prop_set_uint16(DeviceState *dev, const char *name, uint16_t value)
{
    qdev_prop_set(dev, name, &value, PROP_TYPE_UINT16);
}

void qdev_prop_set_uint32(DeviceState *dev, const char *name, uint32_t value)
{
    qdev_prop_set(dev, name, &value, PROP_TYPE_UINT32);
}

void qdev_prop_set_uint64(DeviceState *dev, const char *name, uint64_t value)
{
    qdev_prop_set(dev, name, &value, PROP_TYPE_UINT64);
}

void qdev_prop_set_drive(DeviceState *dev, const char *name, DriveInfo *value)
{
    qdev_prop_set(dev, name, &value, PROP_TYPE_DRIVE);
}

void qdev_prop_set_chr(DeviceState *dev, const char *name, CharDriverState *value)
{
    qdev_prop_set(dev, name, &value, PROP_TYPE_CHR);
}

void qdev_prop_set_ptr(DeviceState *dev, const char *name, void *value)
{
    qdev_prop_set(dev, name, &value, PROP_TYPE_PTR);
}

void qdev_prop_set_defaults(DeviceState *dev, Property *props)
{
    char *dst;

    if (!props)
        return;
    while (props->name) {
        if (props->defval) {
            dst = qdev_get_prop_ptr(dev, props);
            memcpy(dst, props->defval, props->info->size);
        }
        props++;
    }
}

static CompatProperty *compat_props;

void qdev_prop_register_compat(CompatProperty *props)
{
    compat_props = props;
}

void qdev_prop_set_compat(DeviceState *dev)
{
    CompatProperty *prop;

    if (!compat_props) {
        return;
    }
    for (prop = compat_props; prop->driver != NULL; prop++) {
        if (strcmp(dev->info->name, prop->driver) != 0) {
            continue;
        }
        if (qdev_prop_parse(dev, prop->property, prop->value) != 0) {
            abort();
        }
    }
}
