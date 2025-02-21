/*
 * CXL Utility library for devices
 *
 * Copyright(C) 2020 Intel Corporation.
 *
 * This work is licensed under the terms of the GNU GPL, version 2. See the
 * COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/cxl/cxl.h"

/*
 * Device registers have no restrictions per the spec, and so fall back to the
 * default memory mapped register rules in CXL r3.1 Section 8.2:
 *   Software shall use CXL.io Memory Read and Write to access memory mapped
 *   register defined in this section. Unless otherwise specified, software
 *   shall restrict the accesses width based on the following:
 *   • A 32 bit register shall be accessed as a 1 Byte, 2 Bytes or 4 Bytes
 *     quantity.
 *   • A 64 bit register shall be accessed as a 1 Byte, 2 Bytes, 4 Bytes or 8
 *     Bytes
 *   • The address shall be a multiple of the access width, e.g. when
 *     accessing a register as a 4 Byte quantity, the address shall be
 *     multiple of 4.
 *   • The accesses shall map to contiguous bytes.If these rules are not
 *     followed, the behavior is undefined
 */

static uint64_t caps_reg_read(void *opaque, hwaddr offset, unsigned size)
{
    CXLDeviceState *cxl_dstate = opaque;

    switch (size) {
    case 4:
        return cxl_dstate->caps_reg_state32[offset / size];
    case 8:
        return cxl_dstate->caps_reg_state64[offset / size];
    default:
        g_assert_not_reached();
    }
}

static uint64_t dev_reg_read(void *opaque, hwaddr offset, unsigned size)
{
    CXLDeviceState *cxl_dstate = opaque;

    switch (size) {
    case 1:
        return cxl_dstate->dev_reg_state[offset];
    case 2:
        return cxl_dstate->dev_reg_state16[offset / size];
    case 4:
        return cxl_dstate->dev_reg_state32[offset / size];
    case 8:
        return cxl_dstate->dev_reg_state64[offset / size];
    default:
        g_assert_not_reached();
    }
}

static uint64_t mailbox_reg_read(void *opaque, hwaddr offset, unsigned size)
{
    CXLDeviceState *cxl_dstate;
    CXLCCI *cci = opaque;

    if (object_dynamic_cast(OBJECT(cci->intf), TYPE_CXL_TYPE3)) {
        cxl_dstate = &CXL_TYPE3(cci->intf)->cxl_dstate;
    } else if (object_dynamic_cast(OBJECT(cci->intf),
                                   TYPE_CXL_SWITCH_MAILBOX_CCI)) {
        cxl_dstate = &CXL_SWITCH_MAILBOX_CCI(cci->intf)->cxl_dstate;
    } else {
        return 0;
    }

    switch (size) {
    case 1:
        return cxl_dstate->mbox_reg_state[offset];
    case 2:
        return cxl_dstate->mbox_reg_state16[offset / size];
    case 4:
        return cxl_dstate->mbox_reg_state32[offset / size];
    case 8:
        if (offset == A_CXL_DEV_BG_CMD_STS) {
            uint64_t bg_status_reg;
            bg_status_reg = FIELD_DP64(0, CXL_DEV_BG_CMD_STS, OP,
                                       cci->bg.opcode);
            bg_status_reg = FIELD_DP64(bg_status_reg, CXL_DEV_BG_CMD_STS,
                                       PERCENTAGE_COMP, cci->bg.complete_pct);
            bg_status_reg = FIELD_DP64(bg_status_reg, CXL_DEV_BG_CMD_STS,
                                       RET_CODE, cci->bg.ret_code);
            /* endian? */
            cxl_dstate->mbox_reg_state64[offset / size] = bg_status_reg;
        }
        if (offset == A_CXL_DEV_MAILBOX_STS) {
            uint64_t status_reg = cxl_dstate->mbox_reg_state64[offset / size];
            if (cci->bg.complete_pct) {
                status_reg = FIELD_DP64(status_reg, CXL_DEV_MAILBOX_STS, BG_OP,
                                        0);
                cxl_dstate->mbox_reg_state64[offset / size] = status_reg;
            }
        }
        return cxl_dstate->mbox_reg_state64[offset / size];
    default:
        g_assert_not_reached();
    }
}

static void mailbox_mem_writel(uint32_t *reg_state, hwaddr offset,
                               uint64_t value)
{
    switch (offset) {
    case A_CXL_DEV_MAILBOX_CTRL:
        /* fallthrough */
    case A_CXL_DEV_MAILBOX_CAP:
        /* RO register */
        break;
    default:
        qemu_log_mask(LOG_UNIMP,
                      "%s Unexpected 32-bit access to 0x%" PRIx64 " (WI)\n",
                      __func__, offset);
        return;
    }

    reg_state[offset / sizeof(*reg_state)] = value;
}

