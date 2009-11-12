/*
 * QEMU VLYNQ Interface support
 *
 * This code is licenced under the GNU GPL v2.
 */

#include "vlynq.h"

struct _VLYNQBus {
    BusState qbus;
};

static struct BusInfo vlynq_bus_info = {
    .name = "VLYNQ",
    .size = sizeof(VLYNQBus),
};

#if 0
static int vlynq_slave_init(DeviceState *dev, DeviceInfo *base_info)
{
    VLYNQSlaveInfo *info = container_of(base_info, VLYNQSlaveInfo, qdev);
    VLYNQSlave *s = VLYNQ_SLAVE_FROM_QDEV(dev);
    VLYNQBus *bus;

    bus = FROM_QBUS(VLYNQBus, qdev_get_parent_bus(dev));
    if (QLIST_FIRST(&bus->qbus.children) != dev
        || QLIST_NEXT(dev, sibling) != NULL) {
        hw_error("Too many devices on VLYNQ bus");
    }

    s->info = info;
    return info->init(s);
}

void vlynq_register_slave(VLYNQSlaveInfo *info)
{
    assert(info->qdev.size >= sizeof(VLYNQSlave));
    info->qdev.init = vlynq_slave_init;
    info->qdev.bus_info = &vlynq_bus_info;
    qdev_register(&info->qdev);
}

DeviceState *vlynq_create_slave(VLYNQBus *bus, const char *name)
{
    DeviceState *dev;
    dev = qdev_create(&bus->qbus, name);
    qdev_init_nofail(dev);
    return dev;
}
#endif

VLYNQBus *vlynq_create_bus(DeviceState *parent, const char *name)
{
    BusState *bus;
    bus = qbus_create(&vlynq_bus_info, parent, name);
    return FROM_QBUS(VLYNQBus, bus);
}

#if 0
uint32_t vlynq_transfer(VLYNQBus *bus, uint32_t val)
{
    DeviceState *dev;
    VLYNQSlave *slave;
    dev = QLIST_FIRST(&bus->qbus.children);
    if (!dev) {
        return 0;
    }
    slave = VLYNQ_SLAVE_FROM_QDEV(dev);
    return slave->info->transfer(slave, val);
}
#endif
