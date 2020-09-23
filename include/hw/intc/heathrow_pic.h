/*
 * Heathrow PIC support (OldWorld PowerMac)
 *
 * Copyright (c) 2005-2007 Fabrice Bellard
 * Copyright (c) 2007 Jocelyn Mayer
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

#ifndef HW_INTC_HEATHROW_PIC_H
#define HW_INTC_HEATHROW_PIC_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_HEATHROW "heathrow"
OBJECT_DECLARE_SIMPLE_TYPE(HeathrowState, HEATHROW)

typedef struct HeathrowPICState {
    uint32_t events;
    uint32_t mask;
    uint32_t levels;
    uint32_t level_triggered;
} HeathrowPICState;

struct HeathrowState {
    SysBusDevice parent_obj;

    MemoryRegion mem;
    HeathrowPICState pics[2];
    qemu_irq irqs[1];
};

#define HEATHROW_NUM_IRQS 64

#endif /* HW_INTC_HEATHROW_PIC_H */