static void mailbox_mem_writeq(uint64_t *reg_state, hwaddr offset,
                               uint64_t value)
{
    switch (offset) {
    case A_CXL_DEV_MAILBOX_CMD:
        break;
    case A_CXL_DEV_BG_CMD_STS:
        break;
    case A_CXL_DEV_MAILBOX_STS:
        /* Read only register, will get updated by the state machine */
        return;
    default:
        qemu_log_mask(LOG_UNIMP,
                      "%s Unexpected 64-bit access to 0x%" PRIx64 " (WI)\n",
                      __func__, offset);
        return;
    }


    reg_state[offset / sizeof(*reg_state)] = value;
}

static void mailbox_reg_write(void *opaque, hwaddr offset, uint64_t value,
                              unsigned size)
{
    CXLDeviceState *cxl_dstate;
    CXLCCI *cci = opaque;

    if (object_dynamic_cast(OBJECT(cci->intf), TYPE_CXL_TYPE3)) {
        cxl_dstate = &CXL_TYPE3(cci->intf)->cxl_dstate;
    } else if (object_dynamic_cast(OBJECT(cci->intf),
                                   TYPE_CXL_SWITCH_MAILBOX_CCI)) {
        cxl_dstate = &CXL_SWITCH_MAILBOX_CCI(cci->intf)->cxl_dstate;
    } else {
        return;
    }

    if (offset >= A_CXL_DEV_CMD_PAYLOAD) {
        memcpy(cxl_dstate->mbox_reg_state + offset, &value, size);
        return;
    }

    switch (size) {
    case 4:
        mailbox_mem_writel(cxl_dstate->mbox_reg_state32, offset, value);
        break;
    case 8:
        mailbox_mem_writeq(cxl_dstate->mbox_reg_state64, offset, value);
        break;
    default:
        g_assert_not_reached();
    }

    if (ARRAY_FIELD_EX32(cxl_dstate->mbox_reg_state32, CXL_DEV_MAILBOX_CTRL,
                         DOORBELL)) {
        uint64_t command_reg =
            cxl_dstate->mbox_reg_state64[R_CXL_DEV_MAILBOX_CMD];
        uint8_t cmd_set = FIELD_EX64(command_reg, CXL_DEV_MAILBOX_CMD,
                                     COMMAND_SET);
        uint8_t cmd = FIELD_EX64(command_reg, CXL_DEV_MAILBOX_CMD, COMMAND);
        size_t len_in = FIELD_EX64(command_reg, CXL_DEV_MAILBOX_CMD, LENGTH);
        uint8_t *pl = cxl_dstate->mbox_reg_state + A_CXL_DEV_CMD_PAYLOAD;
        /*
         * Copy taken to avoid need for individual command handlers to care
         * about aliasing.
         */
        g_autofree uint8_t *pl_in_copy = NULL;
        size_t len_out = 0;
        uint64_t status_reg;
        bool bg_started = false;
        int rc;

        pl_in_copy = g_memdup2(pl, len_in);
        if (len_in == 0 || pl_in_copy) {
            /* Avoid stale data  - including from earlier cmds */
            memset(pl, 0, CXL_MAILBOX_MAX_PAYLOAD_SIZE);
            rc = cxl_process_cci_message(cci, cmd_set, cmd, len_in, pl_in_copy,
                                         &len_out, pl, &bg_started);
        } else {
            rc = CXL_MBOX_INTERNAL_ERROR;
        }

        /* Set bg and the return code */
        status_reg = FIELD_DP64(0, CXL_DEV_MAILBOX_STS, BG_OP,
                                bg_started ? 1 : 0);
        status_reg = FIELD_DP64(status_reg, CXL_DEV_MAILBOX_STS, ERRNO, rc);
        /* Set the return length */
        command_reg = FIELD_DP64(0, CXL_DEV_MAILBOX_CMD, COMMAND_SET, cmd_set);
        command_reg = FIELD_DP64(command_reg, CXL_DEV_MAILBOX_CMD,
                                 COMMAND, cmd);
        command_reg = FIELD_DP64(command_reg, CXL_DEV_MAILBOX_CMD,
                                 LENGTH, len_out);

        cxl_dstate->mbox_reg_state64[R_CXL_DEV_MAILBOX_CMD] = command_reg;
        cxl_dstate->mbox_reg_state64[R_CXL_DEV_MAILBOX_STS] = status_reg;
        /* Tell the host we're done */
        ARRAY_FIELD_DP32(cxl_dstate->mbox_reg_state32, CXL_DEV_MAILBOX_CTRL,
                         DOORBELL, 0);
    }
}

