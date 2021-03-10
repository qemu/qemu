/*
 * ARM IoTKit system information block
 *
 * Copyright (c) 2018 Linaro Limited
 * Written by Peter Maydell
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 or
 *  (at your option) any later version.
 */

/*
 * This is a model of the "system information block" which is part of the
 * Arm IoTKit and documented in
 * https://developer.arm.com/documentation/ecm0601256/latest
 * It consists of 2 read-only version/config registers, plus the
 * usual ID registers.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "trace.h"
#include "qapi/error.h"
#include "hw/sysbus.h"
#include "hw/registerfields.h"
#include "hw/misc/iotkit-sysinfo.h"
#include "hw/qdev-properties.h"
#include "hw/arm/armsse-version.h"

REG32(SYS_VERSION, 0x0)
REG32(SYS_CONFIG, 0x4)
REG32(SYS_CONFIG1, 0x8)
REG32(IIDR, 0xfc8)
REG32(PID4, 0xfd0)
REG32(PID5, 0xfd4)
REG32(PID6, 0xfd8)
REG32(PID7, 0xfdc)
REG32(PID0, 0xfe0)
REG32(PID1, 0xfe4)
REG32(PID2, 0xfe8)
REG32(PID3, 0xfec)
REG32(CID0, 0xff0)
REG32(CID1, 0xff4)
REG32(CID2, 0xff8)
REG32(CID3, 0xffc)

/* PID/CID values */
static const int sysinfo_id[] = {
    0x04, 0x00, 0x00, 0x00, /* PID4..PID7 */
    0x58, 0xb8, 0x0b, 0x00, /* PID0..PID3 */
    0x0d, 0xf0, 0x05, 0xb1, /* CID0..CID3 */
};

static const int sysinfo_sse300_id[] = {
    0x04, 0x00, 0x00, 0x00, /* PID4..PID7 */
    0x58, 0xb8, 0x1b, 0x00, /* PID0..PID3 */
    0x0d, 0xf0, 0x05, 0xb1, /* CID0..CID3 */
};

static uint64_t iotkit_sysinfo_read(void *opaque, hwaddr offset,
                                    unsigned size)
{
    IoTKitSysInfo *s = IOTKIT_SYSINFO(opaque);
    uint64_t r;

    switch (offset) {
    case A_SYS_VERSION:
        r = s->sys_version;
        break;

    case A_SYS_CONFIG:
        r = s->sys_config;
        break;
    case A_SYS_CONFIG1:
        switch (s->sse_version) {
        case ARMSSE_SSE300:
            return 0;
            break;
        default:
            goto bad_read;
        }
        break;
    case A_IIDR:
        switch (s->sse_version) {
        case ARMSSE_SSE300:
            return s->iidr;
            break;
        default:
            goto bad_read;
        }
        break;
    case A_PID4 ... A_CID3:
        switch (s->sse_version) {
        case ARMSSE_SSE300:
            r = sysinfo_sse300_id[(offset - A_PID4) / 4];
            break;
        default:
            r = sysinfo_id[(offset - A_PID4) / 4];
            break;
        }
        break;
    default:
    bad_read:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "IoTKit SysInfo read: bad offset %x\n", (int)offset);
        r = 0;
        break;
    }
    trace_iotkit_sysinfo_read(offset, r, size);
    return r;
}

static void iotkit_sysinfo_write(void *opaque, hwaddr offset,
                                 uint64_t value, unsigned size)
{
    trace_iotkit_sysinfo_write(offset, value, size);

    qemu_log_mask(LOG_GUEST_ERROR,
                  "IoTKit SysInfo: write to RO offset 0x%x\n", (int)offset);
}

static const MemoryRegionOps iotkit_sysinfo_ops = {
    .read = iotkit_sysinfo_read,
    .write = iotkit_sysinfo_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    /* byte/halfword accesses are just zero-padded on reads and writes */
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
};

static Property iotkit_sysinfo_props[] = {
    DEFINE_PROP_UINT32("SYS_VERSION", IoTKitSysInfo, sys_version, 0),
    DEFINE_PROP_UINT32("SYS_CONFIG", IoTKitSysInfo, sys_config, 0),
    DEFINE_PROP_UINT32("sse-version", IoTKitSysInfo, sse_version, 0),
    DEFINE_PROP_UINT32("IIDR", IoTKitSysInfo, iidr, 0),
    DEFINE_PROP_END_OF_LIST()
};

static void iotkit_sysinfo_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    IoTKitSysInfo *s = IOTKIT_SYSINFO(obj);

    memory_region_init_io(&s->iomem, obj, &iotkit_sysinfo_ops,
                          s, "iotkit-sysinfo", 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
}

static void iotkit_sysinfo_realize(DeviceState *dev, Error **errp)
{
    IoTKitSysInfo *s = IOTKIT_SYSINFO(dev);

    if (!armsse_version_valid(s->sse_version)) {
        error_setg(errp, "invalid sse-version value %d", s->sse_version);
        return;
    }
}

static void iotkit_sysinfo_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    /*
     * This device has no guest-modifiable state and so it
     * does not need a reset function or VMState.
     */
    dc->realize = iotkit_sysinfo_realize;
    device_class_set_props(dc, iotkit_sysinfo_props);
}

static const TypeInfo iotkit_sysinfo_info = {
    .name = TYPE_IOTKIT_SYSINFO,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IoTKitSysInfo),
    .instance_init = iotkit_sysinfo_init,
    .class_init = iotkit_sysinfo_class_init,
};

static void iotkit_sysinfo_register_types(void)
{
    type_register_static(&iotkit_sysinfo_info);
}

type_init(iotkit_sysinfo_register_types);
