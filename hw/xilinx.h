
/* OPB Interrupt Controller.  */
qemu_irq *microblaze_pic_init_cpu(CPUState *env);

static inline DeviceState *
xilinx_intc_create(target_phys_addr_t base, qemu_irq irq, int kind_of_intr)
{
    DeviceState *dev;

    dev = qdev_create(NULL, "xilinx,intc");
    qdev_set_prop_int(dev, "kind-of-intr", kind_of_intr);
    qdev_init(dev);
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
    qdev_set_prop_int(dev, "nr-timers", nr);
    qdev_set_prop_int(dev, "frequency", freq);
    qdev_init(dev);
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
    qdev_set_netdev(dev, nd);
    qdev_set_prop_int(dev, "txpingpong", txpingpong);
    qdev_set_prop_int(dev, "rxpingpong", rxpingpong);
    qdev_init(dev);
    sysbus_mmio_map(sysbus_from_qdev(dev), 0, base);
    sysbus_connect_irq(sysbus_from_qdev(dev), 0, irq);
    return dev;
}
