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

#include "qemu/osdep.h"
#include "hw/core/split-irq.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "qapi/error.h"
#include "qemu/module.h"

static void split_irq_handler(void *opaque, int n, int level)
{
    SplitIRQ *s = SPLIT_IRQ(opaque);
    int i;

    for (i = 0; i < s->num_lines; i++) {
        qemu_set_irq(s->out_irq[i], level);
    }
}

static void split_irq_init(Object *obj)
{
    qdev_init_gpio_in(DEVICE(obj), split_irq_handler, 1);
}

static void split_irq_realize(DeviceState *dev, Error **errp)
{
    SplitIRQ *s = SPLIT_IRQ(dev);

    if (s->num_lines < 1 || s->num_lines >= MAX_SPLIT_LINES) {
        error_setg(errp,
                   "IRQ splitter number of lines %d is not between 1 and %d",
                   s->num_lines, MAX_SPLIT_LINES);
        return;
    }

    qdev_init_gpio_out(dev, s->out_irq, s->num_lines);
}

static Property split_irq_properties[] = {
    DEFINE_PROP_UINT16("num-lines", SplitIRQ, num_lines, 1),
    DEFINE_PROP_END_OF_LIST(),
};

static void split_irq_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    /* No state to reset or migrate */
    dc->props = split_irq_properties;
    dc->realize = split_irq_realize;

    /* Reason: Needs to be wired up to work */
    dc->user_creatable = false;
}

static const TypeInfo split_irq_type_info = {
   .name = TYPE_SPLIT_IRQ,
   .parent = TYPE_DEVICE,
   .instance_size = sizeof(SplitIRQ),
   .instance_init = split_irq_init,
   .class_init = split_irq_class_init,
};

static void split_irq_register_types(void)
{
    type_register_static(&split_irq_type_info);
}

type_init(split_irq_register_types)
