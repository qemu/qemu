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

void imx_serial_create(int uart, const target_phys_addr_t addr, qemu_irq irq);

#endif /* IMX_H */
