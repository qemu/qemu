/*
 * QEMU GRLIB Components
 *
 * Copyright (c) 2010-2019 AdaCore
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

#ifndef GRLIB_H
#define GRLIB_H

#include "hw/sysbus.h"

/* Emulation of GrLib device is base on the GRLIB IP Core User's Manual:
 * http://www.gaisler.com/products/grlib/grip.pdf
 */

/* IRQMP */
#define TYPE_GRLIB_IRQMP "grlib,irqmp"

typedef void (*set_pil_in_fn) (void *opaque, uint32_t pil_in);

void grlib_irqmp_set_irq(void *opaque, int irq, int level);

void grlib_irqmp_ack(DeviceState *dev, int intno);

/* GPTimer */
#define TYPE_GRLIB_GPTIMER "grlib,gptimer"

/* APB UART */
#define TYPE_GRLIB_APB_UART "grlib,apbuart"

#endif /* GRLIB_H */
