#include "stream.h"
#include "qemu-common.h"
#include "net.h"

static inline DeviceState *
xilinx_intc_create(target_phys_addr_t base, qemu_irq irq, int kind_of_intr)
{
    DeviceState *dev;

    dev = qdev_create(NULL, "xlnx.xps-intc");
    qdev_prop_set_uint32(dev, "kind-of-intr", kind_of_intr);
    qdev_init_nofail(dev);
    sysbus_mmio_map(sysbus_from_qdev(dev), 0, base);
    sysbus_connect_irq(sysbus_from_qdev(dev), 0, irq);
    return dev;
}

/* OPB Timer/Counter.  */
static inline DeviceState *
xilinx_timer_create(target_phys_addr_t base, qemu_irq irq, int oto, int freq)
{
    DeviceState *dev;

    dev = qdev_create(NULL, "xlnx,xps-timer");
    qdev_prop_set_uint32(dev, "one-timer-only", oto);
    qdev_prop_set_uint32(dev, "frequency", freq);
    qdev_init_nofail(dev);
    sysbus_mmio_map(sysbus_from_qdev(dev), 0, base);
    sysbus_connect_irq(sysbus_from_qdev(dev), 0, irq);
    return dev;
}

/* XPS Ethernet Lite MAC.  */
static inline DeviceState *
xilinx_ethlite_create(NICInfo *nd, target_phys_addr_t base, qemu_irq irq,
                      int txpingpong, int rxpingpong)
{
    DeviceState *dev;

    qemu_check_nic_model(nd, "xlnx.xps-ethernetlite");

    dev = qdev_create(NULL, "xlnx.xps-ethernetlite");
    qdev_set_nic_properties(dev, nd);
    qdev_prop_set_uint32(dev, "tx-ping-pong", txpingpong);
    qdev_prop_set_uint32(dev, "rx-ping-pong", rxpingpong);
    qdev_init_nofail(dev);
    sysbus_mmio_map(sysbus_from_qdev(dev), 0, base);
    sysbus_connect_irq(sysbus_from_qdev(dev), 0, irq);
    return dev;
}

static inline DeviceState *
xilinx_axiethernet_create(NICInfo *nd, StreamSlave *peer,
                          target_phys_addr_t base, qemu_irq irq,
                          int txmem, int rxmem)
{
    DeviceState *dev;
    qemu_check_nic_model(nd, "xlnx.axi-ethernet");

    dev = qdev_create(NULL, "xlnx.axi-ethernet");
    qdev_set_nic_properties(dev, nd);
    qdev_prop_set_uint32(dev, "rxmem", rxmem);
    qdev_prop_set_uint32(dev, "txmem", txmem);
    object_property_set_link(OBJECT(dev), OBJECT(peer), "tx_dev", NULL);
    qdev_init_nofail(dev);
    sysbus_mmio_map(sysbus_from_qdev(dev), 0, base);
    sysbus_connect_irq(sysbus_from_qdev(dev), 0, irq);

    return dev;
}

static inline void
xilinx_axiethernetdma_init(DeviceState *dev, StreamSlave *peer,
                           target_phys_addr_t base, qemu_irq irq,
                           qemu_irq irq2, int freqhz)
{
    qdev_prop_set_uint32(dev, "freqhz", freqhz);
    object_property_set_link(OBJECT(dev), OBJECT(peer), "tx_dev", NULL);
    qdev_init_nofail(dev);

    sysbus_mmio_map(sysbus_from_qdev(dev), 0, base);
    sysbus_connect_irq(sysbus_from_qdev(dev), 0, irq);
    sysbus_connect_irq(sysbus_from_qdev(dev), 1, irq2);
}
