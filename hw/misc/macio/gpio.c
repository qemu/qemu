/*
 * PowerMac NewWorld MacIO GPIO emulation
 *
 * Copyright (c) 2016 Benjamin Herrenschmidt
 * Copyright (c) 2018 Mark Cave-Ayland
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
#include "hw/ppc/mac.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "hw/misc/macio/macio.h"
#include "hw/misc/macio/gpio.h"
#include "hw/nmi.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "trace.h"


void macio_set_gpio(MacIOGPIOState *s, uint32_t gpio, bool state)
{
    uint8_t new_reg;

    trace_macio_set_gpio(gpio, state);

    if (s->gpio_regs[gpio] & 4) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "GPIO: Setting GPIO %d while it's an output\n", gpio);
    }

    new_reg = s->gpio_regs[gpio] & ~2;
    if (state) {
        new_reg |= 2;
    }

    if (new_reg == s->gpio_regs[gpio]) {
        return;
    }

    s->gpio_regs[gpio] = new_reg;

    /* This is will work until we fix the binding between MacIO and
     * the MPIC properly so we can route all GPIOs and avoid going
     * via the top level platform code.
     *
     * Note that we probably need to get access to the MPIC config to
     * decode polarity since qemu always use "raise" regardless.
     *
     * For now, we hard wire known GPIOs
     */

    switch (gpio) {
    case 1:
        /* Level low */
        if (!state) {
            trace_macio_gpio_irq_assert(gpio);
            qemu_irq_raise(s->gpio_extirqs[gpio]);
        } else {
            trace_macio_gpio_irq_deassert(gpio);
            qemu_irq_lower(s->gpio_extirqs[gpio]);
        }
        break;

    case 9:
        /* Edge, triggered by NMI below */
        if (state) {
            trace_macio_gpio_irq_assert(gpio);
            qemu_irq_raise(s->gpio_extirqs[gpio]);
        } else {
            trace_macio_gpio_irq_deassert(gpio);
            qemu_irq_lower(s->gpio_extirqs[gpio]);
        }
        break;

    default:
        qemu_log_mask(LOG_UNIMP, "GPIO: setting unimplemented GPIO %d", gpio);
    }
}

static void macio_gpio_write(void *opaque, hwaddr addr, uint64_t value,
                             unsigned size)
{
    MacIOGPIOState *s = opaque;
    uint8_t ibit;

    trace_macio_gpio_write(addr, value);

    /* Levels regs are read-only */
    if (addr < 8) {
        return;
    }

    addr -= 8;
    if (addr < 36) {
        value &= ~2;

        if (value & 4) {
            ibit = (value & 1) << 1;
        } else {
            ibit = s->gpio_regs[addr] & 2;
        }

        s->gpio_regs[addr] = value | ibit;
    }
}

static uint64_t macio_gpio_read(void *opaque, hwaddr addr, unsigned size)
{
    MacIOGPIOState *s = opaque;
    uint64_t val = 0;

    /* Levels regs */
    if (addr < 8) {
        val = s->gpio_levels[addr];
    } else {
        addr -= 8;

        if (addr < 36) {
            val = s->gpio_regs[addr];
        }
    }

    trace_macio_gpio_write(addr, val);
    return val;
}

static const MemoryRegionOps macio_gpio_ops = {
    .read = macio_gpio_read,
    .write = macio_gpio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static void macio_gpio_realize(DeviceState *dev, Error **errp)
{
    MacIOGPIOState *s = MACIO_GPIO(dev);

    s->gpio_extirqs[1] = qdev_get_gpio_in(DEVICE(s->pic),
                                          NEWWORLD_EXTING_GPIO1);
    s->gpio_extirqs[9] = qdev_get_gpio_in(DEVICE(s->pic),
                                          NEWWORLD_EXTING_GPIO9);
}

static void macio_gpio_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    MacIOGPIOState *s = MACIO_GPIO(obj);

    object_property_add_link(obj, "pic", TYPE_OPENPIC,
                             (Object **) &s->pic,
                             qdev_prop_allow_set_link_before_realize,
                             0, NULL);

    memory_region_init_io(&s->gpiomem, OBJECT(s), &macio_gpio_ops, obj,
                          "gpio", 0x30);
    sysbus_init_mmio(sbd, &s->gpiomem);
}

static const VMStateDescription vmstate_macio_gpio = {
    .name = "macio_gpio",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8_ARRAY(gpio_levels, MacIOGPIOState, 8),
        VMSTATE_UINT8_ARRAY(gpio_regs, MacIOGPIOState, 36),
        VMSTATE_END_OF_LIST()
    }
};

static void macio_gpio_reset(DeviceState *dev)
{
    MacIOGPIOState *s = MACIO_GPIO(dev);

    /* GPIO 1 is up by default */
    macio_set_gpio(s, 1, true);
}

static void macio_gpio_nmi(NMIState *n, int cpu_index, Error **errp)
{
    macio_set_gpio(MACIO_GPIO(n), 9, true);
    macio_set_gpio(MACIO_GPIO(n), 9, false);
}

static void macio_gpio_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    NMIClass *nc = NMI_CLASS(oc);

    dc->realize = macio_gpio_realize;
    dc->reset = macio_gpio_reset;
    dc->vmsd = &vmstate_macio_gpio;
    nc->nmi_monitor_handler = macio_gpio_nmi;
}

static const TypeInfo macio_gpio_init_info = {
    .name          = TYPE_MACIO_GPIO,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(MacIOGPIOState),
    .instance_init = macio_gpio_init,
    .class_init    = macio_gpio_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_NMI },
        { }
    },
};

static void macio_gpio_register_types(void)
{
    type_register_static(&macio_gpio_init_info);
}

type_init(macio_gpio_register_types)