static uint64_t mdev_reg_read(void *opaque, hwaddr offset, unsigned size)
{
    CXLDeviceState *cxl_dstate = opaque;

    return cxl_dstate->memdev_status;
}

static void ro_reg_write(void *opaque, hwaddr offset, uint64_t value,
                           unsigned size)
{
    /* Many register sets are read only */
}

static const MemoryRegionOps mdev_ops = {
    .read = mdev_reg_read,
    .write = ro_reg_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 8,
        .unaligned = false,
    },
    .impl = {
        .min_access_size = 8,
        .max_access_size = 8,
    },
};

static const MemoryRegionOps mailbox_ops = {
    .read = mailbox_reg_read,
    .write = mailbox_reg_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 8,
        .unaligned = false,
    },
    .impl = {
        .min_access_size = 1,
        .max_access_size = 8,
    },
};

static const MemoryRegionOps dev_ops = {
    .read = dev_reg_read,
    .write = ro_reg_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 8,
        .unaligned = false,
    },
    .impl = {
        .min_access_size = 1,
        .max_access_size = 8,
    },
};

static const MemoryRegionOps caps_ops = {
    .read = caps_reg_read,
    .write = ro_reg_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 8,
        .unaligned = false,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
};

void cxl_device_register_block_init(Object *obj, CXLDeviceState *cxl_dstate,
                                    CXLCCI *cci)
{
    /* This will be a BAR, so needs to be rounded up to pow2 for PCI spec */
    memory_region_init(&cxl_dstate->device_registers, obj, "device-registers",
                       pow2ceil(CXL_MMIO_SIZE));

    memory_region_init_io(&cxl_dstate->caps, obj, &caps_ops, cxl_dstate,
                          "cap-array", CXL_CAPS_SIZE);
    memory_region_init_io(&cxl_dstate->device, obj, &dev_ops, cxl_dstate,
                          "device-status", CXL_DEVICE_STATUS_REGISTERS_LENGTH);
    memory_region_init_io(&cxl_dstate->mailbox, obj, &mailbox_ops, cci,
                          "mailbox", CXL_MAILBOX_REGISTERS_LENGTH);
    memory_region_init_io(&cxl_dstate->memory_device, obj, &mdev_ops,
                          cxl_dstate, "memory device caps",
                          CXL_MEMORY_DEVICE_REGISTERS_LENGTH);

    memory_region_add_subregion(&cxl_dstate->device_registers, 0,
                                &cxl_dstate->caps);
    memory_region_add_subregion(&cxl_dstate->device_registers,
                                CXL_DEVICE_STATUS_REGISTERS_OFFSET,
                                &cxl_dstate->device);
    memory_region_add_subregion(&cxl_dstate->device_registers,
                                CXL_MAILBOX_REGISTERS_OFFSET,
                                &cxl_dstate->mailbox);
    memory_region_add_subregion(&cxl_dstate->device_registers,
                                CXL_MEMORY_DEVICE_REGISTERS_OFFSET,
                                &cxl_dstate->memory_device);
}

void cxl_event_set_status(CXLDeviceState *cxl_dstate, CXLEventLogType log_type,
                          bool available)
{
    if (available) {
        cxl_dstate->event_status |= (1 << log_type);
    } else {
        cxl_dstate->event_status &= ~(1 << log_type);
    }

    ARRAY_FIELD_DP64(cxl_dstate->dev_reg_state64, CXL_DEV_EVENT_STATUS,
                     EVENT_STATUS, cxl_dstate->event_status);
}

static void device_reg_init_common(CXLDeviceState *cxl_dstate)
{
    CXLEventLogType log;

    for (log = 0; log < CXL_EVENT_TYPE_MAX; log++) {
        cxl_event_set_status(cxl_dstate, log, false);
    }
}

