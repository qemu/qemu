/*
 * ARM SSE-200 Message Handling Unit (MHU)
 *
 * Copyright (c) 2019 Linaro Limited
 * Written by Peter Maydell
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 or
 *  (at your option) any later version.
 */

/*
 * This is a model of the Message Handling Unit (MHU) which is part of the
 * Arm SSE-200 and documented in
 * https://developer.arm.com/documentation/101104/latest/
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "trace.h"
#include "qapi/error.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "hw/registerfields.h"
#include "hw/irq.h"
#include "hw/misc/armsse-mhu.h"

REG32(CPU0INTR_STAT, 0x0)
REG32(CPU0INTR_SET, 0x4)
REG32(CPU0INTR_CLR, 0x8)
REG32(CPU1INTR_STAT, 0x10)
REG32(CPU1INTR_SET, 0x14)
REG32(CPU1INTR_CLR, 0x18)
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

/* Valid bits in the interrupt registers. If any are set the IRQ is raised */
#define INTR_MASK 0xf

/* PID/CID values */
static const int armsse_mhu_id[] = {
    0x04, 0x00, 0x00, 0x00, /* PID4..PID7 */
    0x56, 0xb8, 0x0b, 0x00, /* PID0..PID3 */
    0x0d, 0xf0, 0x05, 0xb1, /* CID0..CID3 */
};

static void armsse_mhu_update(ARMSSEMHU *s)
{
    qemu_set_irq(s->cpu0irq, s->cpu0intr != 0);
    qemu_set_irq(s->cpu1irq, s->cpu1intr != 0);
}

static uint64_t armsse_mhu_read(void *opaque, hwaddr offset, unsigned size)
{
    ARMSSEMHU *s = ARMSSE_MHU(opaque);
    uint64_t r;

    switch (offset) {
    case A_CPU0INTR_STAT:
        r = s->cpu0intr;
        break;

    case A_CPU1INTR_STAT:
        r = s->cpu1intr;
        break;

    case A_PID4 ... A_CID3:
        r = armsse_mhu_id[(offset - A_PID4) / 4];
        break;

    case A_CPU0INTR_SET:
    case A_CPU0INTR_CLR:
    case A_CPU1INTR_SET:
    case A_CPU1INTR_CLR:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "SSE MHU: read of write-only register at offset 0x%x\n",
                      (int)offset);
        r = 0;
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "SSE MHU read: bad offset 0x%x\n", (int)offset);
        r = 0;
        break;
    }
    trace_armsse_mhu_read(offset, r, size);
    return r;
}

static void armsse_mhu_write(void *opaque, hwaddr offset,
                             uint64_t value, unsigned size)
{
    ARMSSEMHU *s = ARMSSE_MHU(opaque);

    trace_armsse_mhu_write(offset, value, size);

    switch (offset) {
    case A_CPU0INTR_SET:
        s->cpu0intr |= (value & INTR_MASK);
        break;
    case A_CPU0INTR_CLR:
        s->cpu0intr &= ~(value & INTR_MASK);
        break;
    case A_CPU1INTR_SET:
        s->cpu1intr |= (value & INTR_MASK);
        break;
    case A_CPU1INTR_CLR:
        s->cpu1intr &= ~(value & INTR_MASK);
        break;

    case A_CPU0INTR_STAT:
    case A_CPU1INTR_STAT:
    case A_PID4 ... A_CID3:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "SSE MHU: write to read-only register at offset 0x%x\n",
                      (int)offset);
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "SSE MHU write: bad offset 0x%x\n", (int)offset);
        break;
    }

    armsse_mhu_update(s);
}

static const MemoryRegionOps armsse_mhu_ops = {
    .read = armsse_mhu_read,
    .write = armsse_mhu_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};

static void armsse_mhu_reset(DeviceState *dev)
{
    ARMSSEMHU *s = ARMSSE_MHU(dev);

    s->cpu0intr = 0;
    s->cpu1intr = 0;
}

static const VMStateDescription armsse_mhu_vmstate = {
    .name = "armsse-mhu",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(cpu0intr, ARMSSEMHU),
        VMSTATE_UINT32(cpu1intr, ARMSSEMHU),
        VMSTATE_END_OF_LIST()
    },
};

static void armsse_mhu_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    ARMSSEMHU *s = ARMSSE_MHU(obj);

    memory_region_init_io(&s->iomem, obj, &armsse_mhu_ops,
                          s, "armsse-mhu", 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->cpu0irq);
    sysbus_init_irq(sbd, &s->cpu1irq);
}

static void armsse_mhu_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, armsse_mhu_reset);
    dc->vmsd = &armsse_mhu_vmstate;
}

static const TypeInfo armsse_mhu_info = {
    .name = TYPE_ARMSSE_MHU,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(ARMSSEMHU),
    .instance_init = armsse_mhu_init,
    .class_init = armsse_mhu_class_init,
};

static void armsse_mhu_register_types(void)
{
    type_register_static(&armsse_mhu_info);
}

type_init(armsse_mhu_register_types);
