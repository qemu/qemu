/*
 * ASPEED PWM Controller
 *
 * Copyright (C) 2017-2021 IBM Corp.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "hw/misc/aspeed_pwm.h"
#include "qapi/error.h"
#include "migration/vmstate.h"

#include "trace.h"

static uint64_t aspeed_pwm_read(void *opaque, hwaddr addr,
                                     unsigned int size)
{
    AspeedPWMState *s = ASPEED_PWM(opaque);
    uint64_t val = 0;

    addr >>= 2;

    if (addr >= ASPEED_PWM_NR_REGS) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Out-of-bounds read at offset 0x%" HWADDR_PRIx "\n",
                      __func__, addr << 2);
    } else {
        val = s->regs[addr];
    }

    trace_aspeed_pwm_read(addr << 2, val);

    return val;
}

static void aspeed_pwm_write(void *opaque, hwaddr addr, uint64_t data,
                              unsigned int size)
{
    AspeedPWMState *s = ASPEED_PWM(opaque);

    trace_aspeed_pwm_write(addr, data);

    addr >>= 2;

    if (addr >= ASPEED_PWM_NR_REGS) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Out-of-bounds write at offset 0x%" HWADDR_PRIx "\n",
                      __func__, addr << 2);
        return;
    }

    s->regs[addr] = data;
}

static const MemoryRegionOps aspeed_pwm_ops = {
    .read = aspeed_pwm_read,
    .write = aspeed_pwm_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static void aspeed_pwm_reset(DeviceState *dev)
{
    struct AspeedPWMState *s = ASPEED_PWM(dev);

    memset(s->regs, 0, sizeof(s->regs));
}

static void aspeed_pwm_realize(DeviceState *dev, Error **errp)
{
    AspeedPWMState *s = ASPEED_PWM(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    sysbus_init_irq(sbd, &s->irq);

    memory_region_init_io(&s->iomem, OBJECT(s), &aspeed_pwm_ops, s,
            TYPE_ASPEED_PWM, 0x1000);

    sysbus_init_mmio(sbd, &s->iomem);
}

static const VMStateDescription vmstate_aspeed_pwm = {
    .name = TYPE_ASPEED_PWM,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, AspeedPWMState, ASPEED_PWM_NR_REGS),
        VMSTATE_END_OF_LIST(),
    }
};

static void aspeed_pwm_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = aspeed_pwm_realize;
    device_class_set_legacy_reset(dc, aspeed_pwm_reset);
    dc->desc = "Aspeed PWM Controller";
    dc->vmsd = &vmstate_aspeed_pwm;
}

static const TypeInfo aspeed_pwm_info = {
    .name = TYPE_ASPEED_PWM,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AspeedPWMState),
    .class_init = aspeed_pwm_class_init,
};

static void aspeed_pwm_register_types(void)
{
    type_register_static(&aspeed_pwm_info);
}

type_init(aspeed_pwm_register_types);
