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

typedef struct AspeedOTPState {
    DeviceState parent_obj;

    BlockBackend *blk;

    uint64_t size;

    AddressSpace as;

    MemoryRegion mmio;

    uint8_t *storage;
} AspeedOTPState;

#endif /* ASPEED_OTP_H */
