/*
 * ASPEED I3C Controller
 *
 * Copyright (C) 2021 ASPEED Technology Inc.
 * Copyright (C) 2025 Google, LLC.
 *
 * This code is licensed under the GPL version 2 or later.  See
 * the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "hw/i3c/aspeed_i3c.h"
#include "hw/core/registerfields.h"
#include "hw/core/qdev-properties.h"
#include "qapi/error.h"
#include "migration/vmstate.h"
#include "trace.h"

/* I3C Controller Registers */
REG32(I3C1_REG0, 0x10)
REG32(I3C1_REG1, 0x14)
    FIELD(I3C1_REG1, I2C_MODE,      0,  1)
    FIELD(I3C1_REG1, SLV_TEST_MODE, 1,  1)
    FIELD(I3C1_REG1, ACT_MODE,      2,  2)
    FIELD(I3C1_REG1, PENDING_INT,   4,  4)
    FIELD(I3C1_REG1, SA,            8,  7)
    FIELD(I3C1_REG1, SA_EN,         15, 1)
    FIELD(I3C1_REG1, INST_ID,       16, 4)
REG32(I3C2_REG0, 0x20)
REG32(I3C2_REG1, 0x24)
    FIELD(I3C2_REG1, I2C_MODE,      0,  1)
    FIELD(I3C2_REG1, SLV_TEST_MODE, 1,  1)
    FIELD(I3C2_REG1, ACT_MODE,      2,  2)
    FIELD(I3C2_REG1, PENDING_INT,   4,  4)
    FIELD(I3C2_REG1, SA,            8,  7)
    FIELD(I3C2_REG1, SA_EN,         15, 1)
    FIELD(I3C2_REG1, INST_ID,       16, 4)
REG32(I3C3_REG0, 0x30)
REG32(I3C3_REG1, 0x34)
    FIELD(I3C3_REG1, I2C_MODE,      0,  1)
    FIELD(I3C3_REG1, SLV_TEST_MODE, 1,  1)
    FIELD(I3C3_REG1, ACT_MODE,      2,  2)
    FIELD(I3C3_REG1, PENDING_INT,   4,  4)
    FIELD(I3C3_REG1, SA,            8,  7)
    FIELD(I3C3_REG1, SA_EN,         15, 1)
    FIELD(I3C3_REG1, INST_ID,       16, 4)
REG32(I3C4_REG0, 0x40)
REG32(I3C4_REG1, 0x44)
    FIELD(I3C4_REG1, I2C_MODE,      0,  1)
    FIELD(I3C4_REG1, SLV_TEST_MODE, 1,  1)
    FIELD(I3C4_REG1, ACT_MODE,      2,  2)
    FIELD(I3C4_REG1, PENDING_INT,   4,  4)
    FIELD(I3C4_REG1, SA,            8,  7)
    FIELD(I3C4_REG1, SA_EN,         15, 1)
    FIELD(I3C4_REG1, INST_ID,       16, 4)
REG32(I3C5_REG0, 0x50)
REG32(I3C5_REG1, 0x54)
    FIELD(I3C5_REG1, I2C_MODE,      0,  1)
    FIELD(I3C5_REG1, SLV_TEST_MODE, 1,  1)
    FIELD(I3C5_REG1, ACT_MODE,      2,  2)
    FIELD(I3C5_REG1, PENDING_INT,   4,  4)
    FIELD(I3C5_REG1, SA,            8,  7)
    FIELD(I3C5_REG1, SA_EN,         15, 1)
    FIELD(I3C5_REG1, INST_ID,       16, 4)
REG32(I3C6_REG0, 0x60)
REG32(I3C6_REG1, 0x64)
    FIELD(I3C6_REG1, I2C_MODE,      0,  1)
    FIELD(I3C6_REG1, SLV_TEST_MODE, 1,  1)
    FIELD(I3C6_REG1, ACT_MODE,      2,  2)
    FIELD(I3C6_REG1, PENDING_INT,   4,  4)
    FIELD(I3C6_REG1, SA,            8,  7)
    FIELD(I3C6_REG1, SA_EN,         15, 1)
    FIELD(I3C6_REG1, INST_ID,       16, 4)

static const uint32_t ast2600_i3c_controller_ro[ASPEED_I3C_NR_REGS] = {
    [R_I3C1_REG0]                   = 0xcc000000,
    [R_I3C1_REG1]                   = 0xfff00000,
    [R_I3C2_REG0]                   = 0xcc000000,
    [R_I3C2_REG1]                   = 0xfff00000,
    [R_I3C3_REG0]                   = 0xcc000000,
    [R_I3C3_REG1]                   = 0xfff00000,
    [R_I3C4_REG0]                   = 0xcc000000,
    [R_I3C4_REG1]                   = 0xfff00000,
    [R_I3C5_REG0]                   = 0xcc000000,
    [R_I3C5_REG1]                   = 0xfff00000,
    [R_I3C6_REG0]                   = 0xcc000000,
    [R_I3C6_REG1]                   = 0xfff00000,
};

