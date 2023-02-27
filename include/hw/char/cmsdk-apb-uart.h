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

#endif
