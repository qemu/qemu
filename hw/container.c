#include "sysbus.h"

static int container_initfn(SysBusDevice *dev)
{
    return 0;
}

static void container_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

    k->init = container_initfn;
    dc->no_user = 1;
}

static TypeInfo container_info = {
    .name          = "container",
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(SysBusDevice),
    .class_init    = container_class_init,
};

static void container_init(void)
{
    type_register_static(&container_info);
}

device_init(container_init);