static void mailbox_reg_init_common(CXLDeviceState *cxl_dstate, int msi_n)
{
    /* 2048 payload size */
    ARRAY_FIELD_DP32(cxl_dstate->mbox_reg_state32, CXL_DEV_MAILBOX_CAP,
                     PAYLOAD_SIZE, CXL_MAILBOX_PAYLOAD_SHIFT);
    cxl_dstate->payload_size = CXL_MAILBOX_MAX_PAYLOAD_SIZE;
    /* irq support */
    ARRAY_FIELD_DP32(cxl_dstate->mbox_reg_state32, CXL_DEV_MAILBOX_CAP,
                     BG_INT_CAP, 1);
    ARRAY_FIELD_DP32(cxl_dstate->mbox_reg_state32, CXL_DEV_MAILBOX_CAP,
                     MSI_N, msi_n);
    cxl_dstate->mbox_msi_n = msi_n;
    ARRAY_FIELD_DP32(cxl_dstate->mbox_reg_state32, CXL_DEV_MAILBOX_CAP,
                     MBOX_READY_TIME, 0); /* Not reported */
    ARRAY_FIELD_DP32(cxl_dstate->mbox_reg_state32, CXL_DEV_MAILBOX_CAP,
                     TYPE, 0); /* Inferred from class code */
}

static void memdev_reg_init_common(CXLDeviceState *cxl_dstate)
{
    uint64_t memdev_status_reg;

    memdev_status_reg = FIELD_DP64(0, CXL_MEM_DEV_STS, MEDIA_STATUS, 1);
    memdev_status_reg = FIELD_DP64(memdev_status_reg, CXL_MEM_DEV_STS,
                                   MBOX_READY, 1);
    cxl_dstate->memdev_status = memdev_status_reg;
}

void cxl_device_register_init_t3(CXLType3Dev *ct3d, int msi_n)
{
    CXLDeviceState *cxl_dstate = &ct3d->cxl_dstate;
    uint64_t *cap_h = cxl_dstate->caps_reg_state64;
    const int cap_count = 3;

    /* CXL Device Capabilities Array Register */
    ARRAY_FIELD_DP64(cap_h, CXL_DEV_CAP_ARRAY, CAP_ID, 0);
    ARRAY_FIELD_DP64(cap_h, CXL_DEV_CAP_ARRAY, CAP_VERSION, 1);
    ARRAY_FIELD_DP64(cap_h, CXL_DEV_CAP_ARRAY, CAP_COUNT, cap_count);

    cxl_device_cap_init(cxl_dstate, DEVICE_STATUS, 1,
                        CXL_DEVICE_STATUS_VERSION);
    device_reg_init_common(cxl_dstate);

    cxl_device_cap_init(cxl_dstate, MAILBOX, 2, CXL_DEV_MAILBOX_VERSION);
    mailbox_reg_init_common(cxl_dstate, msi_n);

    cxl_device_cap_init(cxl_dstate, MEMORY_DEVICE, 0x4000,
        CXL_MEM_DEV_STATUS_VERSION);
    memdev_reg_init_common(cxl_dstate);

    cxl_initialize_mailbox_t3(&ct3d->cci, DEVICE(ct3d),
                              CXL_MAILBOX_MAX_PAYLOAD_SIZE);
}

void cxl_device_register_init_swcci(CSWMBCCIDev *sw, int msi_n)
{
    CXLDeviceState *cxl_dstate = &sw->cxl_dstate;
    uint64_t *cap_h = cxl_dstate->caps_reg_state64;
    const int cap_count = 3;

    /* CXL Device Capabilities Array Register */
    ARRAY_FIELD_DP64(cap_h, CXL_DEV_CAP_ARRAY, CAP_ID, 0);
    ARRAY_FIELD_DP64(cap_h, CXL_DEV_CAP_ARRAY, CAP_VERSION, 1);
    ARRAY_FIELD_DP64(cap_h, CXL_DEV_CAP_ARRAY, CAP_COUNT, cap_count);

    cxl_device_cap_init(cxl_dstate, DEVICE_STATUS, 1, 2);
    device_reg_init_common(cxl_dstate);

    cxl_device_cap_init(cxl_dstate, MAILBOX, 2, 1);
    mailbox_reg_init_common(cxl_dstate, msi_n);

    cxl_device_cap_init(cxl_dstate, MEMORY_DEVICE, 0x4000, 1);
    memdev_reg_init_common(cxl_dstate);
}

uint64_t cxl_device_get_timestamp(CXLDeviceState *cxl_dstate)
{
    uint64_t time, delta;
    uint64_t final_time = 0;

    if (cxl_dstate->timestamp.set) {
        /* Find the delta from the last time the host set the time. */
        time = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        delta = time - cxl_dstate->timestamp.last_set;
        final_time = cxl_dstate->timestamp.host_set + delta;
    }

    return final_time;
}
