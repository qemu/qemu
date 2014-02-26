#ifndef HW_XILINX_H
#define HW_XILINX_H 1


#include "qemu-common.h"
#include "qapi/qmp/qerror.h"
#include "hw/stream.h"
#include "net/net.h"

static inline void
xilinx_axiethernet_init(DeviceState *dev, NICInfo *nd, StreamSlave *ds,
                        StreamSlave *cs, hwaddr base, qemu_irq irq, int txmem,
                        int rxmem)
{
    qdev_set_nic_properties(dev, nd);
    qdev_prop_set_uint32(dev, "rxmem", rxmem);
    qdev_prop_set_uint32(dev, "txmem", txmem);
    object_property_set_link(OBJECT(dev), OBJECT(ds),
                             "axistream-connected", &error_abort);
    object_property_set_link(OBJECT(dev), OBJECT(cs),
                             "axistream-control-connected", &error_abort);
    qdev_init_nofail(dev);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, base);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, irq);
}

static inline void
xilinx_axidma_init(DeviceState *dev, StreamSlave *ds, StreamSlave *cs,
                   hwaddr base, qemu_irq irq, qemu_irq irq2, int freqhz)
{
    qdev_prop_set_uint32(dev, "freqhz", freqhz);
    object_property_set_link(OBJECT(dev), OBJECT(ds),
                             "axistream-connected", &error_abort);
    object_property_set_link(OBJECT(dev), OBJECT(cs),
                             "axistream-control-connected", &error_abort);
    qdev_init_nofail(dev);

    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, base);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, irq);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 1, irq2);
}

#endif
