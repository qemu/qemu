#include "qdev.h"
#include "qdev-addr.h"
#include "targphys.h"

/* --- target physical address --- */

static int parse_taddr(DeviceState *dev, Property *prop, const char *str)
{
    a_target_phys_addr *ptr = qdev_get_prop_ptr(dev, prop);

    *ptr = strtoull(str, NULL, 16);
    return 0;
}

static int print_taddr(DeviceState *dev, Property *prop, char *dest, size_t len)
{
    a_target_phys_addr *ptr = qdev_get_prop_ptr(dev, prop);
    return snprintf(dest, len, "0x" TARGET_FMT_plx, *ptr);
}

PropertyInfo qdev_prop_taddr = {
    .name  = "taddr",
    .type  = PROP_TYPE_TADDR,
    .size  = sizeof(a_target_phys_addr),
    .parse = parse_taddr,
    .print = print_taddr,
};

void qdev_prop_set_taddr(DeviceState *dev, const char *name, a_target_phys_addr value)
{
    qdev_prop_set(dev, name, &value, PROP_TYPE_TADDR);
}
