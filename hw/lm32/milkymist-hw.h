#ifndef QEMU_HW_MILKYMIST_HW_H
#define QEMU_HW_MILKYMIST_HW_H

#include "hw/qdev-core.h"
#include "net/net.h"

static inline DeviceState *milkymist_uart_create(hwaddr base,
                                                 qemu_irq irq,
                                                 Chardev *chr)
{
    DeviceState *dev;

    dev = qdev_create(NULL, "milkymist-uart");
    qdev_prop_set_chr(dev, "chardev", chr);
    qdev_init_nofail(dev);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, base);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, irq);

    return dev;
}

static inline DeviceState *milkymist_hpdmc_create(hwaddr base)
{
    DeviceState *dev;

    dev = qdev_create(NULL, "milkymist-hpdmc");
    qdev_init_nofail(dev);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, base);

    return dev;
}

static inline DeviceState *milkymist_memcard_create(hwaddr base)
{
    DeviceState *dev;

    dev = qdev_create(NULL, "milkymist-memcard");
    qdev_init_nofail(dev);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, base);

    return dev;
}

static inline DeviceState *milkymist_vgafb_create(hwaddr base,
        uint32_t fb_offset, uint32_t fb_mask)
{
    DeviceState *dev;

    dev = qdev_create(NULL, "milkymist-vgafb");
    qdev_prop_set_uint32(dev, "fb_offset", fb_offset);
    qdev_prop_set_uint32(dev, "fb_mask", fb_mask);
    qdev_init_nofail(dev);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, base);

    return dev;
}

static inline DeviceState *milkymist_sysctl_create(hwaddr base,
        qemu_irq gpio_irq, qemu_irq timer0_irq, qemu_irq timer1_irq,
        uint32_t freq_hz, uint32_t system_id, uint32_t capabilities,
        uint32_t gpio_strappings)
{
    DeviceState *dev;

    dev = qdev_create(NULL, "milkymist-sysctl");
    qdev_prop_set_uint32(dev, "frequency", freq_hz);
    qdev_prop_set_uint32(dev, "systemid", system_id);
    qdev_prop_set_uint32(dev, "capabilities", capabilities);
    qdev_prop_set_uint32(dev, "gpio_strappings", gpio_strappings);
    qdev_init_nofail(dev);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, base);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, gpio_irq);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 1, timer0_irq);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 2, timer1_irq);

    return dev;
}

static inline DeviceState *milkymist_pfpu_create(hwaddr base,
        qemu_irq irq)
{
    DeviceState *dev;

    dev = qdev_create(NULL, "milkymist-pfpu");
    qdev_init_nofail(dev);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, base);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, irq);
    return dev;
}

static inline DeviceState *milkymist_ac97_create(hwaddr base,
        qemu_irq crrequest_irq, qemu_irq crreply_irq, qemu_irq dmar_irq,
        qemu_irq dmaw_irq)
{
    DeviceState *dev;

    dev = qdev_create(NULL, "milkymist-ac97");
    qdev_init_nofail(dev);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, base);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, crrequest_irq);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 1, crreply_irq);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 2, dmar_irq);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 3, dmaw_irq);

    return dev;
}

static inline DeviceState *milkymist_minimac2_create(hwaddr base,
        hwaddr buffers_base, qemu_irq rx_irq, qemu_irq tx_irq)
{
    DeviceState *dev;

    qemu_check_nic_model(&nd_table[0], "minimac2");
    dev = qdev_create(NULL, "milkymist-minimac2");
    qdev_set_nic_properties(dev, &nd_table[0]);
    qdev_init_nofail(dev);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, base);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 1, buffers_base);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, rx_irq);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 1, tx_irq);

    return dev;
}

static inline DeviceState *milkymist_softusb_create(hwaddr base,
        qemu_irq irq, uint32_t pmem_base, uint32_t pmem_size,
        uint32_t dmem_base, uint32_t dmem_size)
{
    DeviceState *dev;

    dev = qdev_create(NULL, "milkymist-softusb");
    qdev_prop_set_uint32(dev, "pmem_size", pmem_size);
    qdev_prop_set_uint32(dev, "dmem_size", dmem_size);
    qdev_init_nofail(dev);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, base);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 1, pmem_base);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 2, dmem_base);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, irq);

    return dev;
}

#endif /* QEMU_HW_MILKYMIST_HW_H */
