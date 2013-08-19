#ifndef HW_XILINX_H
#define HW_XILINX_H 1


#include "qemu-common.h"
#include "qapi/qmp/qerror.h"
#include "hw/stream.h"
#include "net/net.h"

static inline DeviceState *
xilinx_intc_create(hwaddr base, qemu_irq irq, int kind_of_intr)
{
    DeviceState *dev;

    dev = qdev_create(NULL, "xlnx.xps-intc");
    qdev_prop_set_uint32(dev, "kind-of-intr", kind_of_intr);
    qdev_init_nofail(dev);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, base);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, irq);
    return dev;
}

/* OPB Timer/Counter.  */
static inline DeviceState *
xilinx_timer_create(hwaddr base, qemu_irq irq, int oto, int freq)
{
    DeviceState *dev;

    dev = qdev_create(NULL, "xlnx.xps-timer");
    qdev_prop_set_uint32(dev, "one-timer-only", oto);
    qdev_prop_set_uint32(dev, "clock-frequency", freq);
    qdev_init_nofail(dev);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, base);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, irq);
    return dev;
}

/* XPS Ethernet Lite MAC.  */
static inline DeviceState *
xilinx_ethlite_create(NICInfo *nd, hwaddr base, qemu_irq irq,
                      int txpingpong, int rxpingpong)
{
    DeviceState *dev;

    qemu_check_nic_model(nd, "xlnx.xps-ethernetlite");

    dev = qdev_create(NULL, "xlnx.xps-ethernetlite");
    qdev_set_nic_properties(dev, nd);
    qdev_prop_set_uint32(dev, "tx-ping-pong", txpingpong);
    qdev_prop_set_uint32(dev, "rx-ping-pong", rxpingpong);
    qdev_init_nofail(dev);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, base);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, irq);
    return dev;
}

static inline void
xilinx_axiethernet_init(DeviceState *dev, NICInfo *nd, StreamSlave *ds,
                        StreamSlave *cs, hwaddr base, qemu_irq irq, int txmem,
                        int rxmem)
{
    Error *errp = NULL;

    qdev_set_nic_properties(dev, nd);
    qdev_prop_set_uint32(dev, "rxmem", rxmem);
    qdev_prop_set_uint32(dev, "txmem", txmem);
    object_property_set_link(OBJECT(dev), OBJECT(ds),
                             "axistream-connected", &errp);
    object_property_set_link(OBJECT(dev), OBJECT(cs),
                             "axistream-control-connected", &errp);
    assert_no_error(errp);
    qdev_init_nofail(dev);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, base);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, irq);
}

static inline void
xilinx_axidma_init(DeviceState *dev, StreamSlave *ds, StreamSlave *cs,
                   hwaddr base, qemu_irq irq, qemu_irq irq2, int freqhz)
{
    Error *errp = NULL;

    qdev_prop_set_uint32(dev, "freqhz", freqhz);
    object_property_set_link(OBJECT(dev), OBJECT(ds),
                             "axistream-connected", &errp);
    object_property_set_link(OBJECT(dev), OBJECT(cs),
                             "axistream-control-connected", &errp);
    assert_no_error(errp);
    qdev_init_nofail(dev);

    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, base);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, irq);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 1, irq2);
}

#endif
