/*
 * Copyright (c) 2017, Impinj, Inc.
 *
 * i.MX7 SNVS block emulation code
 *
 * Author: Andrey Smirnov <andrew.smirnov@gmail.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef IMX7_SNVS_H
#define IMX7_SNVS_H

#include "qemu/bitops.h"
#include "hw/sysbus.h"
#include "qom/object.h"


enum IMX7SNVSRegisters {
    SNVS_LPCR = 0x38,
    SNVS_LPCR_TOP   = BIT(6),
    SNVS_LPCR_DP_EN = BIT(5),
    SNVS_LPSRTCMR = 0x050, /* Secure Real Time Counter MSB Register */
    SNVS_LPSRTCLR = 0x054, /* Secure Real Time Counter LSB Register */
};

#define TYPE_IMX7_SNVS "imx7.snvs"
OBJECT_DECLARE_SIMPLE_TYPE(IMX7SNVSState, IMX7_SNVS)

struct IMX7SNVSState {
    /* <private> */
    SysBusDevice parent_obj;

    MemoryRegion mmio;

    uint64_t tick_offset;
    uint64_t lpcr;
};

#endif /* IMX7_SNVS_H */
