/*
 * Raspberry Pi emulation (c) 2012 Gregory Estrade
 * This code is licensed under the GNU GPLv2 and later.
 */

#include "hw/sysbus.h"

#define TYPE_BCM2835_TODO "bcm2835_todo"
#define BCM2835_TODO(obj) \
        OBJECT_CHECK(bcm2835_todo_state, (obj), TYPE_BCM2835_TODO)

typedef struct {
    SysBusDevice busdev;
    MemoryRegion iomem;
} bcm2835_todo_state;

static uint64_t bcm2835_todo_read(void *opaque, hwaddr offset,
    unsigned size)
{
    /* "Unlocks" RiscOS boot */
    if (offset == 0x980010) {
        return 0xffffffff;
    }

    return 0;
}

static void bcm2835_todo_write(void *opaque, hwaddr offset,
    uint64_t value, unsigned size)
{
}

static const MemoryRegionOps bcm2835_todo_ops = {
    .read = bcm2835_todo_read,
    .write = bcm2835_todo_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const VMStateDescription vmstate_bcm2835_todo = {
    .name = TYPE_BCM2835_TODO,
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields      = (VMStateField[]) {
        VMSTATE_END_OF_LIST()
    }
};

static int bcm2835_todo_init(SysBusDevice *sbd)
{
    DeviceState *dev = DEVICE(sbd);
    bcm2835_todo_state *s = BCM2835_TODO(dev);

    memory_region_init_io(&s->iomem, OBJECT(s), &bcm2835_todo_ops, s,
        TYPE_BCM2835_TODO, 0x1000000);
    sysbus_init_mmio(sbd, &s->iomem);

    vmstate_register(dev, -1, &vmstate_bcm2835_todo, s);

    return 0;
}

static void bcm2835_todo_class_init(ObjectClass *klass, void *data)
{
    SysBusDeviceClass *sdc = SYS_BUS_DEVICE_CLASS(klass);

    sdc->init = bcm2835_todo_init;
}

static TypeInfo bcm2835_todo_info = {
    .name          = TYPE_BCM2835_TODO,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(bcm2835_todo_state),
    .class_init    = bcm2835_todo_class_init,
};

static void bcm2835_todo_register_types(void)
{
    type_register_static(&bcm2835_todo_info);
}

type_init(bcm2835_todo_register_types)
