/*
 * QEMU 8253/8254 interval timer emulation
 *
 * Copyright (c) 2003-2004 Fabrice Bellard
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

#ifndef HW_I8254_H
#define HW_I8254_H

#include "hw/qdev-properties.h"
#include "hw/isa/isa.h"
#include "qapi/error.h"
#include "qom/object.h"

#define PIT_FREQ 1193182

typedef struct PITChannelInfo {
    int gate;
    int mode;
    int initial_count;
    int out;
} PITChannelInfo;

#define TYPE_PIT_COMMON "pit-common"
OBJECT_DECLARE_TYPE(PITCommonState, PITCommonClass, PIT_COMMON)

#define TYPE_I8254 "isa-pit"
#define TYPE_KVM_I8254 "kvm-pit"

static inline ISADevice *i8254_pit_init(ISABus *bus, int base, int isa_irq,
                                        qemu_irq alt_irq)
{
    DeviceState *dev;
    ISADevice *d;

    d = isa_new(TYPE_I8254);
    dev = DEVICE(d);
    qdev_prop_set_uint32(dev, "iobase", base);
    isa_realize_and_unref(d, bus, &error_fatal);
    qdev_connect_gpio_out(dev, 0,
                          isa_irq >= 0 ? isa_bus_get_irq(bus, isa_irq)
                                       : alt_irq);

    return d;
}

static inline ISADevice *kvm_pit_init(ISABus *bus, int base)
{
    DeviceState *dev;
    ISADevice *d;

    d = isa_new(TYPE_KVM_I8254);
    dev = DEVICE(d);
    qdev_prop_set_uint32(dev, "iobase", base);
    isa_realize_and_unref(d, bus, &error_fatal);

    return d;
}

void pit_set_gate(ISADevice *dev, int channel, int val);
void pit_get_channel_info(ISADevice *dev, int channel, PITChannelInfo *info);

#endif /* HW_I8254_H */
