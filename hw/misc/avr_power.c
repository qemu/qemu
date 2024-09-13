/*
 * AVR Power Reduction Management
 *
 * Copyright (c) 2019-2020 Michael Rolnik
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
#include "hw/misc/avr_power.h"
#include "qemu/log.h"
#include "hw/qdev-properties.h"
#include "hw/irq.h"
#include "trace.h"

static void avr_mask_reset(DeviceState *dev)
{
    AVRMaskState *s = AVR_MASK(dev);

    s->val = 0x00;

    for (int i = 0; i < 8; i++) {
        qemu_set_irq(s->irq[i], 0);
    }
}

static uint64_t avr_mask_read(void *opaque, hwaddr offset, unsigned size)
{
    assert(size == 1);
    assert(offset == 0);
    AVRMaskState *s = opaque;

    trace_avr_power_read(s->val);

    return (uint64_t)s->val;
}

static void avr_mask_write(void *opaque, hwaddr offset,
                           uint64_t val64, unsigned size)
{
    assert(size == 1);
    assert(offset == 0);
    AVRMaskState *s = opaque;
    uint8_t val8 = val64;

    trace_avr_power_write(val8);
    s->val = val8;
    for (int i = 0; i < 8; i++) {
        qemu_set_irq(s->irq[i], (val8 & (1 << i)) != 0);
    }
}

static const MemoryRegionOps avr_mask_ops = {
    .read = avr_mask_read,
    .write = avr_mask_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {
        .max_access_size = 1,
    },
};

static void avr_mask_init(Object *dev)
{
    AVRMaskState *s = AVR_MASK(dev);
    SysBusDevice *busdev = SYS_BUS_DEVICE(dev);

    memory_region_init_io(&s->iomem, dev, &avr_mask_ops, s, TYPE_AVR_MASK,
                          0x01);
    sysbus_init_mmio(busdev, &s->iomem);

    for (int i = 0; i < 8; i++) {
        sysbus_init_irq(busdev, &s->irq[i]);
    }
    s->val = 0x00;
}

static void avr_mask_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, avr_mask_reset);
}

static const TypeInfo avr_mask_info = {
    .name          = TYPE_AVR_MASK,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AVRMaskState),
    .class_init    = avr_mask_class_init,
    .instance_init = avr_mask_init,
};

static void avr_mask_register_types(void)
{
    type_register_static(&avr_mask_info);
}

type_init(avr_mask_register_types)
