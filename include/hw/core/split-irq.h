/*
 * IRQ splitter device.
 *
 * Copyright (c) 2018 Linaro Limited.
 * Written by Peter Maydell
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

/* This is a simple device which has one GPIO input line and multiple
 * GPIO output lines. Any change on the input line is forwarded to all
 * of the outputs.
 *
 * QEMU interface:
 *  + one unnamed GPIO input: the input line
 *  + N unnamed GPIO outputs: the output lines
 *  + QOM property "num-lines": sets the number of output lines
 */
#ifndef HW_SPLIT_IRQ_H
#define HW_SPLIT_IRQ_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_SPLIT_IRQ "split-irq"

#define MAX_SPLIT_LINES 16


OBJECT_DECLARE_SIMPLE_TYPE(SplitIRQ, SPLIT_IRQ)

struct SplitIRQ {
    DeviceState parent_obj;

    qemu_irq out_irq[MAX_SPLIT_LINES];
    uint16_t num_lines;
};

#endif
