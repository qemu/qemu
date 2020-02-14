/*
 * Copyright (c) 2018, Impinj, Inc.
 *
 * i.MX2 Watchdog IP block
 *
 * Author: Andrey Smirnov <andrew.smirnov@gmail.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "qemu/module.h"
#include "sysemu/watchdog.h"

#include "hw/misc/imx2_wdt.h"

#define IMX2_WDT_WCR_WDA    BIT(5)      /* -> External Reset WDOG_B */
#define IMX2_WDT_WCR_SRS    BIT(4)      /* -> Software Reset Signal */

static uint64_t imx2_wdt_read(void *opaque, hwaddr addr,
                              unsigned int size)
{
    return 0;
}

static void imx2_wdt_write(void *opaque, hwaddr addr,
                           uint64_t value, unsigned int size)
{
    if (addr == IMX2_WDT_WCR &&
        (~value & (IMX2_WDT_WCR_WDA | IMX2_WDT_WCR_SRS))) {
        watchdog_perform_action();
    }
}

static const MemoryRegionOps imx2_wdt_ops = {
    .read  = imx2_wdt_read,
    .write = imx2_wdt_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {
        /*
         * Our device would not work correctly if the guest was doing
         * unaligned access. This might not be a limitation on the
         * real device but in practice there is no reason for a guest
         * to access this device unaligned.
         */
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
};

static void imx2_wdt_realize(DeviceState *dev, Error **errp)
{
    IMX2WdtState *s = IMX2_WDT(dev);

    memory_region_init_io(&s->mmio, OBJECT(dev),
                          &imx2_wdt_ops, s,
                          TYPE_IMX2_WDT".mmio",
                          IMX2_WDT_REG_NUM * sizeof(uint16_t));
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->mmio);
}

static void imx2_wdt_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = imx2_wdt_realize;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static const TypeInfo imx2_wdt_info = {
    .name          = TYPE_IMX2_WDT,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IMX2WdtState),
    .class_init    = imx2_wdt_class_init,
};

static WatchdogTimerModel model = {
    .wdt_name = "imx2-watchdog",
    .wdt_description = "i.MX2 Watchdog",
};

static void imx2_wdt_register_type(void)
{
    watchdog_add_model(&model);
    type_register_static(&imx2_wdt_info);
}
type_init(imx2_wdt_register_type)
