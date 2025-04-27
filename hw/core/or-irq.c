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

#include "qemu/osdep.h"
#include "hw/irq.h"
#include "hw/or-irq.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "qemu/module.h"

static void or_irq_handler(void *opaque, int n, int level)
{
    OrIRQState *s = OR_IRQ(opaque);
    int or_level = 0;
    int i;

    s->levels[n] = level;

    for (i = 0; i < s->num_lines; i++) {
        or_level |= s->levels[i];
    }

    qemu_set_irq(s->out_irq, or_level);
}

static void or_irq_reset(DeviceState *dev)
{
    OrIRQState *s = OR_IRQ(dev);
    int i;

    for (i = 0; i < MAX_OR_LINES; i++) {
        s->levels[i] = false;
    }
}

static void or_irq_realize(DeviceState *dev, Error **errp)
{
    OrIRQState *s = OR_IRQ(dev);

    assert(s->num_lines <= MAX_OR_LINES);

    qdev_init_gpio_in(dev, or_irq_handler, s->num_lines);
}

static void or_irq_init(Object *obj)
{
    OrIRQState *s = OR_IRQ(obj);

    qdev_init_gpio_out(DEVICE(obj), &s->out_irq, 1);
}

/* The original version of this device had a fixed 16 entries in its
 * VMState array; devices with more inputs than this need to
 * migrate the extra lines via a subsection.
 * The subsection migrates as much of the levels[] array as is needed
 * (including repeating the first 16 elements), to avoid the awkwardness
 * of splitting it in two to meet the requirements of VMSTATE_VARRAY_UINT16.
 */
#define OLD_MAX_OR_LINES 16
#if MAX_OR_LINES < OLD_MAX_OR_LINES
#error MAX_OR_LINES must be at least 16 for migration compatibility
#endif

static bool vmstate_extras_needed(void *opaque)
{
    OrIRQState *s = OR_IRQ(opaque);

    return s->num_lines >= OLD_MAX_OR_LINES;
}

static const VMStateDescription vmstate_or_irq_extras = {
    .name = "or-irq-extras",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = vmstate_extras_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_VARRAY_UINT16_UNSAFE(levels, OrIRQState, num_lines, 0,
                                     vmstate_info_bool, bool),
        VMSTATE_END_OF_LIST(),
    },
};

static const VMStateDescription vmstate_or_irq = {
    .name = TYPE_OR_IRQ,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_BOOL_SUB_ARRAY(levels, OrIRQState, 0, OLD_MAX_OR_LINES),
        VMSTATE_END_OF_LIST(),
    },
    .subsections = (const VMStateDescription * const []) {
        &vmstate_or_irq_extras,
        NULL
    },
};

static const Property or_irq_properties[] = {
    DEFINE_PROP_UINT16("num-lines", OrIRQState, num_lines, 1),
};

static void or_irq_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, or_irq_reset);
    device_class_set_props(dc, or_irq_properties);
    dc->realize = or_irq_realize;
    dc->vmsd = &vmstate_or_irq;

    /* Reason: Needs to be wired up to work, e.g. see stm32f205_soc.c */
    dc->user_creatable = false;
}

static const TypeInfo or_irq_type_info = {
   .name = TYPE_OR_IRQ,
   .parent = TYPE_DEVICE,
   .instance_size = sizeof(OrIRQState),
   .instance_init = or_irq_init,
   .class_init = or_irq_class_init,
};

static void or_irq_register_types(void)
{
    type_register_static(&or_irq_type_info);
}

type_init(or_irq_register_types)
