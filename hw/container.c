#include "sysbus.h"

static int container_initfn(SysBusDevice *dev)
{
    return 0;
}

static SysBusDeviceInfo container_info = {
    .init = container_initfn,
    .qdev.name = "container",
    .qdev.size = sizeof(SysBusDevice),
    .qdev.no_user = 1,
};

static void container_init(void)
{
    sysbus_register_withprop(&container_info);
}

device_init(container_init);
