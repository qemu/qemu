/*
 * ASPEED I3C Controller
 *
 * Copyright (C) 2021 ASPEED Technology Inc.
 *
 * This code is licensed under the GPL version 2 or later.  See
 * the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "hw/misc/aspeed_i3c.h"
#include "hw/registerfields.h"
#include "hw/qdev-properties.h"
#include "qapi/error.h"
#include "migration/vmstate.h"
#include "trace.h"

/* I3C Controller Registers */
REG32(I3C1_REG0, 0x10)
REG32(I3C1_REG1, 0x14)
    FIELD(I3C1_REG1, I2C_MODE,  0,  1)
    FIELD(I3C1_REG1, SA_EN,     15, 1)
REG32(I3C2_REG0, 0x20)
REG32(I3C2_REG1, 0x24)
    FIELD(I3C2_REG1, I2C_MODE,  0,  1)
    FIELD(I3C2_REG1, SA_EN,     15, 1)
REG32(I3C3_REG0, 0x30)
REG32(I3C3_REG1, 0x34)
    FIELD(I3C3_REG1, I2C_MODE,  0,  1)
    FIELD(I3C3_REG1, SA_EN,     15, 1)
REG32(I3C4_REG0, 0x40)
REG32(I3C4_REG1, 0x44)
    FIELD(I3C4_REG1, I2C_MODE,  0,  1)
    FIELD(I3C4_REG1, SA_EN,     15, 1)
REG32(I3C5_REG0, 0x50)
REG32(I3C5_REG1, 0x54)
    FIELD(I3C5_REG1, I2C_MODE,  0,  1)
    FIELD(I3C5_REG1, SA_EN,     15, 1)
REG32(I3C6_REG0, 0x60)
REG32(I3C6_REG1, 0x64)
    FIELD(I3C6_REG1, I2C_MODE,  0,  1)
    FIELD(I3C6_REG1, SA_EN,     15, 1)

/* I3C Device Registers */
REG32(DEVICE_CTRL,                  0x00)
REG32(DEVICE_ADDR,                  0x04)
REG32(HW_CAPABILITY,                0x08)
REG32(COMMAND_QUEUE_PORT,           0x0c)
REG32(RESPONSE_QUEUE_PORT,          0x10)
REG32(RX_TX_DATA_PORT,              0x14)
REG32(IBI_QUEUE_STATUS,             0x18)
REG32(IBI_QUEUE_DATA,               0x18)
REG32(QUEUE_THLD_CTRL,              0x1c)
REG32(DATA_BUFFER_THLD_CTRL,        0x20)
REG32(IBI_QUEUE_CTRL,               0x24)
REG32(IBI_MR_REQ_REJECT,            0x2c)
REG32(IBI_SIR_REQ_REJECT,           0x30)
REG32(RESET_CTRL,                   0x34)
REG32(SLV_EVENT_CTRL,               0x38)
REG32(INTR_STATUS,                  0x3c)
REG32(INTR_STATUS_EN,               0x40)
REG32(INTR_SIGNAL_EN,               0x44)
REG32(INTR_FORCE,                   0x48)
REG32(QUEUE_STATUS_LEVEL,           0x4c)
REG32(DATA_BUFFER_STATUS_LEVEL,     0x50)
REG32(PRESENT_STATE,                0x54)
REG32(CCC_DEVICE_STATUS,            0x58)
REG32(DEVICE_ADDR_TABLE_POINTER,    0x5c)
    FIELD(DEVICE_ADDR_TABLE_POINTER, DEPTH, 16, 16)
    FIELD(DEVICE_ADDR_TABLE_POINTER, ADDR,  0,  16)
REG32(DEV_CHAR_TABLE_POINTER,       0x60)
REG32(VENDOR_SPECIFIC_REG_POINTER,  0x6c)
REG32(SLV_MIPI_PID_VALUE,           0x70)
REG32(SLV_PID_VALUE,                0x74)
REG32(SLV_CHAR_CTRL,                0x78)
REG32(SLV_MAX_LEN,                  0x7c)
REG32(MAX_READ_TURNAROUND,          0x80)
REG32(MAX_DATA_SPEED,               0x84)
REG32(SLV_DEBUG_STATUS,             0x88)
REG32(SLV_INTR_REQ,                 0x8c)
REG32(DEVICE_CTRL_EXTENDED,         0xb0)
REG32(SCL_I3C_OD_TIMING,            0xb4)
REG32(SCL_I3C_PP_TIMING,            0xb8)
REG32(SCL_I2C_FM_TIMING,            0xbc)
REG32(SCL_I2C_FMP_TIMING,           0xc0)
REG32(SCL_EXT_LCNT_TIMING,          0xc8)
REG32(SCL_EXT_TERMN_LCNT_TIMING,    0xcc)
REG32(BUS_FREE_TIMING,              0xd4)
REG32(BUS_IDLE_TIMING,              0xd8)
REG32(I3C_VER_ID,                   0xe0)
REG32(I3C_VER_TYPE,                 0xe4)
REG32(EXTENDED_CAPABILITY,          0xe8)
REG32(SLAVE_CONFIG,                 0xec)

