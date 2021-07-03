/*
 * BCM2835 Power Management emulation
 *
 * Copyright (C) 2017 Marcin Chojnacki <marcinch7@gmail.com>
 * Copyright (C) 2021 Nolan Leake <nolan@sigbus.net>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/misc/bcm2835_powermgt.h"
#include "migration/vmstate.h"
#include "sysemu/runstate.h"

#define PASSWORD 0x5a000000
#define PASSWORD_MASK 0xff000000

#define R_RSTC 0x1c
#define V_RSTC_RESET 0x20
#define R_RSTS 0x20
#define V_RSTS_POWEROFF 0x555 /* Linux uses partition 63 to indicate halt. */
#define R_WDOG 0x24

static uint64_t bcm2835_powermgt_read(void *opaque, hwaddr offset,
                                      unsigned size)
{
    BCM2835PowerMgtState *s = (BCM2835PowerMgtState *)opaque;
    uint32_t res = 0;

    switch (offset) {
    case R_RSTC:
        res = s->rstc;
        break;
    case R_RSTS:
        res = s->rsts;
        break;
    case R_WDOG:
        res = s->wdog;
        break;

    default:
        qemu_log_mask(LOG_UNIMP,
                      "bcm2835_powermgt_read: Unknown offset 0x%08"HWADDR_PRIx
                      "\n", offset);
        res = 0;
        break;
    }

    return res;
}

static void bcm2835_powermgt_write(void *opaque, hwaddr offset,
                                   uint64_t value, unsigned size)
{
    BCM2835PowerMgtState *s = (BCM2835PowerMgtState *)opaque;

    if ((value & PASSWORD_MASK) != PASSWORD) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "bcm2835_powermgt_write: Bad password 0x%"PRIx64
                      " at offset 0x%08"HWADDR_PRIx"\n",
                      value, offset);
        return;
    }

    value = value & ~PASSWORD_MASK;

    switch (offset) {
    case R_RSTC:
        s->rstc = value;
        if (value & V_RSTC_RESET) {
            if ((s->rsts & 0xfff) == V_RSTS_POWEROFF) {
                qemu_system_shutdown_request(SHUTDOWN_CAUSE_GUEST_SHUTDOWN);
            } else {
                qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
            }
        }
        break;
    case R_RSTS:
        qemu_log_mask(LOG_UNIMP,
                      "bcm2835_powermgt_write: RSTS\n");
        s->rsts = value;
        break;
    case R_WDOG:
        qemu_log_mask(LOG_UNIMP,
                      "bcm2835_powermgt_write: WDOG\n");
        s->wdog = value;
        break;

    default:
        qemu_log_mask(LOG_UNIMP,
                      "bcm2835_powermgt_write: Unknown offset 0x%08"HWADDR_PRIx
                      "\n", offset);
        break;
    }
}

static const MemoryRegionOps bcm2835_powermgt_ops = {
    .read = bcm2835_powermgt_read,
    .write = bcm2835_powermgt_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
};

static const VMStateDescription vmstate_bcm2835_powermgt = {
    .name = TYPE_BCM2835_POWERMGT,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(rstc, BCM2835PowerMgtState),
        VMSTATE_UINT32(rsts, BCM2835PowerMgtState),
        VMSTATE_UINT32(wdog, BCM2835PowerMgtState),
        VMSTATE_END_OF_LIST()
    }
};

static void bcm2835_powermgt_init(Object *obj)
{
    BCM2835PowerMgtState *s = BCM2835_POWERMGT(obj);

    memory_region_init_io(&s->iomem, obj, &bcm2835_powermgt_ops, s,
                          TYPE_BCM2835_POWERMGT, 0x200);
    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->iomem);
}

static void bcm2835_powermgt_reset(DeviceState *dev)
{
    BCM2835PowerMgtState *s = BCM2835_POWERMGT(dev);

    /* https://elinux.org/BCM2835_registers#PM */
    s->rstc = 0x00000102;
    s->rsts = 0x00001000;
    s->wdog = 0x00000000;
}

static void bcm2835_powermgt_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = bcm2835_powermgt_reset;
    dc->vmsd = &vmstate_bcm2835_powermgt;
}

static TypeInfo bcm2835_powermgt_info = {
    .name          = TYPE_BCM2835_POWERMGT,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(BCM2835PowerMgtState),
    .class_init    = bcm2835_powermgt_class_init,
    .instance_init = bcm2835_powermgt_init,
};

static void bcm2835_powermgt_register_types(void)
{
    type_register_static(&bcm2835_powermgt_info);
}

type_init(bcm2835_powermgt_register_types)
