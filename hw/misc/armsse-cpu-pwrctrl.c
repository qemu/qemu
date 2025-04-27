/*
 * Arm SSE CPU PWRCTRL register block
 *
 * Copyright (c) 2021 Linaro Limited
 * Written by Peter Maydell
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 or
 *  (at your option) any later version.
 */

/*
 * This is a model of the "CPU<N>_PWRCTRL block" which is part of the
 * Arm Corstone SSE-300 Example Subsystem and documented in
 * https://developer.arm.com/documentation/101773/0000
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "trace.h"
#include "qapi/error.h"
#include "migration/vmstate.h"
#include "hw/sysbus.h"
#include "hw/registerfields.h"
#include "hw/misc/armsse-cpu-pwrctrl.h"

REG32(CPUPWRCFG, 0x0)
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
static const int cpu_pwrctrl_id[] = {
    0x04, 0x00, 0x00, 0x00, /* PID4..PID7 */
    0x5a, 0xb8, 0x0b, 0x00, /* PID0..PID3 */
    0x0d, 0xf0, 0x05, 0xb1, /* CID0..CID3 */
};

static uint64_t pwrctrl_read(void *opaque, hwaddr offset, unsigned size)
{
    ARMSSECPUPwrCtrl *s = ARMSSE_CPU_PWRCTRL(opaque);
    uint64_t r;

    switch (offset) {
    case A_CPUPWRCFG:
        r = s->cpupwrcfg;
        break;
    case A_PID4 ... A_CID3:
        r = cpu_pwrctrl_id[(offset - A_PID4) / 4];
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "SSE CPU_PWRCTRL read: bad offset %x\n", (int)offset);
        r = 0;
        break;
    }
    trace_armsse_cpu_pwrctrl_read(offset, r, size);
    return r;
}

static void pwrctrl_write(void *opaque, hwaddr offset,
                          uint64_t value, unsigned size)
{
    ARMSSECPUPwrCtrl *s = ARMSSE_CPU_PWRCTRL(opaque);

    trace_armsse_cpu_pwrctrl_write(offset, value, size);

    switch (offset) {
    case A_CPUPWRCFG:
        qemu_log_mask(LOG_UNIMP,
                      "SSE CPU_PWRCTRL: CPUPWRCFG unimplemented\n");
        s->cpupwrcfg = value;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "SSE CPU_PWRCTRL write: bad offset 0x%x\n", (int)offset);
        break;
    }
}

static const MemoryRegionOps pwrctrl_ops = {
    .read = pwrctrl_read,
    .write = pwrctrl_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};

static void pwrctrl_reset(DeviceState *dev)
{
    ARMSSECPUPwrCtrl *s = ARMSSE_CPU_PWRCTRL(dev);

    s->cpupwrcfg = 0;
}

static const VMStateDescription pwrctrl_vmstate = {
    .name = "armsse-cpu-pwrctrl",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(cpupwrcfg, ARMSSECPUPwrCtrl),
        VMSTATE_END_OF_LIST()
    },
};

static void pwrctrl_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    ARMSSECPUPwrCtrl *s = ARMSSE_CPU_PWRCTRL(obj);

    memory_region_init_io(&s->iomem, obj, &pwrctrl_ops,
                          s, "armsse-cpu-pwrctrl", 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
}

static void pwrctrl_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, pwrctrl_reset);
    dc->vmsd = &pwrctrl_vmstate;
}

static const TypeInfo pwrctrl_info = {
    .name = TYPE_ARMSSE_CPU_PWRCTRL,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ARMSSECPUPwrCtrl),
    .instance_init = pwrctrl_init,
    .class_init = pwrctrl_class_init,
};

static void pwrctrl_register_types(void)
{
    type_register_static(&pwrctrl_info);
}

type_init(pwrctrl_register_types);