static const uint32_t ast2600_i3c_device_resets[ASPEED_I3C_DEVICE_NR_REGS] = {
    [R_HW_CAPABILITY]               = 0x000e00bf,
    [R_QUEUE_THLD_CTRL]             = 0x01000101,
    [R_I3C_VER_ID]                  = 0x3130302a,
    [R_I3C_VER_TYPE]                = 0x6c633033,
    [R_DEVICE_ADDR_TABLE_POINTER]   = 0x00080280,
    [R_DEV_CHAR_TABLE_POINTER]      = 0x00020200,
    [A_VENDOR_SPECIFIC_REG_POINTER] = 0x000000b0,
    [R_SLV_MAX_LEN]                 = 0x00ff00ff,
};

static uint64_t aspeed_i3c_device_read(void *opaque, hwaddr offset,
                                       unsigned size)
{
    AspeedI3CDevice *s = ASPEED_I3C_DEVICE(opaque);
    uint32_t addr = offset >> 2;
    uint64_t value;

    switch (addr) {
    case R_COMMAND_QUEUE_PORT:
        value = 0;
        break;
    default:
        value = s->regs[addr];
        break;
    }

    trace_aspeed_i3c_device_read(s->id, offset, value);

    return value;
}

static void aspeed_i3c_device_write(void *opaque, hwaddr offset,
                                    uint64_t value, unsigned size)
{
    AspeedI3CDevice *s = ASPEED_I3C_DEVICE(opaque);
    uint32_t addr = offset >> 2;

    trace_aspeed_i3c_device_write(s->id, offset, value);

    switch (addr) {
    case R_HW_CAPABILITY:
    case R_RESPONSE_QUEUE_PORT:
    case R_IBI_QUEUE_DATA:
    case R_QUEUE_STATUS_LEVEL:
    case R_PRESENT_STATE:
    case R_CCC_DEVICE_STATUS:
    case R_DEVICE_ADDR_TABLE_POINTER:
    case R_VENDOR_SPECIFIC_REG_POINTER:
    case R_SLV_CHAR_CTRL:
    case R_SLV_MAX_LEN:
    case R_MAX_READ_TURNAROUND:
    case R_I3C_VER_ID:
    case R_I3C_VER_TYPE:
    case R_EXTENDED_CAPABILITY:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: write to readonly register[0x%02" HWADDR_PRIx
                      "] = 0x%08" PRIx64 "\n",
                      __func__, offset, value);
        break;
    case R_RX_TX_DATA_PORT:
        break;
    case R_RESET_CTRL:
        break;
    default:
        s->regs[addr] = value;
        break;
    }
}

static const VMStateDescription aspeed_i3c_device_vmstate = {
    .name = TYPE_ASPEED_I3C,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]){
        VMSTATE_UINT32_ARRAY(regs, AspeedI3CDevice, ASPEED_I3C_DEVICE_NR_REGS),
        VMSTATE_END_OF_LIST(),
    }
};

static const MemoryRegionOps aspeed_i3c_device_ops = {
    .read = aspeed_i3c_device_read,
    .write = aspeed_i3c_device_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void aspeed_i3c_device_reset(DeviceState *dev)
{
    AspeedI3CDevice *s = ASPEED_I3C_DEVICE(dev);

    memcpy(s->regs, ast2600_i3c_device_resets, sizeof(s->regs));
}

static void aspeed_i3c_device_realize(DeviceState *dev, Error **errp)
{
    AspeedI3CDevice *s = ASPEED_I3C_DEVICE(dev);
    g_autofree char *name = g_strdup_printf(TYPE_ASPEED_I3C_DEVICE ".%d",
                                            s->id);

    sysbus_init_irq(SYS_BUS_DEVICE(dev), &s->irq);

    memory_region_init_io(&s->mr, OBJECT(s), &aspeed_i3c_device_ops,
                          s, name, ASPEED_I3C_DEVICE_NR_REGS << 2);
}

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
                TYPE_ASPEED_I3C_DEVICE);
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

static Property aspeed_i3c_device_properties[] = {
    DEFINE_PROP_UINT8("device-id", AspeedI3CDevice, id, 0),
    DEFINE_PROP_END_OF_LIST(),
};

static void aspeed_i3c_device_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "Aspeed I3C Device";
    dc->realize = aspeed_i3c_device_realize;
    dc->reset = aspeed_i3c_device_reset;
    device_class_set_props(dc, aspeed_i3c_device_properties);
}

static const TypeInfo aspeed_i3c_device_info = {
    .name = TYPE_ASPEED_I3C_DEVICE,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AspeedI3CDevice),
    .class_init = aspeed_i3c_device_class_init,
};

static const VMStateDescription vmstate_aspeed_i3c = {
    .name = TYPE_ASPEED_I3C,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, AspeedI3CState, ASPEED_I3C_NR_REGS),
        VMSTATE_STRUCT_ARRAY(devices, AspeedI3CState, ASPEED_I3C_NR_DEVICES, 1,
                             aspeed_i3c_device_vmstate, AspeedI3CDevice),
        VMSTATE_END_OF_LIST(),
    }
};

static void aspeed_i3c_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = aspeed_i3c_realize;
    dc->reset = aspeed_i3c_reset;
    dc->desc = "Aspeed I3C Controller";
    dc->vmsd = &vmstate_aspeed_i3c;
}

static const TypeInfo aspeed_i3c_info = {
    .name = TYPE_ASPEED_I3C,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_init = aspeed_i3c_instance_init,
    .instance_size = sizeof(AspeedI3CState),
    .class_init = aspeed_i3c_class_init,
};

static void aspeed_i3c_register_types(void)
{
    type_register_static(&aspeed_i3c_device_info);
    type_register_static(&aspeed_i3c_info);
}

type_init(aspeed_i3c_register_types);
