/*
 * QEMU GRLIB Components
 *
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2010-2024 AdaCore
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

#ifndef GRLIB_IRQMP_H
#define GRLIB_IRQMP_H

#include "hw/sysbus.h"

/* Emulation of GrLib device is base on the GRLIB IP Core User's Manual:
 * http://www.gaisler.com/products/grlib/grip.pdf
 */

/* IRQMP */
#define TYPE_GRLIB_IRQMP "grlib-irqmp"

void grlib_irqmp_ack(DeviceState *dev, unsigned int cpu, int intno);

#endif /* GRLIB_IRQMP_H */
