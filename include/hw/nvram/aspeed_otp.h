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
 *  - [OTP_DATA_DWORD_COUNT(OTP_CFG0), OTP_MEMORY_SIZE / 4]: the
 *    configuration region. Each address contains 32 bits of data.
 */
#define OTP_DATA_DWORD_COUNT            (0x800)

/* Start of the OTP configuration/strap region. */
#define OTP_CFG0                        (0x800)

/*
 * OTP straps are a 64-bit value packed as two 32-bit halves starting at
 * config word OTP_STRAP_START_INDEX (OTPCFG16 and OTPCFG17). Each strap
 * bit is stored redundantly in OTP_STRAP_COPY_NUM config words, spaced
 * (OTP_STRAP_BIT_NUM / 32) words apart -- i.e. one word per 32-bit half,
 * so the two halves interleave; the effective bit value is the XOR of
 * all copies, matching how the real hardware and the ast-otp reference
 * tool resolve straps.
 */
#define OTP_STRAP_START_INDEX           16
#define OTP_STRAP_BIT_NUM               64
#define OTP_STRAP_COPY_NUM              6

typedef struct AspeedOTPState {
    DeviceState parent_obj;

    BlockBackend *blk;

    uint64_t size;

    AddressSpace as;

    MemoryRegion mmio;

    uint8_t *storage;
} AspeedOTPState;

uint32_t aspeed_otp_read_config(AspeedOTPState *s, unsigned int cfg_word);
bool aspeed_otp_read_strap(AspeedOTPState *s, unsigned int bit);

#endif /* ASPEED_OTP_H */
