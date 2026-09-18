/*
 *  ASPEED OTP (One-Time Programmable) memory
 *
 *  Copyright (C) 2025 Aspeed
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef ASPEED_OTP_H
#define ASPEED_OTP_H

#include "system/memory.h"
#include "hw/block/block.h"
#include "system/address-spaces.h"

#define TYPE_ASPEED_OTP "aspeed-otp"
OBJECT_DECLARE_SIMPLE_TYPE(AspeedOTPState, ASPEED_OTP)

#define OTP_MEMORY_SIZE                 0x4000

/*
 * The OTP address space is indexed by dword address and is split into
 * two regions:
 *
 *  - [0, OTP_DATA_DWORD_COUNT]: the data region. Each address contains
 *    64 bits of data.
 *  - [OTP_DATA_DWORD_COUNT, OTP_MEMORY_SIZE / 4]: the configuration
 *    region. Each address contains 32 bits of data.
 */
#define OTP_DATA_DWORD_COUNT            (0x800)

typedef struct AspeedOTPState {
    DeviceState parent_obj;

    BlockBackend *blk;

    uint64_t size;

    AddressSpace as;

    MemoryRegion mmio;

    uint8_t *storage;
} AspeedOTPState;

#endif /* ASPEED_OTP_H */
