/*
 * TI OMAP processors GPIO emulation.
 *
 * Copyright (C) 2006-2008 Andrzej Zaborowski  <balrog@zabor.org>
 * Copyright (C) 2007-2009 Nokia Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/arm/omap.h"
#include "hw/sysbus.h"
#include "qemu/error-report.h"
#include "qemu/module.h"
#include "qapi/error.h"

struct omap_gpio_s {
    qemu_irq irq;
    qemu_irq handler[16];

    uint16_t inputs;
    uint16_t outputs;
    uint16_t dir;
    uint16_t edge;
    uint16_t mask;
    uint16_t ints;
    uint16_t pins;
};

struct Omap1GpioState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    int mpu_model;
    void *clk;
    struct omap_gpio_s omap1;
};

/* General-Purpose I/O of OMAP1 */
static void omap_gpio_set(void *opaque, int line, int level)
{
    Omap1GpioState *p = opaque;
    struct omap_gpio_s *s = &p->omap1;
    uint16_t prev = s->inputs;

    if (level)
        s->inputs |= 1 << line;
    else
        s->inputs &= ~(1 << line);

    if (((s->edge & s->inputs & ~prev) | (~s->edge & ~s->inputs & prev)) &
                    (1 << line) & s->dir & ~s->mask) {
        s->ints |= 1 << line;
        qemu_irq_raise(s->irq);
    }
}

static uint64_t omap_gpio_read(void *opaque, hwaddr addr,
                               unsigned size)
{
    struct omap_gpio_s *s = opaque;
    int offset = addr & OMAP_MPUI_REG_MASK;

    if (size != 2) {
        return omap_badwidth_read16(opaque, addr);
    }

    switch (offset) {
    case 0x00:  /* DATA_INPUT */
        return s->inputs & s->pins;

    case 0x04:  /* DATA_OUTPUT */
        return s->outputs;

    case 0x08:  /* DIRECTION_CONTROL */
        return s->dir;

    case 0x0c:  /* INTERRUPT_CONTROL */
        return s->edge;

    case 0x10:  /* INTERRUPT_MASK */
        return s->mask;

    case 0x14:  /* INTERRUPT_STATUS */
        return s->ints;

    case 0x18:  /* PIN_CONTROL (not in OMAP310) */
        OMAP_BAD_REG(addr);
        return s->pins;
    }

    OMAP_BAD_REG(addr);
    return 0;
}

static void omap_gpio_write(void *opaque, hwaddr addr,
                            uint64_t value, unsigned size)
{
    struct omap_gpio_s *s = opaque;
    int offset = addr & OMAP_MPUI_REG_MASK;
    uint16_t diff;
    int ln;

    if (size != 2) {
        omap_badwidth_write16(opaque, addr, value);
        return;
    }

    switch (offset) {
    case 0x00:  /* DATA_INPUT */
        OMAP_RO_REG(addr);
        return;

    case 0x04:  /* DATA_OUTPUT */
        diff = (s->outputs ^ value) & ~s->dir;
        s->outputs = value;
        while ((ln = ctz32(diff)) != 32) {
            if (s->handler[ln])
                qemu_set_irq(s->handler[ln], (value >> ln) & 1);
            diff &= ~(1 << ln);
        }
        break;

    case 0x08:  /* DIRECTION_CONTROL */
        diff = s->outputs & (s->dir ^ value);
        s->dir = value;

        value = s->outputs & ~s->dir;
        while ((ln = ctz32(diff)) != 32) {
            if (s->handler[ln])
                qemu_set_irq(s->handler[ln], (value >> ln) & 1);
            diff &= ~(1 << ln);
        }
        break;

    case 0x0c:  /* INTERRUPT_CONTROL */
        s->edge = value;
        break;

    case 0x10:  /* INTERRUPT_MASK */
        s->mask = value;
        break;

    case 0x14:  /* INTERRUPT_STATUS */
        s->ints &= ~value;
        if (!s->ints)
            qemu_irq_lower(s->irq);
        break;

    case 0x18:  /* PIN_CONTROL (not in OMAP310 TRM) */
        OMAP_BAD_REG(addr);
        s->pins = value;
        break;

    default:
        OMAP_BAD_REG(addr);
        return;
    }
}

/* *Some* sources say the memory region is 32-bit.  */
static const MemoryRegionOps omap_gpio_ops = {
    .read = omap_gpio_read,
    .write = omap_gpio_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void omap_gpio_reset(struct omap_gpio_s *s)
{
    s->inputs = 0;
    s->outputs = ~0;
    s->dir = ~0;
    s->edge = ~0;
    s->mask = ~0;
    s->ints = 0;
    s->pins = ~0;
}

static void omap_gpif_reset(DeviceState *dev)
{
    Omap1GpioState *s = OMAP1_GPIO(dev);

    omap_gpio_reset(&s->omap1);
}

static void omap_gpio_init(Object *obj)
{
    DeviceState *dev = DEVICE(obj);
    Omap1GpioState *s = OMAP1_GPIO(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    qdev_init_gpio_in(dev, omap_gpio_set, 16);
    qdev_init_gpio_out(dev, s->omap1.handler, 16);
    sysbus_init_irq(sbd, &s->omap1.irq);
    memory_region_init_io(&s->iomem, obj, &omap_gpio_ops, &s->omap1,
                          "omap.gpio", 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
}

static void omap_gpio_realize(DeviceState *dev, Error **errp)
{
    Omap1GpioState *s = OMAP1_GPIO(dev);

    if (!s->clk) {
        error_setg(errp, "omap-gpio: clk not connected");
    }
}

void omap_gpio_set_clk(Omap1GpioState *gpio, omap_clk clk)
{
    gpio->clk = clk;
}

static const Property omap_gpio_properties[] = {
    DEFINE_PROP_INT32("mpu_model", Omap1GpioState, mpu_model, 0),
};

static void omap_gpio_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = omap_gpio_realize;
    device_class_set_legacy_reset(dc, omap_gpif_reset);
    device_class_set_props(dc, omap_gpio_properties);
    /* Reason: pointer property "clk" */
    dc->user_creatable = false;
}

static const TypeInfo omap_gpio_info = {
    .name          = TYPE_OMAP1_GPIO,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Omap1GpioState),
    .instance_init = omap_gpio_init,
    .class_init    = omap_gpio_class_init,
};

static void omap_gpio_register_types(void)
{
    type_register_static(&omap_gpio_info);
}

type_init(omap_gpio_register_types)
