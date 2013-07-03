/*
 * i.MX31 emulation
 *
 * Copyright (C) 2012 Peter Chubb
 * NICTA
 *
 * This code is released under the GPL, version 2.0 or later
 * See the file `../COPYING' for details.
 */

#ifndef IMX_H
#define IMX_H

void imx_serial_create(int uart, const hwaddr addr, qemu_irq irq);

typedef enum  {
    NOCLK,
    MCU,
    HSP,
    IPG,
    CLK_32k
} IMXClk;

uint32_t imx_clock_frequency(DeviceState *s, IMXClk clock);

void imx_timerp_create(const hwaddr addr,
                      qemu_irq irq,
                      DeviceState *ccm);
void imx_timerg_create(const hwaddr addr,
                      qemu_irq irq,
                      DeviceState *ccm);


#endif /* IMX_H */
