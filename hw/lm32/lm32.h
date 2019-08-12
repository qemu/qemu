#ifndef HW_LM32_H
#define HW_LM32_H

#include "hw/char/lm32_juart.h"
#include "hw/qdev-properties.h"

static inline DeviceState *lm32_pic_init(qemu_irq cpu_irq)
{
    DeviceState *dev;
    SysBusDevice *d;

    dev = qdev_create(NULL, "lm32-pic");
    qdev_init_nofail(dev);
    d = SYS_BUS_DEVICE(dev);
    sysbus_connect_irq(d, 0, cpu_irq);

    return dev;
}

static inline DeviceState *lm32_juart_init(Chardev *chr)
{
    DeviceState *dev;

    dev = qdev_create(NULL, TYPE_LM32_JUART);
    qdev_prop_set_chr(dev, "chardev", chr);
    qdev_init_nofail(dev);

    return dev;
}

static inline DeviceState *lm32_uart_create(hwaddr addr,
                                            qemu_irq irq,
                                            Chardev *chr)
{
    DeviceState *dev;
    SysBusDevice *s;

    dev = qdev_create(NULL, "lm32-uart");
    s = SYS_BUS_DEVICE(dev);
    qdev_prop_set_chr(dev, "chardev", chr);
    qdev_init_nofail(dev);
    sysbus_mmio_map(s, 0, addr);
    sysbus_connect_irq(s, 0, irq);
    return dev;
}

#endif
