/*
 * IMX7 Secure Non-Volatile Storage
 *
 * Copyright (c) 2018, Impinj, Inc.
 *
 * Author: Andrey Smirnov <andrew.smirnov@gmail.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 * Bare minimum emulation code needed to support being able to shut
 * down linux guest gracefully.
 */

#include "qemu/osdep.h"
#include "hw/misc/imx7_snvs.h"
#include "qemu/module.h"
#include "sysemu/runstate.h"
#include "trace.h"

static uint64_t imx7_snvs_read(void *opaque, hwaddr offset, unsigned size)
{
    trace_imx7_snvs_read(offset, 0);

    return 0;
}

static void imx7_snvs_write(void *opaque, hwaddr offset,
                            uint64_t v, unsigned size)
{
    const uint32_t value = v;
    const uint32_t mask  = SNVS_LPCR_TOP | SNVS_LPCR_DP_EN;

    trace_imx7_snvs_write(offset, value);

    if (offset == SNVS_LPCR && ((value & mask) == mask)) {
        qemu_system_shutdown_request(SHUTDOWN_CAUSE_GUEST_SHUTDOWN);
    }
}

static const struct MemoryRegionOps imx7_snvs_ops = {
    .read = imx7_snvs_read,
    .write = imx7_snvs_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {
        /*
         * Our device would not work correctly if the guest was doing
         * unaligned access. This might not be a limitation on the real
         * device but in practice there is no reason for a guest to access
         * this device unaligned.
         */
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
};

static void imx7_snvs_init(Object *obj)
{
    SysBusDevice *sd = SYS_BUS_DEVICE(obj);
    IMX7SNVSState *s = IMX7_SNVS(obj);

    memory_region_init_io(&s->mmio, obj, &imx7_snvs_ops, s,
                          TYPE_IMX7_SNVS, 0x1000);

    sysbus_init_mmio(sd, &s->mmio);
}

static void imx7_snvs_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc  = "i.MX7 Secure Non-Volatile Storage Module";
}

static const TypeInfo imx7_snvs_info = {
    .name          = TYPE_IMX7_SNVS,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IMX7SNVSState),
    .instance_init = imx7_snvs_init,
    .class_init    = imx7_snvs_class_init,
};

static void imx7_snvs_register_type(void)
{
    type_register_static(&imx7_snvs_info);
}
type_init(imx7_snvs_register_type)
