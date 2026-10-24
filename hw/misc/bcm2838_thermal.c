/*
 * BCM2838 dummy thermal sensor
 *
 * Copyright (C) 2022 Maksim Kopusov <maksim.kopusov@auriga.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "hw/misc/bcm2838_thermal.h"
#include "hw/core/registerfields.h"
#include "migration/vmstate.h"
#include "qemu/error-report.h"

REG32(STAT, 0x200)
FIELD(STAT, DATA, 0, 10)
FIELD(STAT, VALID_1, 10, 1)
FIELD(STAT, VALID_2, 16, 1)

#define BCM2838_THERMAL_SIZE 0xf00

#define THERMAL_OFFSET_C 410040
#define THERMAL_COEFF  (-487.0f)
#define MILLIDEGREE_COEFF 1000

static uint16_t bcm2838_thermal_temp2adc(int temp_C)
{
    return (temp_C * MILLIDEGREE_COEFF - THERMAL_OFFSET_C) / THERMAL_COEFF;
}

static uint64_t bcm2838_thermal_read(void *opaque, hwaddr addr, unsigned size)
{
    uint32_t val = 0;

    switch (addr) {
    case A_STAT:
        /* Temperature is always 25°C */
        val = FIELD_DP32(val, STAT, DATA, bcm2838_thermal_temp2adc(25));
        val = FIELD_DP32(val, STAT, VALID_1, 1);
        val = FIELD_DP32(val, STAT, VALID_2, 1);

        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s can't access addr: 0x%"HWADDR_PRIx,
                     TYPE_BCM2838_THERMAL, addr);
    }
    return val;
}

static void bcm2838_thermal_write(void *opaque, hwaddr addr,
                                      uint64_t value, unsigned size)
{
    qemu_log_mask(LOG_GUEST_ERROR, "%s: write 0x%" PRIx64
                                " to 0x%" HWADDR_PRIx "\n",
                __func__, value, addr);
}

static const MemoryRegionOps bcm2838_thermal_ops = {
    .read = bcm2838_thermal_read,
    .write = bcm2838_thermal_write,
    .impl.max_access_size = 4,
    .valid.min_access_size = 4,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void bcm2838_thermal_realize(DeviceState *dev, Error **errp)
{
    Bcm2838ThermalState *s = BCM2838_THERMAL(dev);

    memory_region_init_io(&s->iomem, OBJECT(s), &bcm2838_thermal_ops,
                          s, TYPE_BCM2838_THERMAL, BCM2838_THERMAL_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->iomem);
}

static void bcm2838_thermal_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = bcm2838_thermal_realize;

    /* This device has no state: no need for vmstate or reset */
}

static const TypeInfo bcm2838_thermal_info = {
    .name = TYPE_BCM2838_THERMAL,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Bcm2838ThermalState),
    .class_init = bcm2838_thermal_class_init,
};

static void bcm2838_thermal_register_types(void)
{
    type_register_static(&bcm2838_thermal_info);
}

type_init(bcm2838_thermal_register_types)
