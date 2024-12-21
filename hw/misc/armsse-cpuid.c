/*
 * ARM SSE-200 CPU_IDENTITY register block
 *
 * Copyright (c) 2019 Linaro Limited
 * Written by Peter Maydell
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 or
 *  (at your option) any later version.
 */

/*
 * This is a model of the "CPU_IDENTITY" register block which is part of the
 * Arm SSE-200 and documented in
 * https://developer.arm.com/documentation/101104/latest/
 *
 * It consists of one read-only CPUID register (set by QOM property), plus the
 * usual ID registers.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "trace.h"
#include "qapi/error.h"
#include "hw/sysbus.h"
#include "hw/registerfields.h"
#include "hw/misc/armsse-cpuid.h"
#include "hw/qdev-properties.h"

REG32(CPUID, 0x0)
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

static uint64_t armsse_cpuid_read(void *opaque, hwaddr offset,
                                    unsigned size)
{
    ARMSSECPUID *s = ARMSSE_CPUID(opaque);
    uint64_t r;

    switch (offset) {
    case A_CPUID:
        r = s->cpuid;
        break;
    case A_PID4 ... A_CID3:
        r = sysinfo_id[(offset - A_PID4) / 4];
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "SSE CPU_IDENTITY read: bad offset 0x%x\n", (int)offset);
        r = 0;
        break;
    }
    trace_armsse_cpuid_read(offset, r, size);
    return r;
}

static void armsse_cpuid_write(void *opaque, hwaddr offset,
                                 uint64_t value, unsigned size)
{
    trace_armsse_cpuid_write(offset, value, size);

    qemu_log_mask(LOG_GUEST_ERROR,
                  "SSE CPU_IDENTITY: write to RO offset 0x%x\n", (int)offset);
}

static const MemoryRegionOps armsse_cpuid_ops = {
    .read = armsse_cpuid_read,
    .write = armsse_cpuid_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    /* byte/halfword accesses are just zero-padded on reads and writes */
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
};

static const Property armsse_cpuid_props[] = {
    DEFINE_PROP_UINT32("CPUID", ARMSSECPUID, cpuid, 0),
};

static void armsse_cpuid_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    ARMSSECPUID *s = ARMSSE_CPUID(obj);

    memory_region_init_io(&s->iomem, obj, &armsse_cpuid_ops,
                          s, "armsse-cpuid", 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
}

static void armsse_cpuid_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    /*
     * This device has no guest-modifiable state and so it
     * does not need a reset function or VMState.
     */

    device_class_set_props(dc, armsse_cpuid_props);
}

static const TypeInfo armsse_cpuid_info = {
    .name = TYPE_ARMSSE_CPUID,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ARMSSECPUID),
    .instance_init = armsse_cpuid_init,
    .class_init = armsse_cpuid_class_init,
};

static void armsse_cpuid_register_types(void)
{
    type_register_static(&armsse_cpuid_info);
}

type_init(armsse_cpuid_register_types);
