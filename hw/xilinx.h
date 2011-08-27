#include "qemu-common.h"
#include "net.h"

static inline DeviceState *
xilinx_intc_create(target_phys_addr_t base, qemu_irq irq, int kind_of_intr)
{
    DeviceState *dev;

    dev = qdev_create(NULL, "xilinx,intc");
    qdev_prop_set_uint32(dev, "kind-of-intr", kind_of_intr);
    qdev_init_nofail(dev);
    sysbus_mmio_map(sysbus_from_qdev(dev), 0, base);
    sysbus_connect_irq(sysbus_from_qdev(dev), 0, irq);
    return dev;
}

/* OPB Timer/Counter.  */
static inline DeviceState *
xilinx_timer_create(target_phys_addr_t base, qemu_irq irq, int nr, int freq)
{
    DeviceState *dev;

    dev = qdev_create(NULL, "xilinx,timer");
    qdev_prop_set_uint32(dev, "nr-timers", nr);
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

    qemu_check_nic_model(nd, "xilinx-ethlite");

    dev = qdev_create(NULL, "xilinx,ethlite");
    qdev_set_nic_properties(dev, nd);
    qdev_prop_set_uint32(dev, "txpingpong", txpingpong);
    qdev_prop_set_uint32(dev, "rxpingpong", rxpingpong);
    qdev_init_nofail(dev);
    sysbus_mmio_map(sysbus_from_qdev(dev), 0, base);
    sysbus_connect_irq(sysbus_from_qdev(dev), 0, irq);
    return dev;
}

static inline DeviceState *
xilinx_axiethernet_create(void *dmach,
                          NICInfo *nd, target_phys_addr_t base, qemu_irq irq,
                          int txmem, int rxmem)
{
    DeviceState *dev;
    qemu_check_nic_model(nd, "xilinx-axienet");

    dev = qdev_create(NULL, "xilinx,axienet");
    qdev_set_nic_properties(dev, nd);
    qdev_prop_set_uint32(dev, "c_rxmem", rxmem);
    qdev_prop_set_uint32(dev, "c_txmem", txmem);
    qdev_prop_set_ptr(dev, "dmach", dmach);
    qdev_init_nofail(dev);
    sysbus_mmio_map(sysbus_from_qdev(dev), 0, base);
    sysbus_connect_irq(sysbus_from_qdev(dev), 0, irq);

    return dev;
}

static inline DeviceState *
xilinx_axiethernetdma_create(void *dmach,
                             target_phys_addr_t base, qemu_irq irq,
                             qemu_irq irq2, int freqhz)
{
    DeviceState *dev = NULL;

    dev = qdev_create(NULL, "xilinx,axidma");
    qdev_prop_set_uint32(dev, "freqhz", freqhz);
    qdev_prop_set_ptr(dev, "dmach", dmach);
    qdev_init_nofail(dev);

    sysbus_mmio_map(sysbus_from_qdev(dev), 0, base);
    sysbus_connect_irq(sysbus_from_qdev(dev), 0, irq2);
    sysbus_connect_irq(sysbus_from_qdev(dev), 1, irq);

    return dev;
}