static uint64_t aspeed_i3c_read(void *opaque, hwaddr addr, unsigned int size)
{
    AspeedI3CState *s = ASPEED_I3C(opaque);
    uint64_t val = 0;

    val = s->regs[addr >> 2];

    trace_aspeed_i3c_read(addr, val);

    return val;
}

static void aspeed_i3c_write(void *opaque,
                             hwaddr addr,
                             uint64_t data,
                             unsigned int size)
{
    AspeedI3CState *s = ASPEED_I3C(opaque);

    trace_aspeed_i3c_write(addr, data);

    addr >>= 2;

    data &= ~ast2600_i3c_controller_ro[addr];
    /* I3C controller register */
    switch (addr) {
    case R_I3C1_REG1:
    case R_I3C2_REG1:
    case R_I3C3_REG1:
    case R_I3C4_REG1:
    case R_I3C5_REG1:
    case R_I3C6_REG1:
        if (data & R_I3C1_REG1_I2C_MODE_MASK) {
            qemu_log_mask(LOG_UNIMP,
                          "%s: Unsupported I2C mode [0x%08" HWADDR_PRIx
                          "]=%08" PRIx64 "\n",
                          __func__, addr << 2, data);
            break;
        }
        if (data & R_I3C1_REG1_SA_EN_MASK) {
            qemu_log_mask(LOG_UNIMP,
                          "%s: Unsupported slave mode [%08" HWADDR_PRIx
                          "]=0x%08" PRIx64 "\n",
                          __func__, addr << 2, data);
            break;
        }
        s->regs[addr] = data;
        break;
    default:
        s->regs[addr] = data;
        break;
    }
}

static const MemoryRegionOps aspeed_i3c_ops = {
    .read = aspeed_i3c_read,
    .write = aspeed_i3c_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    }
};

static void aspeed_i3c_reset(DeviceState *dev)
{
    AspeedI3CState *s = ASPEED_I3C(dev);
    memset(s->regs, 0, sizeof(s->regs));
}

static void aspeed_i3c_instance_init(Object *obj)
{
    AspeedI3CState *s = ASPEED_I3C(obj);
    int i;

    for (i = 0; i < ASPEED_I3C_NR_DEVICES; ++i) {
        object_initialize_child(obj, "device[*]", &s->devices[i],
                TYPE_DW_I3C);
    }
}

static void aspeed_i3c_realize(DeviceState *dev, Error **errp)
{
    int i;
    AspeedI3CState *s = ASPEED_I3C(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    memory_region_init(&s->iomem_container, OBJECT(s),
            TYPE_ASPEED_I3C ".container", 0x8000);

    sysbus_init_mmio(sbd, &s->iomem_container);

    memory_region_init_io(&s->iomem, OBJECT(s), &aspeed_i3c_ops, s,
            TYPE_ASPEED_I3C ".regs", ASPEED_I3C_NR_REGS << 2);

    memory_region_add_subregion(&s->iomem_container, 0x0, &s->iomem);

    for (i = 0; i < ASPEED_I3C_NR_DEVICES; ++i) {
        Object *i3c_dev = OBJECT(&s->devices[i]);

        if (!object_property_set_uint(i3c_dev, "device-id", i, errp)) {
            return;
        }

        if (!sysbus_realize(SYS_BUS_DEVICE(i3c_dev), errp)) {
            return;
        }

        /*
         * Register Address of I3CX Device =
         *     (Base Address of Global Register) + (Offset of I3CX) + Offset
         * X = 0, 1, 2, 3, 4, 5
         * Offset of I3C0 = 0x2000
         * Offset of I3C1 = 0x3000
         * Offset of I3C2 = 0x4000
         * Offset of I3C3 = 0x5000
         * Offset of I3C4 = 0x6000
         * Offset of I3C5 = 0x7000
         */
        memory_region_add_subregion(&s->iomem_container,
                0x2000 + i * 0x1000, &s->devices[i].mr);
    }

}

static const VMStateDescription vmstate_aspeed_i3c = {
    .name = TYPE_ASPEED_I3C,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, AspeedI3CState, ASPEED_I3C_NR_REGS),
        VMSTATE_STRUCT_ARRAY(devices, AspeedI3CState, ASPEED_I3C_NR_DEVICES, 1,
                             vmstate_dw_i3c, DWI3C),
        VMSTATE_END_OF_LIST(),
    }
};

static void aspeed_i3c_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = aspeed_i3c_realize;
    device_class_set_legacy_reset(dc, aspeed_i3c_reset);
    dc->desc = "Aspeed I3C Controller";
    dc->vmsd = &vmstate_aspeed_i3c;
}

static const TypeInfo aspeed_i3c_types[] = {
    {
        .name = TYPE_ASPEED_I3C,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_init = aspeed_i3c_instance_init,
        .instance_size = sizeof(AspeedI3CState),
        .class_init = aspeed_i3c_class_init,
    },
};

DEFINE_TYPES(aspeed_i3c_types)
