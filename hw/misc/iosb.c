/*
 * QEMU IOSB emulation
 *
 * Copyright (c) 2019 Laurent Vivier
 * Copyright (c) 2022 Mark Cave-Ayland
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "migration/vmstate.h"
#include "hw/sysbus.h"
#include "hw/misc/iosb.h"
#include "trace.h"

#define IOSB_SIZE          0x2000

#define IOSB_CONFIG        0x0
#define IOSB_CONFIG2       0x100
#define IOSB_SONIC_SCSI    0x200
#define IOSB_REVISION      0x300
#define IOSB_SCSI_RESID    0x400
#define IOSB_BRIGHTNESS    0x500
#define IOSB_TIMEOUT       0x600


static uint64_t iosb_read(void *opaque, hwaddr addr,
                          unsigned size)
{
    IOSBState *s = IOSB(opaque);
    uint64_t val = 0;

    switch (addr) {
    case IOSB_CONFIG:
    case IOSB_CONFIG2:
    case IOSB_SONIC_SCSI:
    case IOSB_REVISION:
    case IOSB_SCSI_RESID:
    case IOSB_BRIGHTNESS:
    case IOSB_TIMEOUT:
        val = s->regs[addr >> 8];
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "IOSB: unimplemented read addr=0x%"PRIx64
                                 " val=0x%"PRIx64 " size=%d\n",
                                 addr, val, size);
    }

    trace_iosb_read(addr, val, size);
    return val;
}

static void iosb_write(void *opaque, hwaddr addr, uint64_t val,
                       unsigned size)
{
    IOSBState *s = IOSB(opaque);

    switch (addr) {
    case IOSB_CONFIG:
    case IOSB_CONFIG2:
    case IOSB_SONIC_SCSI:
    case IOSB_REVISION:
    case IOSB_SCSI_RESID:
    case IOSB_BRIGHTNESS:
    case IOSB_TIMEOUT:
        s->regs[addr >> 8] = val;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "IOSB: unimplemented write addr=0x%"PRIx64
                                 " val=0x%"PRIx64 " size=%d\n",
                                 addr, val, size);
    }

    trace_iosb_write(addr, val, size);
}

static const MemoryRegionOps iosb_mmio_ops = {
    .read = iosb_read,
    .write = iosb_write,
    .endianness = DEVICE_BIG_ENDIAN,
};

static void iosb_reset_hold(Object *obj, ResetType type)
{
    IOSBState *s = IOSB(obj);

    memset(s->regs, 0, sizeof(s->regs));

    /* BCLK 33 MHz */
    s->regs[IOSB_CONFIG >> 8] = 1;
}

static void iosb_init(Object *obj)
{
    IOSBState *s = IOSB(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->mem_regs, obj, &iosb_mmio_ops, s, "IOSB",
                          IOSB_SIZE);
    sysbus_init_mmio(sbd, &s->mem_regs);
}

static const VMStateDescription vmstate_iosb = {
    .name = "IOSB",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, IOSBState, IOSB_REGS),
        VMSTATE_END_OF_LIST()
    }
};

static void iosb_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    ResettableClass *rc = RESETTABLE_CLASS(oc);

    dc->vmsd = &vmstate_iosb;
    rc->phases.hold = iosb_reset_hold;
}

static const TypeInfo iosb_info_types[] = {
    {
        .name          = TYPE_IOSB,
        .parent        = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(IOSBState),
        .instance_init = iosb_init,
        .class_init    = iosb_class_init,
    },
};

DEFINE_TYPES(iosb_info_types)
