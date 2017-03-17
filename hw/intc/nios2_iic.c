/*
 * QEMU Altera Internal Interrupt Controller.
 *
 * Copyright (c) 2012 Chris Wulff <crwulff@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see
 * <http://www.gnu.org/licenses/lgpl-2.1.html>
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qapi/error.h"

#include "hw/sysbus.h"
#include "cpu.h"

#define TYPE_ALTERA_IIC "altera,iic"
#define ALTERA_IIC(obj) \
    OBJECT_CHECK(AlteraIIC, (obj), TYPE_ALTERA_IIC)

typedef struct AlteraIIC {
    SysBusDevice  parent_obj;
    void         *cpu;
    qemu_irq      parent_irq;
} AlteraIIC;

static void update_irq(AlteraIIC *pv)
{
    CPUNios2State *env = &((Nios2CPU *)(pv->cpu))->env;

    qemu_set_irq(pv->parent_irq,
                 env->regs[CR_IPENDING] & env->regs[CR_IENABLE]);
}

static void irq_handler(void *opaque, int irq, int level)
{
    AlteraIIC *pv = opaque;
    CPUNios2State *env = &((Nios2CPU *)(pv->cpu))->env;

    env->regs[CR_IPENDING] &= ~(1 << irq);
    env->regs[CR_IPENDING] |= !!level << irq;

    update_irq(pv);
}

static void altera_iic_init(Object *obj)
{
    AlteraIIC *pv = ALTERA_IIC(obj);

    qdev_init_gpio_in(DEVICE(pv), irq_handler, 32);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &pv->parent_irq);
}

static void altera_iic_realize(DeviceState *dev, Error **errp)
{
    struct AlteraIIC *pv = ALTERA_IIC(dev);
    Error *err = NULL;

    pv->cpu = object_property_get_link(OBJECT(dev), "cpu", &err);
    if (!pv->cpu) {
        error_setg(errp, "altera,iic: CPU link not found: %s",
                   error_get_pretty(err));
        return;
    }
}

static void altera_iic_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    /* Reason: needs to be wired up, e.g. by nios2_10m50_ghrd_init() */
    dc->cannot_instantiate_with_device_add_yet = true;
    dc->realize = altera_iic_realize;
}

static TypeInfo altera_iic_info = {
    .name          = "altera,iic",
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AlteraIIC),
    .instance_init = altera_iic_init,
    .class_init    = altera_iic_class_init,
};

static void altera_iic_register(void)
{
    type_register_static(&altera_iic_info);
}

type_init(altera_iic_register)
