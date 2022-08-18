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
 * default memory mapped register rules in 8.2:
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

    if (size == 4) {
        return cxl_dstate->caps_reg_state32[offset / sizeof(*cxl_dstate->caps_reg_state32)];
    } else {
        return cxl_dstate->caps_reg_state64[offset / sizeof(*cxl_dstate->caps_reg_state64)];
    }
}

static uint64_t dev_reg_read(void *opaque, hwaddr offset, unsigned size)
{
    return 0;
}

static uint64_t mailbox_reg_read(void *opaque, hwaddr offset, unsigned size)
{
    CXLDeviceState *cxl_dstate = opaque;

    switch (size) {
    case 1:
        return cxl_dstate->mbox_reg_state[offset];
    case 2:
        return cxl_dstate->mbox_reg_state16[offset / size];
    case 4:
        return cxl_dstate->mbox_reg_state32[offset / size];
    case 8:
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
        /* BG not supported */
        /* fallthrough */
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
    CXLDeviceState *cxl_dstate = opaque;

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
        cxl_process_mailbox(cxl_dstate);
    }
}

static uint64_t mdev_reg_read(void *opaque, hwaddr offset, unsigned size)
{
    uint64_t retval = 0;

    retval = FIELD_DP64(retval, CXL_MEM_DEV_STS, MEDIA_STATUS, 1);
    retval = FIELD_DP64(retval, CXL_MEM_DEV_STS, MBOX_READY, 1);

    return retval;
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

void cxl_device_register_block_init(Object *obj, CXLDeviceState *cxl_dstate)
{
    /* This will be a BAR, so needs to be rounded up to pow2 for PCI spec */
    memory_region_init(&cxl_dstate->device_registers, obj, "device-registers",
                       pow2ceil(CXL_MMIO_SIZE));

    memory_region_init_io(&cxl_dstate->caps, obj, &caps_ops, cxl_dstate,
                          "cap-array", CXL_CAPS_SIZE);
    memory_region_init_io(&cxl_dstate->device, obj, &dev_ops, cxl_dstate,
                          "device-status", CXL_DEVICE_STATUS_REGISTERS_LENGTH);
    memory_region_init_io(&cxl_dstate->mailbox, obj, &mailbox_ops, cxl_dstate,
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

static void device_reg_init_common(CXLDeviceState *cxl_dstate) { }

static void mailbox_reg_init_common(CXLDeviceState *cxl_dstate)
{
    /* 2048 payload size, with no interrupt or background support */
    ARRAY_FIELD_DP32(cxl_dstate->mbox_reg_state32, CXL_DEV_MAILBOX_CAP,
                     PAYLOAD_SIZE, CXL_MAILBOX_PAYLOAD_SHIFT);
    cxl_dstate->payload_size = CXL_MAILBOX_MAX_PAYLOAD_SIZE;
}

static void memdev_reg_init_common(CXLDeviceState *cxl_dstate) { }

void cxl_device_register_init_common(CXLDeviceState *cxl_dstate)
{
    uint64_t *cap_hdrs = cxl_dstate->caps_reg_state64;
    const int cap_count = 3;

    /* CXL Device Capabilities Array Register */
    ARRAY_FIELD_DP64(cap_hdrs, CXL_DEV_CAP_ARRAY, CAP_ID, 0);
    ARRAY_FIELD_DP64(cap_hdrs, CXL_DEV_CAP_ARRAY, CAP_VERSION, 1);
    ARRAY_FIELD_DP64(cap_hdrs, CXL_DEV_CAP_ARRAY, CAP_COUNT, cap_count);

    cxl_device_cap_init(cxl_dstate, DEVICE_STATUS, 1);
    device_reg_init_common(cxl_dstate);

    cxl_device_cap_init(cxl_dstate, MAILBOX, 2);
    mailbox_reg_init_common(cxl_dstate);

    cxl_device_cap_init(cxl_dstate, MEMORY_DEVICE, 0x4000);
    memdev_reg_init_common(cxl_dstate);

    assert(cxl_initialize_mailbox(cxl_dstate) == 0);
}
