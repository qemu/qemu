/*
 * BCM2835 dummy thermal sensor
 *
 * Copyright (C) 2019 Philippe Mathieu-Daudé <f4bug@amsat.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "hw/misc/bcm2835_thermal.h"
#include "hw/registerfields.h"
#include "migration/vmstate.h"

REG32(CTL, 0)
FIELD(CTL, POWER_DOWN, 0, 1)
FIELD(CTL, RESET, 1, 1)
FIELD(CTL, BANDGAP_CTRL, 2, 3)
FIELD(CTL, INTERRUPT_ENABLE, 5, 1)
FIELD(CTL, DIRECT, 6, 1)
FIELD(CTL, INTERRUPT_CLEAR, 7, 1)
FIELD(CTL, HOLD, 8, 10)
FIELD(CTL, RESET_DELAY, 18, 8)
FIELD(CTL, REGULATOR_ENABLE, 26, 1)

REG32(STAT, 4)
FIELD(STAT, DATA, 0, 10)
FIELD(STAT, VALID, 10, 1)
FIELD(STAT, INTERRUPT, 11, 1)

#define THERMAL_OFFSET_C 412
#define THERMAL_COEFF  (-0.538f)

static uint16_t bcm2835_thermal_temp2adc(int temp_C)
{
    return (temp_C - THERMAL_OFFSET_C) / THERMAL_COEFF;
}

static uint64_t bcm2835_thermal_read(void *opaque, hwaddr addr, unsigned size)
{
    Bcm2835ThermalState *s = BCM2835_THERMAL(opaque);
    uint32_t val = 0;

    switch (addr) {
    case A_CTL:
        val = s->ctl;
        break;
    case A_STAT:
        /* Temperature is constantly 25°C. */
        val = FIELD_DP32(bcm2835_thermal_temp2adc(25), STAT, VALID, true);
        break;
    default:
        /* MemoryRegionOps are aligned, so this can not happen. */
        g_assert_not_reached();
    }
    return val;
}

static void bcm2835_thermal_write(void *opaque, hwaddr addr,
                                  uint64_t value, unsigned size)
{
    Bcm2835ThermalState *s = BCM2835_THERMAL(opaque);

    switch (addr) {
    case A_CTL:
        s->ctl = value;
        break;
    case A_STAT:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: write 0x%" PRIx64
                                       " to 0x%" HWADDR_PRIx "\n",
                       __func__, value, addr);
        break;
    default:
        /* MemoryRegionOps are aligned, so this can not happen. */
        g_assert_not_reached();
    }
}

static const MemoryRegionOps bcm2835_thermal_ops = {
    .read = bcm2835_thermal_read,
    .write = bcm2835_thermal_write,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void bcm2835_thermal_reset(DeviceState *dev)
{
    Bcm2835ThermalState *s = BCM2835_THERMAL(dev);

    s->ctl = 0;
}

static void bcm2835_thermal_realize(DeviceState *dev, Error **errp)
{
    Bcm2835ThermalState *s = BCM2835_THERMAL(dev);

    memory_region_init_io(&s->iomem, OBJECT(s), &bcm2835_thermal_ops,
                          s, TYPE_BCM2835_THERMAL, 8);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->iomem);
}

static const VMStateDescription bcm2835_thermal_vmstate = {
    .name = "bcm2835_thermal",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(ctl, Bcm2835ThermalState),
        VMSTATE_END_OF_LIST()
    }
};

static void bcm2835_thermal_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = bcm2835_thermal_realize;
    device_class_set_legacy_reset(dc, bcm2835_thermal_reset);
    dc->vmsd = &bcm2835_thermal_vmstate;
}

static const TypeInfo bcm2835_thermal_info = {
    .name = TYPE_BCM2835_THERMAL,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Bcm2835ThermalState),
    .class_init = bcm2835_thermal_class_init,
};

static void bcm2835_thermal_register_types(void)
{
    type_register_static(&bcm2835_thermal_info);
}

type_init(bcm2835_thermal_register_types)
