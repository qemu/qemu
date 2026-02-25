/*
 * DesignWare I3C Controller
 *
 * Copyright (C) 2021 ASPEED Technology Inc.
 * Copyright (C) 2025 Google, LLC
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "hw/i3c/i3c.h"
#include "hw/i3c/dw-i3c.h"
#include "hw/core/registerfields.h"
#include "hw/core/qdev-properties.h"
#include "qapi/error.h"
#include "migration/vmstate.h"
#include "trace.h"

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

static const uint32_t dw_i3c_resets[DW_I3C_NR_REGS] = {
    [R_HW_CAPABILITY]               = 0x000e00bf,
    [R_QUEUE_THLD_CTRL]             = 0x01000101,
    [R_I3C_VER_ID]                  = 0x3130302a,
    [R_I3C_VER_TYPE]                = 0x6c633033,
    [R_DEVICE_ADDR_TABLE_POINTER]   = 0x00080280,
    [R_DEV_CHAR_TABLE_POINTER]      = 0x00020200,
    [A_VENDOR_SPECIFIC_REG_POINTER] = 0x000000b0,
    [R_SLV_MAX_LEN]                 = 0x00ff00ff,
};

static uint64_t dw_i3c_read(void *opaque, hwaddr offset, unsigned size)
{
    DWI3C *s = DW_I3C(opaque);
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

    trace_dw_i3c_read(s->id, offset, value);

    return value;
}

static void dw_i3c_write(void *opaque, hwaddr offset, uint64_t value,
                         unsigned size)
{
    DWI3C *s = DW_I3C(opaque);
    uint32_t addr = offset >> 2;

    trace_dw_i3c_write(s->id, offset, value);

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

const VMStateDescription vmstate_dw_i3c = {
    .name = TYPE_DW_I3C,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]){
        VMSTATE_UINT32_ARRAY(regs, DWI3C, DW_I3C_NR_REGS),
        VMSTATE_END_OF_LIST(),
    }
};

static const MemoryRegionOps dw_i3c_ops = {
    .read = dw_i3c_read,
    .write = dw_i3c_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void dw_i3c_reset_enter(Object *obj, ResetType type)
{
    DWI3C *s = DW_I3C(obj);

    memcpy(s->regs, dw_i3c_resets, sizeof(s->regs));
}

static void dw_i3c_realize(DeviceState *dev, Error **errp)
{
    DWI3C *s = DW_I3C(dev);
    g_autofree char *name = g_strdup_printf(TYPE_DW_I3C ".%d", s->id);

    sysbus_init_irq(SYS_BUS_DEVICE(dev), &s->irq);

    memory_region_init_io(&s->mr, OBJECT(s), &dw_i3c_ops, s, name,
                          DW_I3C_NR_REGS << 2);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->mr);
}

static const Property dw_i3c_properties[] = {
    DEFINE_PROP_UINT8("device-id", DWI3C, id, 0),
};

static void dw_i3c_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.enter = dw_i3c_reset_enter;

    dc->desc = "DesignWare I3C Controller";
    dc->realize = dw_i3c_realize;
    dc->vmsd = &vmstate_dw_i3c;
    device_class_set_props(dc, dw_i3c_properties);
}

static const TypeInfo dw_i3c_types[] = {
    {
        .name = TYPE_DW_I3C,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(DWI3C),
        .class_init = dw_i3c_class_init,
    },
};

DEFINE_TYPES(dw_i3c_types)

