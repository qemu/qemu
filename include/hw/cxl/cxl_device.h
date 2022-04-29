/*
 * QEMU CXL Devices
 *
 * Copyright (c) 2020 Intel
 *
 * This work is licensed under the terms of the GNU GPL, version 2. See the
 * COPYING file in the top-level directory.
 */

#ifndef CXL_DEVICE_H
#define CXL_DEVICE_H

#include "hw/register.h"

/*
 * The following is how a CXL device's Memory Device registers are laid out.
 * The only requirement from the spec is that the capabilities array and the
 * capability headers start at offset 0 and are contiguously packed. The headers
 * themselves provide offsets to the register fields. For this emulation, the
 * actual registers  * will start at offset 0x80 (m == 0x80). No secondary
 * mailbox is implemented which means that the offset of the start of the
 * mailbox payload (n) is given by
 * n = m + sizeof(mailbox registers) + sizeof(device registers).
 *
 *                       +---------------------------------+
 *                       |                                 |
 *                       |    Memory Device Registers      |
 *                       |                                 |
 * n + PAYLOAD_SIZE_MAX  -----------------------------------
 *                  ^    |                                 |
 *                  |    |                                 |
 *                  |    |                                 |
 *                  |    |                                 |
 *                  |    |                                 |
 *                  |    |         Mailbox Payload         |
 *                  |    |                                 |
 *                  |    |                                 |
 *                  |    |                                 |
 *                  n    -----------------------------------
 *                  ^    |       Mailbox Registers         |
 *                  |    |                                 |
 *                  |    -----------------------------------
 *                  |    |                                 |
 *                  |    |        Device Registers         |
 *                  |    |                                 |
 *                  m    ---------------------------------->
 *                  ^    |  Memory Device Capability Header|
 *                  |    -----------------------------------
 *                  |    |     Mailbox Capability Header   |
 *                  |    -----------------------------------
 *                  |    |     Device Capability Header    |
 *                  |    -----------------------------------
 *                  |    |     Device Cap Array Register   |
 *                  0    +---------------------------------+
 *
 */

#define CXL_DEVICE_CAP_HDR1_OFFSET 0x10 /* Figure 138 */
#define CXL_DEVICE_CAP_REG_SIZE 0x10 /* 8.2.8.2 */
#define CXL_DEVICE_CAPS_MAX 4 /* 8.2.8.2.1 + 8.2.8.5 */

#define CXL_DEVICE_STATUS_REGISTERS_OFFSET 0x80 /* Read comment above */
#define CXL_DEVICE_STATUS_REGISTERS_LENGTH 0x8 /* 8.2.8.3.1 */

#define CXL_MAILBOX_REGISTERS_OFFSET \
    (CXL_DEVICE_STATUS_REGISTERS_OFFSET + CXL_DEVICE_STATUS_REGISTERS_LENGTH)
#define CXL_MAILBOX_REGISTERS_SIZE 0x20 /* 8.2.8.4, Figure 139 */
#define CXL_MAILBOX_PAYLOAD_SHIFT 11
#define CXL_MAILBOX_MAX_PAYLOAD_SIZE (1 << CXL_MAILBOX_PAYLOAD_SHIFT)
#define CXL_MAILBOX_REGISTERS_LENGTH \
    (CXL_MAILBOX_REGISTERS_SIZE + CXL_MAILBOX_MAX_PAYLOAD_SIZE)

typedef struct cxl_device_state {
    MemoryRegion device_registers;

    /* mmio for device capabilities array - 8.2.8.2 */
    MemoryRegion device;
    MemoryRegion caps;

    /* mmio for the mailbox registers 8.2.8.4 */
    MemoryRegion mailbox;

    /* memory region for persistent memory, HDM */
    uint64_t pmem_size;
} CXLDeviceState;

/* Initialize the register block for a device */
void cxl_device_register_block_init(Object *obj, CXLDeviceState *dev);

/* Set up default values for the register block */
void cxl_device_register_init_common(CXLDeviceState *dev);

/*
 * CXL 2.0 - 8.2.8.1 including errata F4
 * Documented as a 128 bit register, but 64 bit accesses and the second
 * 64 bits are currently reserved.
 */
REG64(CXL_DEV_CAP_ARRAY, 0) /* Documented as 128 bit register but 64 byte accesses */
    FIELD(CXL_DEV_CAP_ARRAY, CAP_ID, 0, 16)
    FIELD(CXL_DEV_CAP_ARRAY, CAP_VERSION, 16, 8)
    FIELD(CXL_DEV_CAP_ARRAY, CAP_COUNT, 32, 16)

