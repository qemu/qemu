/*
 * ARM CMSDK APB UART emulation
 *
 * Copyright (c) 2017 Linaro Limited
 * Written by Peter Maydell
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 or
 *  (at your option) any later version.
 */

#ifndef CMSDK_APB_UART_H
#define CMSDK_APB_UART_H

#include "hw/qdev-properties.h"
#include "hw/sysbus.h"
#include "chardev/char-fe.h"
#include "qom/object.h"

#define TYPE_CMSDK_APB_UART "cmsdk-apb-uart"
OBJECT_DECLARE_SIMPLE_TYPE(CMSDKAPBUART, CMSDK_APB_UART)

struct CMSDKAPBUART {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;
    CharBackend chr;
    qemu_irq txint;
    qemu_irq rxint;
    qemu_irq txovrint;
    qemu_irq rxovrint;
    qemu_irq uartint;
    guint watch_tag;
    uint32_t pclk_frq;

    uint32_t state;
    uint32_t ctrl;
    uint32_t intstatus;
    uint32_t bauddiv;
    /* This UART has no FIFO, only a 1-character buffer for each of Tx and Rx */
    uint8_t txbuf;
    uint8_t rxbuf;
};

/**
 * cmsdk_apb_uart_create - convenience function to create TYPE_CMSDK_APB_UART
 * @addr: location in system memory to map registers
 * @chr: Chardev backend to connect UART to, or NULL if no backend
 * @pclk_frq: frequency in Hz of the PCLK clock (used for calculating baud rate)
 */
static inline DeviceState *cmsdk_apb_uart_create(hwaddr addr,
                                                 qemu_irq txint,
                                                 qemu_irq rxint,
                                                 qemu_irq txovrint,
                                                 qemu_irq rxovrint,
                                                 qemu_irq uartint,
                                                 Chardev *chr,
                                                 uint32_t pclk_frq)
{
    DeviceState *dev;
    SysBusDevice *s;

    dev = qdev_new(TYPE_CMSDK_APB_UART);
    s = SYS_BUS_DEVICE(dev);
    qdev_prop_set_chr(dev, "chardev", chr);
    qdev_prop_set_uint32(dev, "pclk-frq", pclk_frq);
    sysbus_realize_and_unref(s, &error_fatal);
    sysbus_mmio_map(s, 0, addr);
    sysbus_connect_irq(s, 0, txint);
    sysbus_connect_irq(s, 1, rxint);
    sysbus_connect_irq(s, 2, txovrint);
    sysbus_connect_irq(s, 3, rxovrint);
    sysbus_connect_irq(s, 4, uartint);
    return dev;
}

#endif
