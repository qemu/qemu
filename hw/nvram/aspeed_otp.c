/*
 *  ASPEED OTP (One-Time Programmable) memory
 *
 *  Copyright (C) 2025 Aspeed
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "system/block-backend.h"
#include "hw/qdev-properties.h"
#include "hw/nvram/aspeed_otp.h"

static uint64_t aspeed_otp_read(void *opaque, hwaddr offset, unsigned size)
{
    AspeedOTPState *s = opaque;
    uint64_t val = 0;

    memcpy(&val, s->storage + offset, size);

    return val;
}

static void aspeed_otp_write(void *opaque, hwaddr otp_addr,
                                uint64_t val, unsigned size)
{
    AspeedOTPState *s = opaque;

    memcpy(s->storage + otp_addr, &val, size);
}

static bool aspeed_otp_init_storage(AspeedOTPState *s, Error **errp)
{
    uint32_t *p;
    int i, num;

        num = s->size / sizeof(uint32_t);
        p = (uint32_t *)s->storage;
        for (i = 0; i < num; i++) {
            p[i] = (i % 2 == 0) ? 0x00000000 : 0xFFFFFFFF;
        }

    return true;
}

static const MemoryRegionOps aspeed_otp_ops = {
    .read = aspeed_otp_read,
    .write = aspeed_otp_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
};

static void aspeed_otp_realize(DeviceState *dev, Error **errp)
{
    AspeedOTPState *s = ASPEED_OTP(dev);

    if (s->size == 0) {
        error_setg(errp, "aspeed.otp: 'size' property must be set");
        return;
    }

    s->storage = blk_blockalign(s->blk, s->size);

    if (!aspeed_otp_init_storage(s, errp)) {
        return;
    }

    memory_region_init_io(&s->mmio, OBJECT(dev), &aspeed_otp_ops,
                          s, "aspeed.otp", s->size);
    address_space_init(&s->as, &s->mmio, NULL);
}

static const Property aspeed_otp_properties[] = {
    DEFINE_PROP_UINT64("size", AspeedOTPState, size, 0),
};

static void aspeed_otp_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->realize = aspeed_otp_realize;
    device_class_set_props(dc, aspeed_otp_properties);
}

static const TypeInfo aspeed_otp_info = {
    .name          = TYPE_ASPEED_OTP,
    .parent        = TYPE_DEVICE,
    .instance_size = sizeof(AspeedOTPState),
    .class_init    = aspeed_otp_class_init,
};

static void aspeed_otp_register_types(void)
{
    type_register_static(&aspeed_otp_info);
}

type_init(aspeed_otp_register_types)
