/*
 * QEMU GRLIB Components
 *
 * Copyright (c) 2010-2011 AdaCore
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef _GRLIB_H_
#define _GRLIB_H_

#include "qdev.h"
#include "sysbus.h"

/* Emulation of GrLib device is base on the GRLIB IP Core User's Manual:
 * http://www.gaisler.com/products/grlib/grip.pdf
 */

/* GPTimer */

static inline
DeviceState *grlib_gptimer_create(target_phys_addr_t  base,
                                  uint32_t            nr_timers,
                                  uint32_t            freq,
                                  qemu_irq           *cpu_irqs,
                                  int                 base_irq)
{
    DeviceState *dev;
    int i;

    dev = qdev_create(NULL, "grlib,gptimer");
    qdev_prop_set_uint32(dev, "nr-timers", nr_timers);
    qdev_prop_set_uint32(dev, "frequency", freq);
    qdev_prop_set_uint32(dev, "irq-line", base_irq);

    if (qdev_init(dev)) {
        return NULL;
    }

    sysbus_mmio_map(sysbus_from_qdev(dev), 0, base);

    for (i = 0; i < nr_timers; i++) {
        sysbus_connect_irq(sysbus_from_qdev(dev), i, cpu_irqs[base_irq + i]);
    }

    return dev;
}

#endif /* ! _GRLIB_H_ */
