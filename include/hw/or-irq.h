/*
 * QEMU IRQ/GPIO common code.
 *
 * Copyright (c) 2016 Alistair Francis <alistair@alistair23.me>.
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

#ifndef HW_OR_IRQ_H
#define HW_OR_IRQ_H

#include "hw/irq.h"
#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_OR_IRQ "or-irq"

/* This can safely be increased if necessary without breaking
 * migration compatibility (as long as it remains greater than 15).
 */
#define MAX_OR_LINES      32

typedef struct OrIRQState qemu_or_irq;

#define OR_IRQ(obj) OBJECT_CHECK(qemu_or_irq, (obj), TYPE_OR_IRQ)

struct OrIRQState {
    DeviceState parent_obj;

    qemu_irq out_irq;
    bool levels[MAX_OR_LINES];
    uint16_t num_lines;
};

#endif
