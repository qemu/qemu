#ifndef HW_LM32_H
#define HW_LM32_H

#include "hw/char/lm32_juart.h"
#include "hw/qdev-properties.h"
#include "qapi/error.h"

static inline DeviceState *lm32_pic_init(qemu_irq cpu_irq)
{
    DeviceState *dev;
    SysBusDevice *d;

    dev = qdev_new("lm32-pic");
    d = SYS_BUS_DEVICE(dev);
    sysbus_realize_and_unref(d, &error_fatal);
    sysbus_connect_irq(d, 0, cpu_irq);

    return dev;
}

static inline DeviceState *lm32_juart_init(Chardev *chr)
{
    DeviceState *dev;

    dev = qdev_new(TYPE_LM32_JUART);
    qdev_prop_set_chr(dev, "chardev", chr);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);

    return dev;
}

static inline DeviceState *lm32_uart_create(hwaddr addr,
                                            qemu_irq irq,
                                            Chardev *chr)
{
    DeviceState *dev;
    SysBusDevice *s;

    dev = qdev_new("lm32-uart");
    s = SYS_BUS_DEVICE(dev);
    qdev_prop_set_chr(dev, "chardev", chr);
    sysbus_realize_and_unref(s, &error_fatal);
    sysbus_mmio_map(s, 0, addr);
    sysbus_connect_irq(s, 0, irq);
    return dev;
}

#endif
