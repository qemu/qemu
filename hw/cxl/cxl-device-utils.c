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

static const MemoryRegionOps dev_ops = {
    .read = dev_reg_read,
    .write = NULL, /* status register is read only */
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
    .write = NULL, /* caps registers are read only */
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

    memory_region_add_subregion(&cxl_dstate->device_registers, 0,
                                &cxl_dstate->caps);
    memory_region_add_subregion(&cxl_dstate->device_registers,
                                CXL_DEVICE_STATUS_REGISTERS_OFFSET,
                                &cxl_dstate->device);
}

static void device_reg_init_common(CXLDeviceState *cxl_dstate) { }

void cxl_device_register_init_common(CXLDeviceState *cxl_dstate)
{
    uint64_t *cap_hdrs = cxl_dstate->caps_reg_state64;
    const int cap_count = 1;

    /* CXL Device Capabilities Array Register */
    ARRAY_FIELD_DP64(cap_hdrs, CXL_DEV_CAP_ARRAY, CAP_ID, 0);
    ARRAY_FIELD_DP64(cap_hdrs, CXL_DEV_CAP_ARRAY, CAP_VERSION, 1);
    ARRAY_FIELD_DP64(cap_hdrs, CXL_DEV_CAP_ARRAY, CAP_COUNT, cap_count);

    cxl_device_cap_init(cxl_dstate, DEVICE_STATUS, 1);
    device_reg_init_common(cxl_dstate);
}
