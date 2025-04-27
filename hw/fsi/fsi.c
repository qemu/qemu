/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2024 IBM Corp.
 *
 * IBM Flexible Service Interface
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "trace.h"

#include "hw/fsi/fsi.h"

#define TO_REG(x)                               ((x) >> 2)

static const TypeInfo fsi_bus_info = {
    .name = TYPE_FSI_BUS,
    .parent = TYPE_BUS,
    .instance_size = sizeof(FSIBus),
};

static uint64_t fsi_slave_read(void *opaque, hwaddr addr, unsigned size)
{
    FSISlaveState *s = FSI_SLAVE(opaque);
    int reg = TO_REG(addr);

    trace_fsi_slave_read(addr, size);

    if (reg >= FSI_SLAVE_CONTROL_NR_REGS) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Out of bounds read: 0x%"HWADDR_PRIx" for %u\n",
                      __func__, addr, size);
        return 0;
    }

    return s->regs[reg];
}

static void fsi_slave_write(void *opaque, hwaddr addr, uint64_t data,
                                 unsigned size)
{
    FSISlaveState *s = FSI_SLAVE(opaque);
    int reg = TO_REG(addr);

    trace_fsi_slave_write(addr, size, data);

    if (reg >= FSI_SLAVE_CONTROL_NR_REGS) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Out of bounds write: 0x%"HWADDR_PRIx" for %u\n",
                      __func__, addr, size);
        return;
    }

    s->regs[reg] = data;
}

static const struct MemoryRegionOps fsi_slave_ops = {
    .read = fsi_slave_read,
    .write = fsi_slave_write,
    .endianness = DEVICE_BIG_ENDIAN,
};

static void fsi_slave_reset(DeviceState *dev)
{
    FSISlaveState *s = FSI_SLAVE(dev);

    /* Initialize registers */
    memset(s->regs, 0, sizeof(s->regs));
}

static void fsi_slave_init(Object *o)
{
    FSISlaveState *s = FSI_SLAVE(o);

    memory_region_init_io(&s->iomem, OBJECT(s), &fsi_slave_ops,
                          s, TYPE_FSI_SLAVE, 0x400);
}

static void fsi_slave_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->bus_type = TYPE_FSI_BUS;
    dc->desc = "FSI Slave";
    device_class_set_legacy_reset(dc, fsi_slave_reset);
}

static const TypeInfo fsi_slave_info = {
    .name = TYPE_FSI_SLAVE,
    .parent = TYPE_DEVICE,
    .instance_init = fsi_slave_init,
    .instance_size = sizeof(FSISlaveState),
    .class_init = fsi_slave_class_init,
};

static void fsi_register_types(void)
{
    type_register_static(&fsi_bus_info);
    type_register_static(&fsi_slave_info);
}

type_init(fsi_register_types);
