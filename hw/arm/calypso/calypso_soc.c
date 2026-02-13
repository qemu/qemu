#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "qemu/module.h"
#include "exec/memory.h"
#include "qapi/error.h"
#include "hw/qdev-properties.h"

#define TYPE_CALYPSO_SOC "calypso-soc"

OBJECT_DECLARE_SIMPLE_TYPE(CalypsoSoCState, CALYPSO_SOC)

typedef struct CalypsoSoCState {
    SysBusDevice parent_obj;
    MemoryRegion mmio;
    char *socket_path;
} CalypsoSoCState;

/* MMIO dummy */

static uint64_t calypso_soc_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void calypso_soc_write(void *opaque, hwaddr addr,
                              uint64_t value, unsigned size)
{
    /* stub */
}

static const MemoryRegionOps calypso_soc_ops = {
    .read = calypso_soc_read,
    .write = calypso_soc_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void calypso_soc_realize(DeviceState *dev, Error **errp)
{
    CalypsoSoCState *s = CALYPSO_SOC(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    memory_region_init_io(&s->mmio, OBJECT(dev),
                          &calypso_soc_ops,
                          s,
                          "calypso-soc-mmio",
                          0x100000);

    sysbus_init_mmio(sbd, &s->mmio);
}

static Property calypso_soc_props[] = {
    DEFINE_PROP_STRING("socket-path", CalypsoSoCState, socket_path),
    DEFINE_PROP_END_OF_LIST(),
};

static void calypso_soc_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = calypso_soc_realize;
    device_class_set_props(dc, calypso_soc_props);
}

static const TypeInfo calypso_soc_info = {
    .name          = TYPE_CALYPSO_SOC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(CalypsoSoCState),
    .class_init    = calypso_soc_class_init,
};

static void calypso_soc_register_types(void)
{
    type_register_static(&calypso_soc_info);
}

type_init(calypso_soc_register_types);