/*
 * Helper macro to initialize capability headers for CXL devices.
 *
 * In the 8.2.8.2, this is listed as a 128b register, but in 8.2.8, it says:
 * > No registers defined in Section 8.2.8 are larger than 64-bits wide so that
 * > is the maximum access size allowed for these registers. If this rule is not
 * > followed, the behavior is undefined
 *
 * CXL 2.0 Errata F4 states futher that the layouts in the specification are
 * shown as greater than 128 bits, but implementations are expected to
 * use any size of access up to 64 bits.
 *
 * Here we've chosen to make it 4 dwords. The spec allows any pow2 multiple
 * access to be used for a register up to 64 bits.
 */
#define CXL_DEVICE_CAPABILITY_HEADER_REGISTER(n, offset)  \
    REG32(CXL_DEV_##n##_CAP_HDR0, offset)                 \
        FIELD(CXL_DEV_##n##_CAP_HDR0, CAP_ID, 0, 16)      \
        FIELD(CXL_DEV_##n##_CAP_HDR0, CAP_VERSION, 16, 8) \
    REG32(CXL_DEV_##n##_CAP_HDR1, offset + 4)             \
        FIELD(CXL_DEV_##n##_CAP_HDR1, CAP_OFFSET, 0, 32)  \
    REG32(CXL_DEV_##n##_CAP_HDR2, offset + 8)             \
        FIELD(CXL_DEV_##n##_CAP_HDR2, CAP_LENGTH, 0, 32)

CXL_DEVICE_CAPABILITY_HEADER_REGISTER(DEVICE_STATUS, CXL_DEVICE_CAP_HDR1_OFFSET)
CXL_DEVICE_CAPABILITY_HEADER_REGISTER(MAILBOX, CXL_DEVICE_CAP_HDR1_OFFSET + \
                                               CXL_DEVICE_CAP_REG_SIZE)

/* CXL 2.0 8.2.8.4.3 Mailbox Capabilities Register */
REG32(CXL_DEV_MAILBOX_CAP, 0)
    FIELD(CXL_DEV_MAILBOX_CAP, PAYLOAD_SIZE, 0, 5)
    FIELD(CXL_DEV_MAILBOX_CAP, INT_CAP, 5, 1)
    FIELD(CXL_DEV_MAILBOX_CAP, BG_INT_CAP, 6, 1)
    FIELD(CXL_DEV_MAILBOX_CAP, MSI_N, 7, 4)

/* CXL 2.0 8.2.8.4.4 Mailbox Control Register */
REG32(CXL_DEV_MAILBOX_CTRL, 4)
    FIELD(CXL_DEV_MAILBOX_CTRL, DOORBELL, 0, 1)
    FIELD(CXL_DEV_MAILBOX_CTRL, INT_EN, 1, 1)
    FIELD(CXL_DEV_MAILBOX_CTRL, BG_INT_EN, 2, 1)

/* CXL 2.0 8.2.8.4.5 Command Register */
REG64(CXL_DEV_MAILBOX_CMD, 8)
    FIELD(CXL_DEV_MAILBOX_CMD, COMMAND, 0, 8)
    FIELD(CXL_DEV_MAILBOX_CMD, COMMAND_SET, 8, 8)
    FIELD(CXL_DEV_MAILBOX_CMD, LENGTH, 16, 20)

/* CXL 2.0 8.2.8.4.6 Mailbox Status Register */
REG64(CXL_DEV_MAILBOX_STS, 0x10)
    FIELD(CXL_DEV_MAILBOX_STS, BG_OP, 0, 1)
    FIELD(CXL_DEV_MAILBOX_STS, ERRNO, 32, 16)
    FIELD(CXL_DEV_MAILBOX_STS, VENDOR_ERRNO, 48, 16)

/* CXL 2.0 8.2.8.4.7 Background Command Status Register */
REG64(CXL_DEV_BG_CMD_STS, 0x18)
    FIELD(CXL_DEV_BG_CMD_STS, OP, 0, 16)
    FIELD(CXL_DEV_BG_CMD_STS, PERCENTAGE_COMP, 16, 7)
    FIELD(CXL_DEV_BG_CMD_STS, RET_CODE, 32, 16)
    FIELD(CXL_DEV_BG_CMD_STS, VENDOR_RET_CODE, 48, 16)

/* CXL 2.0 8.2.8.4.8 Command Payload Registers */
REG32(CXL_DEV_CMD_PAYLOAD, 0x20)

#endif
